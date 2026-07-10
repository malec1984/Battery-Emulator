#include "comm_doip.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <string.h>

#include "../../lib/doip/doip_codec.h"
#include "../../lib/doip/doip_session.h"
#include "../../lib/doip/doip_types.h"
#include "../../lib/mathieucarbou-AsyncTCPSock/src/AsyncTCP.h"
#include "../../battery/BATTERIES.h"
#include "../../devboard/utils/logging.h"
#include "AsyncUDP.h"

bool doip_enabled = false;

namespace {

// --- Cross-task bridge queues ----------------------------------------------
// Producer/consumer split so the ISO-TP state machine is only ever touched on
// the core task, and the AsyncClient only ever on the async socket task.

// Tracked physical request: response expected, forwarded via the ISO-TP layer.
struct TrackedReq {
  uint16_t len;
  uint8_t data[DOIP_MAX_UDS_LEN];
};
// Fire-and-forget single frame (functional and/or suppressed positive response).
struct OneshotReq {
  bool functional;
  uint8_t len;
  uint8_t data[8];
};
// Assembled UDS response heading back to the tester.
struct UdsResp {
  uint16_t len;
  uint8_t data[DOIP_MAX_UDS_LEN];
};

QueueHandle_t s_q_tracked = nullptr;  // async -> core
QueueHandle_t s_q_oneshot = nullptr;  // async -> core
QueueHandle_t s_q_resp = nullptr;     // core  -> async

// Exclusive-control handshake flags (set on async task, acted on by the pump).
volatile bool s_want_acquire = false;
volatile bool s_want_release = false;
bool s_acquired = false;  // core-task-only view of channel ownership

DoipConfig s_cfg;
DoipSession* s_session = nullptr;  // allocated once in init_doip()
AsyncServer* s_server = nullptr;
AsyncUDP s_udp;
AsyncClient* s_client = nullptr;  // the single active tester (async task only)

Battery* target_battery() {
  return battery;  // primary battery; generic interface, MEB implements it first
}

// --- Async-task helpers -----------------------------------------------------

void drain_responses_to_client() {
  // Runs on the async task; safe to touch s_session / s_client here.
  UdsResp resp;
  while (s_q_resp && xQueueReceive(s_q_resp, &resp, 0) == pdTRUE) {
    if (s_client && s_session) {
      s_session->deliver_uds_response(resp.data, resp.len);
    }
  }
}

void close_client() {
  // Async task only, and only from onDisconnect: _close() invokes the discard
  // callback as its final statement, so deleting the client here is safe.
  if (s_client) {
    delete s_client;
    s_client = nullptr;
  }
  if (s_session) {
    s_session->reset();
  }
  s_want_release = true;  // pump releases the channel on the core task
}

void wire_client(AsyncClient* client) {
  s_client = client;
  if (s_session) {
    s_session->reset();
  }

  client->onData([](void*, AsyncClient*, void* data, size_t len) {
    if (s_session) {
      s_session->feed((const uint8_t*)data, len);
    }
    drain_responses_to_client();
  });
  client->onPoll([](void*, AsyncClient*) {
    if (s_session) {
      s_session->tick(millis());
    }
    drain_responses_to_client();
  });
  // Delete only in onDisconnect: on the error path the library still calls the
  // discard (disconnect) callback afterwards, so onError must not free the client.
  client->onDisconnect([](void*, AsyncClient*) { close_client(); });
}

void on_new_client(AsyncClient* client) {
  if (s_client != nullptr) {
    // Single-session design: reject additional testers.
    client->close(true);
    delete client;
    return;
  }
  wire_client(client);
}

DoipEntityState live_entity_state() {
  Battery* b = target_battery();
  DoipEntityState st;
  st.open_tcp_sockets = (s_client != nullptr) ? 1 : 0;
  st.power_ready = (b != nullptr) && b->supports_uds_gateway();
  return st;
}

void on_udp_packet(AsyncUDPPacket& packet) {
  const uint8_t* in = packet.data();
  const size_t len = packet.length();
  uint8_t out[64];
  size_t n = 0;

  switch (doip_classify_udp(in, len)) {
    case DoipUdpRequest::VehicleId:
    case DoipUdpRequest::VehicleIdEid:
    case DoipUdpRequest::VehicleIdVin:
      if (doip_vehicle_id_selects_us(s_cfg, in, len)) {
        n = doip_build_announcement(s_cfg, out, sizeof(out));
      }
      break;
    case DoipUdpRequest::EntityStatus:
      n = doip_build_entity_status(s_cfg, live_entity_state(), out, sizeof(out));
      break;
    case DoipUdpRequest::PowerMode:
      n = doip_build_power_mode(live_entity_state(), out, sizeof(out));
      break;
    case DoipUdpRequest::None:
      break;
  }
  if (n) {
    packet.write(out, n);  // replies to the datagram's source
  }
}

// --- Session callbacks (async task) ----------------------------------------

bool session_on_uds_request(uint16_t target, const uint8_t* uds, size_t len) {
  if (len == 0 || len > DOIP_MAX_UDS_LEN) {
    return false;
  }
  const UdsRoute route = doip_classify_uds(s_cfg, target, uds, len);

  // Oneshot lane: functional and/or suppressed-positive-response, single frame.
  if ((route.functional || !route.expect_response) && len <= 7) {
    OneshotReq req;
    req.functional = route.functional;
    req.len = (uint8_t)len;
    memcpy(req.data, uds, len);
    return s_q_oneshot && xQueueSend(s_q_oneshot, &req, 0) == pdTRUE;
  }

  // Tracked lane: physical request, response expected.
  TrackedReq req;
  req.len = (uint16_t)len;
  memcpy(req.data, uds, len);
  return s_q_tracked && xQueueSend(s_q_tracked, &req, 0) == pdTRUE;
}

}  // namespace

