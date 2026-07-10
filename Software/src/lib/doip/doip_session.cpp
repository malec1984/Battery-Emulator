#include "doip_session.h"
#include <string.h>

void DoipSession::reset() {
  _rx_len = 0;
  _activated = false;
  _activity_since_tick = false;
  _alive_check_pending = false;
  _last_activity_ms = 0;
  _alive_check_ms = 0;
}

void DoipSession::feed(const uint8_t* data, size_t len) {
  // Inbound traffic proves the tester is alive; tick() folds this into the timer.
  _activity_since_tick = true;
  _alive_check_pending = false;

  while (len > 0) {
    size_t space = sizeof(_rx) - _rx_len;
    if (space == 0) {
      // Buffer full without a complete message: the stream is oversized or out of
      // sync. NACK and resync from the next bytes (best effort).
      send_generic_nack(DOIP_NACK_MESSAGE_TOO_LARGE);
      reset();
      space = sizeof(_rx);
    }
    const size_t copy = (len < space) ? len : space;
    memcpy(_rx + _rx_len, data, copy);
    _rx_len += copy;
    data += copy;
    len -= copy;

    // Drain every complete DoIP message currently buffered.
    for (;;) {
      if (_rx_len < DOIP_HEADER_LEN) {
        break;
      }
      uint16_t type;
      uint32_t plen;
      DoipNackCode nack;
      if (!doip_parse_header(_rx, _rx_len, &type, &plen, &nack)) {
        send_generic_nack(nack);
        reset();
        break;
      }
      if (plen > sizeof(_rx) - DOIP_HEADER_LEN) {
        // Too large to ever buffer — cannot resync mid-message.
        send_generic_nack(DOIP_NACK_MESSAGE_TOO_LARGE);
        reset();
        break;
      }
      const size_t total = DOIP_HEADER_LEN + plen;
      if (_rx_len < total) {
        break;  // wait for the rest of the message
      }
      handle_message(type, _rx + DOIP_HEADER_LEN, plen, 0);
      // reset() may have been called inside handling; if so, stop draining.
      if (_rx_len == 0) {
        break;
      }
      memmove(_rx, _rx + total, _rx_len - total);
      _rx_len -= total;
    }
  }
}

void DoipSession::handle_message(uint16_t type, const uint8_t* payload, uint32_t plen, uint32_t /*now_hint*/) {
  switch (type) {
    case DOIP_PT_ROUTING_ACT_REQ:
      handle_routing_activation(payload, plen);
      break;
    case DOIP_PT_DIAG_MESSAGE:
      handle_diagnostic_message(payload, plen);
      break;
    case DOIP_PT_ALIVE_CHECK_RESP:
      // Tester answered our alive check; activity is already refreshed in feed().
      break;
    case DOIP_PT_ALIVE_CHECK_REQ: {
      // Reply with our logical address.
      uint8_t msg[DOIP_HEADER_LEN + 2];
      doip_write_header(msg, sizeof(msg), DOIP_PT_ALIVE_CHECK_RESP, 2);
      msg[DOIP_HEADER_LEN] = (uint8_t)(_cfg.entity_address >> 8);
      msg[DOIP_HEADER_LEN + 1] = (uint8_t)(_cfg.entity_address & 0xFF);
      if (on_send) {
        on_send(msg, sizeof(msg));
      }
      break;
    }
    default:
      send_generic_nack(DOIP_NACK_UNKNOWN_PAYLOAD_TYPE);
      break;
  }
}

void DoipSession::handle_routing_activation(const uint8_t* payload, uint32_t plen) {
  // Request: SA(2) activation_type(1) reserved(4) [OEM(4)]. Minimum 7 bytes.
  if (plen < 7) {
    send_generic_nack(DOIP_NACK_INVALID_PAYLOAD_LENGTH);
    return;
  }
  const uint16_t sa = (uint16_t)((payload[0] << 8) | payload[1]);

  uint8_t result;
  if (sa != _cfg.tester_address) {
    result = DOIP_RA_UNKNOWN_SOURCE;
  } else if (_activated) {
    // Same tester re-activating is fine; a different one is rejected upstream by
    // the single-connection glue, so treat any activated state as success here.
    result = DOIP_RA_SUCCESS;
  } else {
    result = DOIP_RA_SUCCESS;
  }

  // Response: tester_SA(2) entity_SA(2) code(1) reserved(4) = 9 bytes.
  uint8_t msg[DOIP_HEADER_LEN + 9];
  doip_write_header(msg, sizeof(msg), DOIP_PT_ROUTING_ACT_RESP, 9);
  uint8_t* p = msg + DOIP_HEADER_LEN;
  *p++ = (uint8_t)(sa >> 8);
  *p++ = (uint8_t)(sa & 0xFF);
  *p++ = (uint8_t)(_cfg.entity_address >> 8);
  *p++ = (uint8_t)(_cfg.entity_address & 0xFF);
  *p++ = result;
  memset(p, 0, 4);  // reserved
  if (on_send) {
    on_send(msg, sizeof(msg));
  }

  if (result == DOIP_RA_SUCCESS && !_activated) {
    _activated = true;
    if (on_activated) {
      on_activated();
    }
  }
}

