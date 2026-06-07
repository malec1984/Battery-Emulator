#ifndef _MEB_HTML_H
#define _MEB_HTML_H

#include <string.h>
#include "../datalayer/datalayer.h"
#include "../datalayer/datalayer_extended.h"
#include "../devboard/webserver/BatteryHtmlRenderer.h"

class MebHtmlRenderer : public BatteryHtmlRenderer {
 public:
  // DTC description file fetched by the loader. Defaults to the MEB set; each
  // platform's battery setup() overrides this (e.g. MQB Evo -> vag_mqb_dtc.json).
  const char* dtc_json_filename = "vag_meb_dtc.json";

  String get_status_html() {
    String content;
    auto& m = datalayer_extended.meb;

    // Pre-size the buffer so the many += below are plain memcpy with no
    // reallocation. Without this, ESP32's String grows in 16-byte steps,
    // causing hundreds of realloc/copy/free cycles that churn the heap.
    size_t estimate = 3500;  // status rows + temperature blocks
    if (m.dtc_last_read_millis != 0 && !m.dtc_read_failed && m.dtc_count > 0)
      estimate += 4096 + m.dtc_count * 256;  // DTC table rows (class-based) + JS loader
    content.reserve(estimate);

    // Lookup tables live in flash; sparse slots use "?" so a single pick()
    // reproduces the original switch/default behaviour.
    static const char* const tbl_closed[] = {"Init", "Closed", "Open!", "Fault"};
    static const char* const tbl_mode[] = {"HV inactive", "HV active",      "Balancing", "Extern charging",
                                           "AC charging",  "Battery error", "DC charging", "Init"};
    static const char* const tbl_balancing[] = {"init", "active", "inactive"};
    static const char* const tbl_diag[] = {"Init", "Battery display", "?",     "?",
                                           "Battery display OK", "?", "Battery display check", "Fault"};
    static const char* const tbl_hvline[] = {"Init", "No open HV line detected", "Open HV line", "Fault"};
    static const char* const tbl_welded[] = {"Init", "No contactor welded", "At least 1 contactor welded",
                                             "Protection status detection error"};
    static const char* const tbl_warn[] = {"OK", "Not OK", "?", "?", "?", "?", "Init", "Fault"};
    static const char* const tbl_vfree[] = {"Init", "BMS interm circuit voltage free (U<20V)",
                                            "BMS interm circuit not voltage free (U >= 25V)", "Error"};
    static const char* const tbl_errstat[] = {"Component IO", "Iso Error 1",          "Iso Error 2", "Interlock",
                                              "SD",          "Performance red", "No component function", "Init"};

    kv(content, "Service disconnect switch", m.SDSW ? "Missing!" : "OK");
    kv(content, "Pilotline", m.pilotline ? "Open!" : "OK");
    kv(content, "Transportmode", m.transportmode ? "Locked!" : "OK");
    kv(content, "Shutdown", m.shutdown_active ? "Active!" : "No");
    kv(content, "Component protection", m.componentprotection ? "Active!" : "No");
    kv(content, "HVIL status", pick(tbl_closed, 4, m.HVIL));
    kv(content, "KL30C status", pick(tbl_closed, 4, m.BMS_Kl30c_Status));
    kv(content, "BMS mode", pick(tbl_mode, 8, m.BMS_mode));
    kv(content, "Charging", m.charging_active ? "active" : "not active");
    kv(content, "Balancing", pick(tbl_balancing, 3, m.balancing_active));
    kv(content, "Slow charging", m.balancing_request ? "requested" : "not requested");
    kv(content, "Diagnostic", pick(tbl_diag, 8, m.battery_diagnostic));
    kv(content, "HV line status",
       m.status_HV_line < 4 ? String(tbl_hvline[m.status_HV_line]) : ("? " + String(m.status_HV_line)));
    kv(content, "BMS fault performance", m.BMS_fault_performance ? "Active!" : "Off");
    kv(content, "BMS fault emergency shutdown crash", m.BMS_fault_emergency_shutdown_crash ? "Active!" : "Off");
    kv(content, "BMS error shutdown request", m.BMS_error_shutdown_request ? "Active!" : "Inactive");
    kv(content, "BMS error shutdown", m.BMS_error_shutdown ? "Active!" : "Off");
    kv(content, "Welded contactors", pick(tbl_welded, 4, m.BMS_welded_contactors_status));
    kv(content, "Warning support", pick(tbl_warn, 8, m.warning_support));
    kv(content, "Interm. Voltage (" + String(m.BMS_voltage_intermediate_dV / 10.0f, 1) + "V) status",
       pick(tbl_vfree, 4, m.BMS_status_voltage_free));
    kv(content, "BMS error status", pick(tbl_errstat, 8, m.BMS_error_status));
    kv(content, "BMS voltage", String(m.BMS_voltage_dV / 10.0f, 1));
    kv(content, "OBD MIL", m.BMS_OBD_MIL ? "ON!" : "Off");
    kv(content, "Red error lamp", m.BMS_error_lamp_req ? "ON!" : "Off");
    kv(content, "Yellow warning lamp", m.BMS_warning_lamp_req ? "ON!" : "Off");
    kv(content, "Isolation resistance", String(m.isolation_resistance) + " kOhm");
    kv(content, "Battery heating", m.battery_heating ? "Active!" : "Off");

    static const char* const rt_enum[] = {"No", "Error level 1", "Error level 2", "Error level 3"};
    struct RtRow {
      const char* label;
      uint8_t value;
    };
    const RtRow rt[] = {
        {"Overcurrent", m.rt_overcurrent},
        {"CAN fault", m.rt_CAN_fault},
        {"Overcharged", m.rt_overcharge},
        {"SOC too high", m.rt_SOC_high},
        {"SOC too low", m.rt_SOC_low},
        {"SOC jumping", m.rt_SOC_jumping},
        {"Temp difference", m.rt_temp_difference},
        {"Cell overtemp", m.rt_cell_overtemp},
        {"Cell undertemp", m.rt_cell_undertemp},
        {"Battery overvoltage", m.rt_battery_overvolt},
        {"Battery undervoltage", m.rt_battery_undervol},
        {"Cell overvoltage", m.rt_cell_overvolt},
        {"Cell undervoltage", m.rt_cell_undervol},
        {"Cell imbalance", m.rt_cell_imbalance},
        {"Battery unathorized", m.rt_battery_unathorized},
    };
    for (auto& r : rt)
      kv(content, r.label, rt_enum[r.value & 0x03]);

    content += "<h4>Battery temperature: ";
    if (m.battery_temperature_dC == 875) {  //Raw value 255
      content += "ERROR";
    } else if (m.battery_temperature_dC == 870) {  //Raw value 254
      content += "INIT";
    } else {
      content += String(m.battery_temperature_dC / 10.f, 1) + " &deg;C";
    }
    content += "</h4>";

    for (int i = 0; i < 3; i++) {
      content += "<h4>Temperature points " + String(i * 6 + 1) + "-" + String(i * 6 + 6) + " :";
      for (int j = 0; j < 6; j++)
        content += " &nbsp;" + String(m.temp_points[i * 6 + j], 1);
      content += " &deg;C</h4>";
    }
    bool temps_done = false;
    for (int i = 0; i < 7 && !temps_done; i++) {
      content += "<h4>Cell temperatures " + String(i * 8 + 1) + "-" + String(i * 8 + 8) + " :";
      for (int j = 0; j < 8; j++) {
        if (m.celltemperature_dC[i * 8 + j] == 865) {
          temps_done = true;
          break;
        } else {
          content += " &nbsp;" + String(m.celltemperature_dC[i * 8 + j] / 10.f, 1);
        }
      }
      content += " &deg;C</h4>";
    }
    kv(content, "Total charged", String(datalayer.battery.status.total_charged_battery_Wh / 1000.0, 1) + " kWh");
    kv(content, "Total discharged", String(datalayer.battery.status.total_discharged_battery_Wh / 1000.0, 1) + " kWh");

    // Diagnostic Trouble Codes Section
    content +=
        "<h3 style='color:#27b06c;border-bottom:2px solid #27b06c;padding-bottom:5px;'>🔧 Diagnostic Trouble "
        "Codes</h3>";
    content += "<div style='margin-left:15px;margin-right:15px;'>";

    if (datalayer_extended.meb.dtc_last_read_millis == 0) {
      // No DTC read has been performed yet
      content +=
          "<p style='color:#ff9800;'>ℹ DTCs have not been read yet. Click 'Read DTC' to scan for fault codes.</p>";
    } else if (datalayer_extended.meb.dtc_read_failed) {
      content += "<p style='color:#d32f2f;'>⚠ Last DTC read failed or not supported</p>";
    } else if (datalayer_extended.meb.dtc_count == 0) {
      content += "<p style='color:#4CAF50;'>✓ No DTCs present</p>";
    } else {
      content += "<p><strong>DTC Count:</strong> " + String(datalayer_extended.meb.dtc_count) + "</p>";

      // Convert last read time to days:hours:minutes:seconds format
      //unsigned long last_read_seconds = (millis() - datalayer_extended.meb.dtc_last_read_millis) / 1000;
      unsigned long last_read_seconds = datalayer_extended.meb.dtc_last_read_millis / 1000;
      unsigned long read_days = last_read_seconds / 86400;
      unsigned long read_hours = (last_read_seconds % 86400) / 3600;
      unsigned long read_minutes = (last_read_seconds % 3600) / 60;
      unsigned long read_seconds = last_read_seconds % 60;

      content += "<p><strong>Last Read:</strong> ";
      if (read_days > 0) {
        content += String(read_days) + "d ";
      }
      if (read_hours > 0 || read_days > 0) {
        content += String(read_hours) + "h ";
      }
      content += String(read_minutes) + "m " + String(read_seconds) + "s ago</p>";

      // Shared cell styles defined once, then referenced by class on every row.
      // Far cheaper than repeating the inline CSS on each <th>/<td>.
      content +=
          "<style>.dh,.dc{padding:12px 15px}.dh{text-align:left;font-weight:600}"
          ".dc{border-top:1px solid #e0e0e0}.dm{font-family:monospace;font-size:1.1em;font-weight:600}"
          ".dd{font-size:.95em;color:#ddd}.dse{font-weight:500}</style>";
      content += "<div style='overflow-x:auto;margin-top:10px;margin-bottom:15px;'>";
      content +=
          "<table style='width:auto;margin:0 auto;border-collapse:separate;border-spacing:0;border:1px solid "
          "#ddd;border-radius:8px;overflow:hidden;'>";

      content += "<thead>";
      content += "<tr style='background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);color:white;'>";
      content += "<th class='dh'>DTC Code</th>";
      content += "<th class='dh'>Status</th>";
      content += "<th class='dh'>Description</th>";
      content += "</tr>";
      content += "</thead>";

      content += "<tbody>";

      for (int i = 0; i < datalayer_extended.meb.dtc_count; i++) {
        uint32_t code = datalayer_extended.meb.dtc_codes[i];
        uint8_t status = datalayer_extended.meb.dtc_status[i];

        char dtcStr[12];
        sprintf(dtcStr, "%06lX", code);

        String statusStr = "Stored";
        String statusColor = "#757575";

        if (status & 0x08) {
          statusStr = "Confirmed";
          statusColor = "#ff6f00";
        }

        if (status & 0x01) {
          statusStr = "Active";
          statusColor = "#d32f2f";
        }

        content += "<tr>";
        content += "<td class='dc dm'>" + String(dtcStr) + "</td>";
        content += "<td class='dc dse' style='color:" + statusColor + "'>" + statusStr + "</td>";
        content += "<td class='dc dd' data-dtc-code='" + String(code) + "'>Unknown</td>";
        content += "</tr>";
      }

      content += "</tbody>";
      content += "</table>";
      content += "</div>";

      content += get_dtc_json_loader_html(GITHUB_RAW_BASE_URL, dtc_json_filename);
    }

    content += "</div>";
    return content;
  }

 private:
  // Emits "<h4>{label}: {value}</h4>". Keeping the markup here means "<h4>",
  // ": " and "</h4>" exist as a single flash literal each instead of being
  // baked into dozens of combined string constants.
  static void kv(String& c, const char* label, const char* value) {
    c += "<h4>";
    c += label;
    c += ": ";
    c += value;
    c += "</h4>";
  }
  static void kv(String& c, const String& label, const String& value) {
    c += "<h4>";
    c += label;
    c += ": ";
    c += value;
    c += "</h4>";
  }

  // Bounds-checked table lookup; out-of-range yields "?" like the old switch
  // default cases.
  static const char* pick(const char* const* tbl, uint8_t n, uint8_t v) { return v < n ? tbl[v] : "?"; }
};

#endif
