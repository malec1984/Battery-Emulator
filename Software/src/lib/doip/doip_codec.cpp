#include "doip_codec.h"
#include <string.h>

// --- Generic header ---------------------------------------------------------

size_t doip_write_header(uint8_t* out, size_t cap, uint16_t payload_type, uint32_t payload_len) {
  if (cap < DOIP_HEADER_LEN) {
    return 0;
  }
  out[0] = DOIP_PROTOCOL_VERSION;
  out[1] = DOIP_VERSION_INVERSE;
  out[2] = (uint8_t)(payload_type >> 8);
  out[3] = (uint8_t)(payload_type & 0xFF);
  out[4] = (uint8_t)(payload_len >> 24);
  out[5] = (uint8_t)(payload_len >> 16);
  out[6] = (uint8_t)(payload_len >> 8);
  out[7] = (uint8_t)(payload_len & 0xFF);
  return DOIP_HEADER_LEN;
}

bool doip_parse_header(const uint8_t* in, size_t len, uint16_t* payload_type, uint32_t* payload_len,
                       DoipNackCode* nack) {
  if (len < DOIP_HEADER_LEN) {
    *nack = DOIP_NACK_INVALID_PAYLOAD_LENGTH;
    return false;
  }
  // The inverse byte must be the bitwise complement of the version byte.
  // Accept the negotiated versions (0x01/0x02/0x03) and 0xFF, which ISO 13400-2
  // mandates for vehicle identification requests sent before a version is known
  // (e.g. ODIS discovery uses version 0xFF / inverse 0x00).
  const uint8_t version = in[0];
  const bool inverse_ok = (in[1] == (uint8_t)(~version));
  const bool version_ok = (version == 0x01 || version == 0x02 || version == 0x03 || version == 0xFF);
  if (!inverse_ok || !version_ok) {
    *nack = DOIP_NACK_INCORRECT_PATTERN;
    return false;
  }
  *payload_type = (uint16_t)((in[2] << 8) | in[3]);
  *payload_len = ((uint32_t)in[4] << 24) | ((uint32_t)in[5] << 16) | ((uint32_t)in[6] << 8) | in[7];
  return true;
}

size_t doip_build_generic_nack(uint8_t* out, size_t cap, DoipNackCode code) {
  if (cap < DOIP_HEADER_LEN + 1) {
    return 0;
  }
  doip_write_header(out, cap, DOIP_PT_GENERIC_NACK, 1);
  out[DOIP_HEADER_LEN] = (uint8_t)code;
  return DOIP_HEADER_LEN + 1;
}

// --- UDP --------------------------------------------------------------------

DoipUdpRequest doip_classify_udp(const uint8_t* in, size_t len) {
  uint16_t type;
  uint32_t plen;
  DoipNackCode nack;
  if (!doip_parse_header(in, len, &type, &plen, &nack)) {
    return DoipUdpRequest::None;
  }
  switch (type) {
    case DOIP_PT_VEHICLE_ID_REQ:
      return DoipUdpRequest::VehicleId;
    case DOIP_PT_VEHICLE_ID_REQ_EID:
      return DoipUdpRequest::VehicleIdEid;
    case DOIP_PT_VEHICLE_ID_REQ_VIN:
      return DoipUdpRequest::VehicleIdVin;
    case DOIP_PT_ENTITY_STATUS_REQ:
      return DoipUdpRequest::EntityStatus;
    case DOIP_PT_POWER_MODE_REQ:
      return DoipUdpRequest::PowerMode;
    default:
      return DoipUdpRequest::None;
  }
}

bool doip_vehicle_id_selects_us(const DoipConfig& cfg, const uint8_t* in, size_t len) {
  if (len < DOIP_HEADER_LEN) {
    return false;
  }
  const uint16_t type = (uint16_t)((in[2] << 8) | in[3]);
  const uint8_t* payload = in + DOIP_HEADER_LEN;
  const size_t payload_len = len - DOIP_HEADER_LEN;
  if (type == DOIP_PT_VEHICLE_ID_REQ_EID) {
    return payload_len >= 6 && memcmp(payload, cfg.eid, 6) == 0;
  }
  if (type == DOIP_PT_VEHICLE_ID_REQ_VIN) {
    return payload_len >= 17 && memcmp(payload, cfg.vin, 17) == 0;
  }
  // Plain vehicle identification request (0x0001) selects every entity.
  return true;
}

