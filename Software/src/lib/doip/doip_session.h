#ifndef DOIP_SESSION_H
#define DOIP_SESSION_H

// One DoIP TCP connection: reassembles the byte stream into DoIP messages,
// runs routing activation / alive-check, and tunnels UDS via callbacks.
// Transport-agnostic — the glue layer wires the std::function members to a
// socket and to the UDS bridge. All methods run on a single thread (the socket
// task); no internal locking.

#include <functional>
#include <stddef.h>
#include <stdint.h>
#include "doip_codec.h"
#include "doip_types.h"

class DoipSession {
 public:
  explicit DoipSession(const DoipConfig& cfg) : _cfg(cfg) {}

  // Reset to the pre-connection state (call on connect and disconnect).
  void reset();

  // Feed received TCP bytes; dispatches every complete DoIP message.
  void feed(const uint8_t* data, size_t len);

  // Deliver an assembled UDS response from the bridge; wraps it as 0x8001.
  void deliver_uds_response(const uint8_t* uds, size_t len);

  // Drive alive-check / inactivity timers. now_ms is a millisecond clock.
  void tick(uint32_t now_ms);

  bool activated() const { return _activated; }

  // --- Callbacks wired by the glue layer -----------------------------------

  // Send raw bytes to the tester over TCP.
  std::function<void(const uint8_t* frame, size_t len)> on_send;

  // Forward a UDS request onto CAN. `target` selects physical vs functional.
  // Return false to reject (emits a 0x8003 target-unreachable NACK).
  std::function<bool(uint16_t target, const uint8_t* uds, size_t len)> on_uds_request;

  // Routing activation accepted → acquire exclusive control of the channel.
  std::function<void()> on_activated;

  // Connection ending → release exclusive control.
  std::function<void()> on_closed;

 private:
  void handle_message(uint16_t type, const uint8_t* payload, uint32_t plen, uint32_t now_hint);
  void handle_routing_activation(const uint8_t* payload, uint32_t plen);
  void handle_diagnostic_message(const uint8_t* payload, uint32_t plen);
  void send_diag_ack(uint16_t sa, uint16_t ta, bool positive, uint8_t code);
  void send_generic_nack(DoipNackCode code);

  const DoipConfig& _cfg;

  // Stream reassembly buffer for one in-flight DoIP message.
  uint8_t _rx[DOIP_MAX_MSG_LEN];
  size_t _rx_len = 0;

  bool _activated = false;
  uint32_t _last_activity_ms = 0;
  bool _activity_since_tick = false;  // set by feed(), consumed by tick()
  bool _alive_check_pending = false;
  uint32_t _alive_check_ms = 0;

  // No traffic for this long → probe the tester with an alive check request.
  static const uint32_t ALIVE_CHECK_IDLE_MS = 5000;
  // No alive check response within this window after probing → drop the session.
  static const uint32_t ALIVE_CHECK_WAIT_MS = 500;
};

#endif  // DOIP_SESSION_H
