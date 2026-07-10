#ifndef COMM_DOIP_H
#define COMM_DOIP_H

// DoIP (ISO 13400) gateway glue: binds the transport-agnostic DoIP codec
// (lib/doip) to the TCP/UDP sockets (AsyncServer + AsyncUDP) and to a battery's
// UDS gateway interface. See lib/doip and Battery::uds_gateway_* for the pieces.

// Feature gate, loaded from NVM ("DOIPENABLED"). Mirrors mqtt_enabled.
extern bool doip_enabled;

// Start the DoIP server. Call from the connectivity task once WiFi is up.
// No-op if disabled or the active battery does not support the UDS gateway.
void init_doip();

// Pump the DoIP <-> battery bridge. Call every tick from the core/CAN task so
// all ISO-TP access stays on that task.
void doip_bridge_pump();

#endif  // COMM_DOIP_H