void DoipSession::handle_diagnostic_message(const uint8_t* payload, uint32_t plen) {
  // Payload: SA(2) TA(2) UDS(1..). Minimum 5 bytes.
  if (plen < DOIP_DIAG_ADDR_LEN + 1) {
    send_generic_nack(DOIP_NACK_INVALID_PAYLOAD_LENGTH);
    return;
  }
  const uint16_t sa = (uint16_t)((payload[0] << 8) | payload[1]);
  const uint16_t ta = (uint16_t)((payload[2] << 8) | payload[3]);
  const uint8_t* uds = payload + DOIP_DIAG_ADDR_LEN;
  const size_t uds_len = plen - DOIP_DIAG_ADDR_LEN;

  if (!_activated || sa != _cfg.tester_address) {
    send_diag_ack(sa, ta, false, DOIP_DIAG_NACK_INVALID_SOURCE);
    return;
  }
  const bool functional = (ta == _cfg.functional_address);
  if (ta != _cfg.target_address && !functional) {
    send_diag_ack(sa, ta, false, DOIP_DIAG_NACK_UNKNOWN_TARGET);
    return;
  }
  if (uds_len > DOIP_MAX_UDS_LEN) {
    send_diag_ack(sa, ta, false, DOIP_DIAG_NACK_MESSAGE_TOO_LARGE);
    return;
  }
  // Functional addressing has no flow control, so only single frames fit.
  if (functional && uds_len > 7) {
    send_diag_ack(sa, ta, false, DOIP_DIAG_NACK_MESSAGE_TOO_LARGE);
    return;
  }

  const bool accepted = on_uds_request && on_uds_request(ta, uds, uds_len);
  if (accepted) {
    send_diag_ack(sa, ta, true, 0x00);
  } else {
    send_diag_ack(sa, ta, false, DOIP_DIAG_NACK_TARGET_UNREACHABLE);
  }
}

void DoipSession::send_diag_ack(uint16_t sa, uint16_t ta, bool positive, uint8_t code) {
  // In the ack, the source is the target that was addressed and the target is
  // the original source (tester).
  uint8_t msg[DOIP_HEADER_LEN + 5];
  doip_write_header(msg, sizeof(msg), positive ? DOIP_PT_DIAG_ACK : DOIP_PT_DIAG_NACK, 5);
  uint8_t* p = msg + DOIP_HEADER_LEN;
  *p++ = (uint8_t)(ta >> 8);
  *p++ = (uint8_t)(ta & 0xFF);
  *p++ = (uint8_t)(sa >> 8);
  *p++ = (uint8_t)(sa & 0xFF);
  *p++ = code;
  if (on_send) {
    on_send(msg, sizeof(msg));
  }
}

void DoipSession::send_generic_nack(DoipNackCode code) {
  uint8_t msg[DOIP_HEADER_LEN + 1];
  const size_t n = doip_build_generic_nack(msg, sizeof(msg), code);
  if (n && on_send) {
    on_send(msg, n);
  }
}

void DoipSession::deliver_uds_response(const uint8_t* uds, size_t len) {
  if (len > DOIP_MAX_UDS_LEN) {
    len = DOIP_MAX_UDS_LEN;
  }
  uint8_t msg[DOIP_MAX_MSG_LEN];
  const uint32_t plen = (uint32_t)(DOIP_DIAG_ADDR_LEN + len);
  doip_write_header(msg, sizeof(msg), DOIP_PT_DIAG_MESSAGE, plen);
  uint8_t* p = msg + DOIP_HEADER_LEN;
  // Response comes from the ECU (target) and is addressed to the tester.
  *p++ = (uint8_t)(_cfg.target_address >> 8);
  *p++ = (uint8_t)(_cfg.target_address & 0xFF);
  *p++ = (uint8_t)(_cfg.tester_address >> 8);
  *p++ = (uint8_t)(_cfg.tester_address & 0xFF);
  memcpy(p, uds, len);
  if (on_send) {
    on_send(msg, DOIP_HEADER_LEN + plen);
  }
}

void DoipSession::tick(uint32_t now_ms) {
  if (_last_activity_ms == 0) {
    _last_activity_ms = now_ms;
  }
  if (_activity_since_tick) {
    _activity_since_tick = false;
    _last_activity_ms = now_ms;
  }
  if (!_activated) {
    _last_activity_ms = now_ms;  // idle timers only run for an active session
    return;
  }

  if (_alive_check_pending) {
    if (now_ms - _alive_check_ms >= ALIVE_CHECK_WAIT_MS) {
      // No response to the alive check — drop the session.
      if (on_closed) {
        on_closed();
      }
      reset();
    }
    return;
  }

  if (now_ms - _last_activity_ms >= ALIVE_CHECK_IDLE_MS) {
    uint8_t msg[DOIP_HEADER_LEN];
    doip_write_header(msg, sizeof(msg), DOIP_PT_ALIVE_CHECK_REQ, 0);
    if (on_send) {
      on_send(msg, sizeof(msg));
    }
    _alive_check_pending = true;
    _alive_check_ms = now_ms;
  }
}
