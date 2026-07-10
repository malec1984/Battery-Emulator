#ifndef DOIP_TYPES_H
#define DOIP_TYPES_H

// Shared constants and value types for the DoIP (ISO 13400-2) gateway.
// This header is transport-agnostic: no sockets, no Arduino types.

#include <stddef.h>
#include <stdint.h>
#include "../uds_isotp/isotp_config.h"  // CONFIG_ISOTP_MAX_MSG_LENGTH

// DoIP generic header: version(1) inverse(1) payload_type(2) payload_len(4).
static const uint8_t DOIP_HEADER_LEN = 8;
static const uint8_t DOIP_PROTOCOL_VERSION = 0x02;  // ISO 13400-2:2012
static const uint8_t DOIP_VERSION_INVERSE = 0xFD;   // = ~0x02

static const uint16_t DOIP_PORT = 13400;

// A diagnostic message carries a 2-byte SA + 2-byte TA ahead of the UDS payload.
static const uint8_t DOIP_DIAG_ADDR_LEN = 4;

// Largest UDS payload we tunnel (matches the ISO-TP reassembly limit).
static const size_t DOIP_MAX_UDS_LEN = CONFIG_ISOTP_MAX_MSG_LENGTH;
// Largest complete DoIP message we ever need to buffer (diagnostic message).
static const size_t DOIP_MAX_MSG_LEN = DOIP_HEADER_LEN + DOIP_DIAG_ADDR_LEN + DOIP_MAX_UDS_LEN;

// Payload types (ISO 13400-2 table 17).
enum DoipPayloadType : uint16_t {
  DOIP_PT_GENERIC_NACK = 0x0000,
  DOIP_PT_VEHICLE_ID_REQ = 0x0001,
  DOIP_PT_VEHICLE_ID_REQ_EID = 0x0002,
  DOIP_PT_VEHICLE_ID_REQ_VIN = 0x0003,
  DOIP_PT_VEHICLE_ANNOUNCEMENT = 0x0004,  // also the vehicle identification response
  DOIP_PT_ROUTING_ACT_REQ = 0x0005,
  DOIP_PT_ROUTING_ACT_RESP = 0x0006,
  DOIP_PT_ALIVE_CHECK_REQ = 0x0007,
  DOIP_PT_ALIVE_CHECK_RESP = 0x0008,
  DOIP_PT_ENTITY_STATUS_REQ = 0x4001,
  DOIP_PT_ENTITY_STATUS_RESP = 0x4002,
  DOIP_PT_POWER_MODE_REQ = 0x4003,
  DOIP_PT_POWER_MODE_RESP = 0x4004,
  DOIP_PT_DIAG_MESSAGE = 0x8001,
  DOIP_PT_DIAG_ACK = 0x8002,   // positive ack
  DOIP_PT_DIAG_NACK = 0x8003,  // negative ack
};

// Generic header negative-ack codes (payload type 0x0000).
enum DoipNackCode : uint8_t {
  DOIP_NACK_INCORRECT_PATTERN = 0x00,
  DOIP_NACK_UNKNOWN_PAYLOAD_TYPE = 0x01,
  DOIP_NACK_MESSAGE_TOO_LARGE = 0x02,
  DOIP_NACK_OUT_OF_MEMORY = 0x03,
  DOIP_NACK_INVALID_PAYLOAD_LENGTH = 0x04,
};

// Routing activation response codes (payload type 0x0006).
enum DoipRoutingActResult : uint8_t {
  DOIP_RA_UNKNOWN_SOURCE = 0x00,
  DOIP_RA_ALL_SOCKETS_ACTIVE = 0x01,
  DOIP_RA_SA_MISMATCH = 0x02,
  DOIP_RA_SA_ALREADY_ACTIVE = 0x03,
  DOIP_RA_SUCCESS = 0x10,
};

// Diagnostic message negative-ack codes (payload type 0x8003).
enum DoipDiagNackCode : uint8_t {
  DOIP_DIAG_NACK_INVALID_SOURCE = 0x00,
  DOIP_DIAG_NACK_UNKNOWN_TARGET = 0x01,
  DOIP_DIAG_NACK_MESSAGE_TOO_LARGE = 0x02,
  DOIP_DIAG_NACK_OUT_OF_MEMORY = 0x03,
  DOIP_DIAG_NACK_TARGET_UNREACHABLE = 0x04,
};

// DoIP entity node type (entity status response).
enum DoipNodeType : uint8_t {
  DOIP_NODE_GATEWAY = 0x00,
  DOIP_NODE_NODE = 0x01,
};

// Diagnostic power mode (power mode response).
enum DoipPowerMode : uint8_t {
  DOIP_POWER_NOT_READY = 0x00,
  DOIP_POWER_READY = 0x01,
  DOIP_POWER_NOT_SUPPORTED = 0x02,
};

// Static identity of this gateway. Filled by the glue layer from the battery
// (addresses) and the ESP32 MAC (eid/gid); see comm_doip.
struct DoipConfig {
  uint16_t entity_address;      // this gateway's own DoIP logical address
  uint16_t tester_address;      // the only tester source address we accept
  uint16_t target_address;      // ECU behind the gateway (the BMS)
  uint16_t functional_address;  // functional group address
  uint8_t vin[17];              // announced VIN (no NUL terminator on the wire)
  uint8_t eid[6];               // entity ID = ESP32 MAC (ISO 13400-2)
  uint8_t gid[6];               // group ID = eid (entity not part of a group)
};

// Live state the UDP responders report; read cheaply, never touches ISO-TP.
struct DoipEntityState {
  uint8_t open_tcp_sockets;  // 0 or 1 (single-session design)
  bool power_ready;          // target battery present and gateway-capable
};

// Synthetic VIN identifying this emulator gateway. Exactly 17 chars, no NUL on
// the wire. Not a real VIN (no valid check digit) — it names an emulator.
static const char DOIP_GATEWAY_VIN[17] = {'W', 'A', 'U', 'Z', 'Z', 'Z', '0', 'B', 'E',
                                          '_', 'G', 'A', 'T', 'E', 'W', 'A', 'Y'};

#endif  // DOIP_TYPES_H