size_t doip_build_announcement(const DoipConfig& cfg, uint8_t* out, size_t cap) {
  // Payload: VIN(17) LA(2) EID(6) GID(6) further-action(1) = 32 bytes.
  const uint32_t payload_len = 17 + 2 + 6 + 6 + 1;
  if (cap < DOIP_HEADER_LEN + payload_len) {
    return 0;
  }
  doip_write_header(out, cap, DOIP_PT_VEHICLE_ANNOUNCEMENT, payload_len);
  uint8_t* p = out + DOIP_HEADER_LEN;
  memcpy(p, cfg.vin, 17);
  p += 17;
  *p++ = (uint8_t)(cfg.entity_address >> 8);
  *p++ = (uint8_t)(cfg.entity_address & 0xFF);
  memcpy(p, cfg.eid, 6);
  p += 6;
  memcpy(p, cfg.gid, 6);
  p += 6;
  *p++ = 0x00;  // further action required: none (VIN/GID known)
  return DOIP_HEADER_LEN + payload_len;
}

size_t doip_build_entity_status(const DoipConfig& cfg, const DoipEntityState& st, uint8_t* out, size_t cap) {
  (void)cfg;
  // Payload: node type(1) MCTS(1) NCTS(1) MDS(4) = 7 bytes.
  const uint32_t payload_len = 1 + 1 + 1 + 4;
  if (cap < DOIP_HEADER_LEN + payload_len) {
    return 0;
  }
  doip_write_header(out, cap, DOIP_PT_ENTITY_STATUS_RESP, payload_len);
  uint8_t* p = out + DOIP_HEADER_LEN;
  *p++ = DOIP_NODE_GATEWAY;        // we bridge to CAN
  *p++ = 1;                        // max concurrent TCP sockets (exclusive design)
  *p++ = st.open_tcp_sockets;      // currently open sockets
  const uint32_t mds = (uint32_t)DOIP_MAX_UDS_LEN;
  *p++ = (uint8_t)(mds >> 24);
  *p++ = (uint8_t)(mds >> 16);
  *p++ = (uint8_t)(mds >> 8);
  *p++ = (uint8_t)(mds & 0xFF);
  return DOIP_HEADER_LEN + payload_len;
}

size_t doip_build_power_mode(const DoipEntityState& st, uint8_t* out, size_t cap) {
  const uint32_t payload_len = 1;
  if (cap < DOIP_HEADER_LEN + payload_len) {
    return 0;
  }
  doip_write_header(out, cap, DOIP_PT_POWER_MODE_RESP, payload_len);
  out[DOIP_HEADER_LEN] = st.power_ready ? DOIP_POWER_READY : DOIP_POWER_NOT_READY;
  return DOIP_HEADER_LEN + payload_len;
}

// --- UDS routing ------------------------------------------------------------

bool doip_uds_has_subfunction(uint8_t sid) {
  switch (sid) {
    case 0x10:  // DiagnosticSessionControl
    case 0x11:  // ECUReset
    case 0x19:  // ReadDTCInformation
    case 0x27:  // SecurityAccess
    case 0x28:  // CommunicationControl
    case 0x29:  // Authentication
    case 0x2A:  // ReadDataByPeriodicIdentifier
    case 0x31:  // RoutineControl
    case 0x3E:  // TesterPresent
    case 0x83:  // AccessTimingParameter
    case 0x84:  // SecuredDataTransmission
    case 0x85:  // ControlDTCSetting
    case 0x86:  // ResponseOnEvent
    case 0x87:  // LinkControl
      return true;
    default:
      return false;
  }
}

UdsRoute doip_classify_uds(const DoipConfig& cfg, uint16_t target, const uint8_t* uds, size_t len) {
  UdsRoute route;
  route.functional = (target == cfg.functional_address);
  route.expect_response = true;
  // A positive response is suppressed only when the service carries a sub-function
  // and bit 7 of that sub-function byte is set.
  if (len >= 2 && doip_uds_has_subfunction(uds[0]) && (uds[1] & 0x80)) {
    route.expect_response = false;
  }
  return route;
}
