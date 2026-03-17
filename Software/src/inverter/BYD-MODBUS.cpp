#include "BYD-MODBUS.h"
#include "../battery/BATTERIES.h"
#include "../datalayer/datalayer.h"
#include "../devboard/hal/hal.h"
#include "../devboard/utils/events.h"

// For modbus register definitions, see https://gitlab.com/pelle8/inverter_resources/-/blob/main/byd_registers_modbus_rtu.md

void BydModbusInverter::update_values() {
  verify_temperature();
  verify_inverter_modbus();
  handle_update_data_modbusp201_byd();
  handle_update_data_modbusp301_byd();
}

void BydModbusInverter::handle_static_data() {
  // Store the data into the array
  // Storage interface_id SI
  static const uint16_t si_data[] = {0x5349, 0x0001};
  // Manufacturer: BYD
  static const uint16_t byd_data[] = {0x4259, 0x4400, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  // Battery Model: "BYD Battery-Box Premium HV"
  static const uint16_t battery_data[] = {0x4259, 0x4420, 0x4261, 0x7474, 0x6572, 0x792D, 0x426F, 0x7820,
                                          0x5072, 0x656D, 0x6975, 0x6D20, 0x4856,   0x00,   0x00,   0x00};
  // Hardware and firmware version (5.0 and 3.24)
  static const uint16_t fw_ver_data[] = {0x352E, 0x3000, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x332E, 0x3236, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  // Battery serial "P030T020Z2308081297     "
  static const uint16_t serial_data[] = {0x5030, 0x3330, 0x5430, 0x3230, 0x5A32, 0x3330, 0x3830, 0x3831,
                                         0x3239, 0x3700,   0x00,   0x00,   0x00,   0x00,   0x00,   0x00};
  // Protocol version
  static const uint16_t protocol_version_data[] = {0x0001, 0x00};

  static const uint16_t* const data_array_pointers[] = {si_data, byd_data, battery_data, fw_ver_data, serial_data,
                                                        protocol_version_data};
  static const size_t data_sizes[] = {sizeof(si_data),    sizeof(byd_data),    sizeof(battery_data),
                                      sizeof(fw_ver_data),  sizeof(serial_data), sizeof(protocol_version_data)};

  static const uint16_t init_p201[] = {0x00, 0x00, 0x00, MAX_POWER, MAX_POWER, 0x00, 0x00,
                                       0xD000, 0x000A, 0xD000, 0x000A, 0x00,      0x00};

  static const uint16_t init_p301[] = {0x00, 0x00, 0x80, 0x00,   0x00, 0x00, 0x00,   0x00, 0x00,   0x07D0, 0x00,   0x07D0,
                                       0x4B, 0x5F, 0x00, 0x00,   0x10, 0x58D5, 0x00, 0x00, 0x0D,   0xCB60, 0xE6,   0x26AC};

  uint16_t i = 100;
  for (uint8_t arr_idx = 0; arr_idx < sizeof(data_array_pointers) / sizeof(uint16_t*); arr_idx++) {
    uint16_t data_size = data_sizes[arr_idx];
    for (int j = 0; j < data_size / sizeof(uint16_t); j++) {
      mbPV[i + j] = data_array_pointers[arr_idx][j];
    }
    i += data_size / sizeof(uint16_t);
  }

  for (int i = 0; i < sizeof(init_p201) / sizeof(uint16_t); i++) {
    mbPV[200 + i] = init_p201[i];
  }

  for (int i = 0; i < sizeof(init_p301) / sizeof(uint16_t); i++) {
    mbPV[300 + i] = init_p301[i];
  }
}

void BydModbusInverter::handle_update_data_modbusp201_byd() {
  if (battery2) {
    mbPV[202] = std::min(datalayer.battery.info.total_capacity_Wh + datalayer.battery2.info.total_capacity_Wh,
                         static_cast<uint32_t>(57960u));  //Cap to 58kWh
  } else {
    mbPV[202] = std::min(datalayer.battery.info.total_capacity_Wh, static_cast<uint32_t>(57960u));  //Cap to 58kWh
  }
  mbPV[205] =
      std::min(datalayer.battery.info.max_design_voltage_dV,
               static_cast<uint16_t>(4500u));  // Max Voltage, if higher Gen24 forces discharge, cap to 450.0V for Primo
  mbPV[206] = (datalayer.battery.info.min_design_voltage_dV);  // Min Voltage, if lower Gen24 disables battery
}

void BydModbusInverter::handle_update_data_modbusp301_byd() {
  if (datalayer.battery.status.reported_current_dA == 0) {
    bms_char_dis_status = STANDBY;
  } else if (datalayer.battery.status.reported_current_dA < 0) {  //Negative value = Discharging
    bms_char_dis_status = DISCHARGING;
  } else {  //Positive value = Charging
    bms_char_dis_status = CHARGING;
  }
  // Convert max discharge Amp value to max Watt
  user_configured_max_discharge_W =
      ((datalayer.battery.settings.max_user_set_discharge_dA * datalayer.battery.info.max_design_voltage_dV) / 100);
  // Use the smaller value, battery reported value OR user configured value
  max_discharge_W = std::min(datalayer.battery.status.max_discharge_power_W, user_configured_max_discharge_W);

  // Convert max charge Amp value to max Watt
  user_configured_max_charge_W =
      ((datalayer.battery.settings.max_user_set_charge_dA * datalayer.battery.info.max_design_voltage_dV) / 100);
  // Use the smaller value, battery reported value OR user configured value
  max_charge_W = std::min(datalayer.battery.status.max_charge_power_W, user_configured_max_charge_W);

  if (datalayer.battery.status.bms_status == ACTIVE) {
    mbPV[308] = datalayer.battery.status.voltage_dV;
  } else {
    mbPV[308] = 0;
  }
  mbPV[300] = datalayer.battery.status.bms_status;
  mbPV[302] = 128 + bms_char_dis_status;
  if (datalayer.battery.status.reported_soc < 100) {
    mbPV[303] = 100;  //Force SOC to never go below 1% to avoid overdischarge
  } else {
    mbPV[303] = datalayer.battery.status.reported_soc;
  }
  if (battery2) {
    mbPV[304] = std::min(datalayer.battery.info.total_capacity_Wh + datalayer.battery2.info.total_capacity_Wh,
                         static_cast<uint32_t>(57960u));  //Cap to 58kWh
  } else {
    mbPV[304] = std::min(datalayer.battery.info.total_capacity_Wh, static_cast<uint32_t>(57960u));  //Cap to 58kWh
  }
  if (battery2) {
    mbPV[305] = std::min(datalayer.battery.status.reported_remaining_capacity_Wh +
                             datalayer.battery2.status.reported_remaining_capacity_Wh,
                         static_cast<uint32_t>(57960u));  //Cap to 58kWh
  } else {
    mbPV[305] = std::min(datalayer.battery.status.reported_remaining_capacity_Wh,
                         static_cast<uint32_t>(57960u));  //Cap to 58kWh
  }
  mbPV[306] = std::min(max_discharge_W, static_cast<uint32_t>(30000u));  //Cap to 30000 if exceeding
  mbPV[307] = std::min(max_charge_W, static_cast<uint32_t>(30000u));     //Cap to 30000 if exceeding
  mbPV[310] = datalayer.battery.status.voltage_dV;
  mbPV[312] = datalayer.battery.status.temperature_min_dC;
  mbPV[313] = datalayer.battery.status.temperature_max_dC;
  mbPV[323] = datalayer.battery.status.soh_pptt;
}

void BydModbusInverter::verify_temperature() {
  if (datalayer.battery.info.chemistry == battery_chemistry_enum::LFP) {
    return;  // Skip the following section
  }
  // This section checks if the battery temperature is negative, and incase it falls between -9.0 and -20.0C degrees
  // The Fronius Gen24 (and other Fronius inverters also affected), will stop charge/discharge if the battery gets colder than -10°C.
  // This is due to the original battery pack (BYD HVM), is a lithium iron phosphate battery, that cannot be charged in cold weather.
  // When using EV packs with NCM/LMO/NCA chemsitry, this is not a problem, since these chemistries are OK for outdoor cold use.
  if (datalayer.battery.status.temperature_min_dC < 0) {
    if (datalayer.battery.status.temperature_min_dC < -90 &&
        datalayer.battery.status.temperature_min_dC > -200) {  // Between -9.0 and -20.0C degrees
      datalayer.battery.status.temperature_min_dC = -90;       //Cap value to -9.0C
    }
  }
  if (datalayer.battery.status.temperature_max_dC < 0) {  // Signed value on negative side
    if (datalayer.battery.status.temperature_max_dC < -90 &&
        datalayer.battery.status.temperature_max_dC > -200) {  // Between -9.0 and -20.0C degrees
      datalayer.battery.status.temperature_max_dC = -90;       //Cap value to -9.0C
    }
  }
}

void BydModbusInverter::verify_inverter_modbus() {
  // Every 60 seconds, the Gen24 writes to this 401 register, alternating between 00FF and FF00.
  // We sample the register every 60 seconds. Incase the value has not changed for 3 minutes, we raise an event
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis60s >= INTERVAL_60_S) {
    previousMillis60s = currentMillis;

    all_401_values_equal = true;
    for (int i = 0; i < HISTORY_LENGTH; ++i) {
      if (register_401_history[i] != mbPV[401]) {
        all_401_values_equal = false;
        break;
      }
    }

    if (all_401_values_equal) {
      set_event(EVENT_MODBUS_INVERTER_MISSING, 0);
    } else {
      clear_event(EVENT_MODBUS_INVERTER_MISSING);
    }

    // Update history
    register_401_history[history_index] = mbPV[401];
    history_index = (history_index + 1) % HISTORY_LENGTH;
  }
}

bool BydModbusInverter::setup(void) {  // Performs one time setup at startup over CAN bus
  // Init Static data to the RTU Modbus
  handle_static_data();

  // Init Serial2 connected to the RTU Modbus
  RTUutils::prepareHardwareSerial(Serial2);

  auto rx_pin = esp32hal->RS485_RX_PIN();
  auto tx_pin = esp32hal->RS485_TX_PIN();

  if (!esp32hal->alloc_pins(Name, rx_pin, tx_pin)) {
    return false;
  }

  Serial2.begin(9600, SERIAL_8N1, rx_pin, tx_pin);

  // Start ModbusRTU background task
  MBserver.begin(Serial2, esp32hal->MODBUS_CORE());

  return true;
}
