#ifndef DOIP_CODEC_H
#define DOIP_CODEC_H

// Pure DoIP (ISO 13400-2) encode/decode helpers. No sockets, no state.
// All build_* functions return the number of bytes written, or 0 if the
// output buffer is too small.

#include <stddef.h>
#include <stdint.h>
#include "doip_types.h"

// --- Generic header ---------------------------------------------------------

// Write an 8-byte DoIP header for payload_type/payload_len into out[0..7].
// Caller must ensure cap >= DOIP_HEADER_LEN.
size_t doip_write_header(uint8_t* out, size_t cap, uint16_t payload_type, uint32_t payload_len);

// Parse and validate a DoIP header. On success fills *payload_type / *payload_len
// and returns true. On a header pattern error, fills *nack with the reason and
// returns false. Needs at least DOIP_HEADER_LEN bytes available in `in`.
bool doip_parse_header(const uint8_t* in, size_t len, uint16_t* payload_type, uint32_t* payload_len,
                       DoipNackCode* nack);

// Build a complete generic negative-ack message (header + 1 code byte).
size_t doip_build_generic_nack(uint8_t* out, size_t cap, DoipNackCode code);

// --- UDP: vehicle identification / entity status / power mode ---------------

enum class DoipUdpRequest { None, VehicleId, VehicleIdEid, VehicleIdVin, EntityStatus, PowerMode };

// Classify a single received UDP datagram by its DoIP payload type.
DoipUdpRequest doip_classify_udp(const uint8_t* in, size_t len);

// True if a vehicle-identification-by-EID/VIN request selects this entity.
// For DoipUdpRequest::VehicleId (plain) this always returns true.
bool doip_vehicle_id_selects_us(const DoipConfig& cfg, const uint8_t* in, size_t len);

// Build a 0x0004 vehicle announcement / identification response.
size_t doip_build_announcement(const DoipConfig& cfg, uint8_t* out, size_t cap);

// Build a 0x4002 entity status response.
size_t doip_build_entity_status(const DoipConfig& cfg, const DoipEntityState& st, uint8_t* out, size_t cap);

// Build a 0x4004 diagnostic power mode response.
size_t doip_build_power_mode(const DoipEntityState& st, uint8_t* out, size_t cap);

// --- UDS routing (diagnostic message payload) -------------------------------

// True only for UDS service IDs that carry a sub-function byte, i.e. where
// bit 7 of the byte after the SID is the suppressPosRspMsgIndicationBit.
bool doip_uds_has_subfunction(uint8_t sid);

// How a decoded diagnostic message must be forwarded onto CAN.
struct UdsRoute {
  bool functional;       // target == functional_address
  bool expect_response;  // false when the SPR bit is set on a sub-function service
};

// Classify the routing of a UDS payload addressed to `target`.
UdsRoute doip_classify_uds(const DoipConfig& cfg, uint16_t target, const uint8_t* uds, size_t len);

#endif  // DOIP_CODEC_H