void init_doip() {
  if (!doip_enabled) {
    return;
  }
  Battery* b = target_battery();
  if (b == nullptr || !b->supports_uds_gateway()) {
    logging.println("DoIP: no UDS-gateway-capable battery, not starting");
    return;
  }

  // Identity: addresses from the battery, EID/GID from the MAC, VIN constant.
  memset(&s_cfg, 0, sizeof(s_cfg));
  s_cfg.entity_address = b->uds_gateway_entity_address();
  s_cfg.tester_address = b->uds_gateway_tester_address();
  s_cfg.target_address = b->uds_gateway_target_address();
  s_cfg.functional_address = b->uds_gateway_functional_address();
  memcpy(s_cfg.vin, DOIP_GATEWAY_VIN, sizeof(s_cfg.vin));
  esp_read_mac(s_cfg.eid, ESP_MAC_WIFI_STA);  // ISO 13400-2: EID = the MAC
  memcpy(s_cfg.gid, s_cfg.eid, sizeof(s_cfg.gid));

  s_q_tracked = xQueueCreate(4, sizeof(TrackedReq));
  s_q_oneshot = xQueueCreate(8, sizeof(OneshotReq));
  s_q_resp = xQueueCreate(4, sizeof(UdsResp));
  if (!s_q_tracked || !s_q_oneshot || !s_q_resp) {
    logging.println("DoIP: queue allocation failed");
    return;
  }

  // Response sink runs on the core task; hand bytes to the async task.
  b->uds_gateway_set_sink([](const uint8_t* data, int len) {
    if (!s_q_resp || len <= 0) {
      return;
    }
    UdsResp resp;
    resp.len = (uint16_t)((len > (int)DOIP_MAX_UDS_LEN) ? DOIP_MAX_UDS_LEN : len);
    memcpy(resp.data, data, resp.len);
    xQueueSend(s_q_resp, &resp, 0);
  });

  s_session = new DoipSession(s_cfg);
  s_session->on_send = [](const uint8_t* frame, size_t len) {
    if (s_client) {
      s_client->write((const char*)frame, len);
    }
  };
  s_session->on_uds_request = session_on_uds_request;
  s_session->on_activated = []() { s_want_acquire = true; };
  s_session->on_closed = []() { s_want_release = true; };

  s_server = new AsyncServer(DOIP_PORT);
  s_server->onClient([](void*, AsyncClient* c) { on_new_client(c); }, nullptr);
  s_server->begin();

  s_udp.listen(DOIP_PORT);
  s_udp.onPacket(on_udp_packet);

  // Three startup vehicle announcements (ISO 13400-2).
  uint8_t ann[64];
  const size_t n = doip_build_announcement(s_cfg, ann, sizeof(ann));
  for (int i = 0; n && i < 3; i++) {
    s_udp.broadcast(ann, n);
  }

  logging.println("DoIP: gateway started on port 13400");
}

void doip_bridge_pump() {
  if (!doip_enabled || s_session == nullptr) {
    return;
  }
  Battery* b = target_battery();
  if (b == nullptr) {
    return;
  }

  // Release takes priority and flushes any queued work.
  if (s_want_release) {
    s_want_release = false;
    s_want_acquire = false;
    if (s_acquired) {
      b->uds_gateway_release();
      s_acquired = false;
      logging.println("DoIP: channel released, internal UDS polling resumed");
    }
    TrackedReq t;
    while (xQueueReceive(s_q_tracked, &t, 0) == pdTRUE) {
    }
    OneshotReq o;
    while (xQueueReceive(s_q_oneshot, &o, 0) == pdTRUE) {
    }
    return;
  }

  // Acquire the channel before forwarding anything.
  if (!s_acquired) {
    if (s_want_acquire && b->uds_gateway_acquire()) {
      s_acquired = true;
      logging.println("DoIP: channel acquired, internal UDS polling suspended");
    } else {
      return;  // keep trying next tick
    }
  }

  // Oneshot lane: dispatch everything immediately, even while a tracked
  // transaction is pending (keeps TesterPresent from being head-of-line blocked).
  OneshotReq one;
  while (xQueueReceive(s_q_oneshot, &one, 0) == pdTRUE) {
    b->uds_gateway_send_oneshot(one.data, one.len, one.functional);
  }

  // Tracked lane: dispatch one request when the channel is free.
  TrackedReq trk;
  if (xQueuePeek(s_q_tracked, &trk, 0) == pdTRUE) {
    if (b->uds_gateway_send(trk.data, trk.len)) {
      xQueueReceive(s_q_tracked, &trk, 0);  // consume the one we just sent
    }
  }
}
