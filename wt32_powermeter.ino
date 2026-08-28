#include <WebServer_WT32_ETH01.h>
#include <WebServer_WT32_ETH01.hpp>

/****************************************************************************************************************************
  Remote Power/SWR Meter - a solution to remotely measure RF power and VSWR over ethernet

  For Ethernet shields using WT32_ETH01 (ESP32 + LAN8720)
  Uses WebServer_WT32_ETH01, a library for the Ethernet LAN8720 in WT32_ETH01 to run WebServer

  Original author: Michael Clemens, DK1MI
  Maintained by: DL3HC
  Licensed under GPLv3 license (see LICENSE.md)

  VU meter code was taken from https://github.com/tomnomnom/vumeter, credits go to Tom Hudson (https://github.com/tomnomnom)
  
 *****************************************************************************************************************************/


/****************************************************************************************************************************
 *  DEBUG / BUILD CONFIGURATION
 *
 *  The following preprocessor definitions control diagnostic output and system behavior:
 *
 *  DEBUG_ETHERNET_WEBSERVER_PORT
 *    - Defines the serial interface used for debug logging output.
 *    - In this case, Serial is used, meaning logs are printed over USB serial.
 *
 *  _ETHERNET_WEBSERVER_LOGLEVEL_
 *    - Controls verbosity of the Ethernet web server library.
 *    - Range: 0 (silent) to 4 (very verbose).
 *    - Current value 3 enables detailed runtime diagnostics useful for debugging network issues.
 *
 *  FORMAT_SPIFFS_IF_FAILED
 *    - If SPIFFS (flash filesystem) initialization fails, the filesystem will be reformatted automatically.
 ****************************************************************************************************************************/
#define DEBUG_ETHERNET_WEBSERVER_PORT Serial

// Debug Level from 0 to 4
#define _ETHERNET_WEBSERVER_LOGLEVEL_ 3

#define FORMAT_SPIFFS_IF_FAILED true


/****************************************************************************************************************************
 *  CORE LIBRARIES AND SYSTEM DEPENDENCIES
 *
 *  WebServer_WT32_ETH01:
 *    Provides HTTP server functionality optimized for WT32_ETH01 boards (ESP32 + LAN8720 Ethernet PHY).
 *
 *  javascript.h / dashboard_css.h / config_css.h / index.h:
 *    Embedded web assets (HTML, CSS, JavaScript) used to render the configuration UI and dashboard.
 *
 *  Preferences:
 *    ESP32 NVS (Non-Volatile Storage) abstraction used to store configuration data persistently.
 *
 *  FS.h / SPIFFS.h:
 *    File system interfaces for SPIFFS flash storage, used for serving and storing static files.
 *
 *  OneWire / DallasTemperature:
 *    Used for communicating with DS18B20 (or compatible) digital temperature sensors via OneWire bus.
 ****************************************************************************************************************************/
#include <WebServer_WT32_ETH01.h>
#include "javascript.h"
#include "dashboard_css.h"
#include "config_css.h"
#include "index.h"  // Main Web page header file
#include <Preferences.h>
#include "FS.h"
#include "SPIFFS.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <esp_task_wdt.h>


/****************************************************************************************************************************
 *  SOFTWARE VERSIONING
 *
 *  version:
 *    Human-readable firmware version string used for UI display, debugging, and update tracking.
 ****************************************************************************************************************************/
String version = "1.0.3";


/****************************************************************************************************************************
 *  NETWORK IDENTITY / RELIABILITY CONFIGURATION
 *
 *  device_hostname:
 *    Used for mDNS (reachable as "<device_hostname>.local" instead of the hardcoded static IP)
 *    and as the OTA update device name. Change this if running multiple units on one network.
 *
 *  WDT_TIMEOUT_SECONDS:
 *    Hardware/task watchdog timeout. If the main loop doesn't check in within this window
 *    (e.g. a hung network call), the device auto-reboots instead of staying wedged indefinitely --
 *    important for a meter that's mounted somewhere you can't easily walk over and power-cycle.
 ****************************************************************************************************************************/
String device_hostname = "powermeter";
const int WDT_TIMEOUT_SECONDS = 15;

/****************************************************************************************************************************
 *  OTA UPDATE PASSWORD
 *
 *  OTA_DEFAULT_PASSWORD:
 *    Factory-default OTA password. Every unit ships with this same value, so it must not be
 *    trusted as a real secret -- it exists only so ArduinoOTA has *some* password from the very
 *    first boot (an unauthenticated OTA listener is a firmware-flashing backdoor for anyone on
 *    the LAN). As long as the stored password still equals this default, the web UI refuses to
 *    serve the normal dashboard/config pages and instead forces the user to set a real password
 *    first (see ota_password_is_default() / build_ota_setup_page() / handleSETOTAPASS()).
 *
 *  ota_password:
 *    The password actually in effect, loaded from NVS at boot (falling back to the default and
 *    persisting it on first-ever boot). Kept in sync with NVS and with ArduinoOTA's own state.
 ****************************************************************************************************************************/
const char* OTA_DEFAULT_PASSWORD = "changeme";
String ota_password = "";


/****************************************************************************************************************************
 *  PERSISTENT CONFIGURATION HANDLING
 *
 *  Preferences config / global_config:
 *    Interfaces to ESP32 NVS storage.
 *    Used for storing user settings such as power thresholds, display options,
 *    antenna configuration, and calibration constants.
 ****************************************************************************************************************************/
Preferences config;
Preferences global_config;


/****************************************************************************************************************************
 *  CONFIGURATION SCHEMA DEFINITION (BAND CONFIGURATION SYSTEM)
 *
 *  These arrays define the configuration keys, default values, and human-readable labels
 *  used in the web interface and internal configuration handling.
 *
 *  band_config_items:
 *    Internal keys used in NVS storage and program logic.
 *
 *  band_config_defaults:
 *    Default values applied if no stored configuration exists.
 *    Stored as strings to simplify persistence layer handling.
 *
 *  band_config_nice_names:
 *    User-facing labels shown in configuration UI.
 *    Each entry corresponds positionally to band_config_items.
 ****************************************************************************************************************************/
String band_config_items[] = { "b_show_mV", "b_show_dBm", "b_show_watt", "s_vswr_thresh", "b_vswr_beep", "s_ant_name", "s_max_led_pwr_f", "s_max_led_pwr_r", "s_max_led_vswr", "b_show_led_fwd", "b_show_led_ref", "b_show_led_vswr", "s_cable_loss", "b_power_avg", "b_show_temp", "b_celsius" };
String band_config_defaults[] = { "true", "true", "true", "2", "true", " ", "100", "100", "3", "true", "true", "true", "0", "false", "false", "true" };
String band_config_nice_names[] = { "Show voltage in mV (yes/no)", "Show power level in dBm (yes/no)", "Show power in Watt (yes/no)", "VSWR threshold that triggers a warning (e.g. 3)", "Beep if VSWR threshold is exceeded (yes/no)", "Name of the antenna", "Max. FWD power displayed by LED bar graph in W (e.g. 100)", "Max. REF power displayed by LED bar graph in W (e.g. 100)", "Max. VSWR displayed by LED bar graph (e.g. 3)", "Show LED graph for FWD power (yes/no)", "Show LED graph for REF power (yes/no)", "Show LED graph for VSWR (yes/no)","Cable loss in db (e.g. 3)","Display average power instead of PEP? (yes=AVG/no=PEP)", "Display Temperature? (yes/no)", "Display Temperature in C or F? (yes=C/no=F)" };


/****************************************************************************************************************************
 *  CALIBRATION TABLE STORAGE (mV -> dBm)
 *
 *  CalPoint:
 *    One calibration point: a millivolt reading (mv) and the RF power level (dbm)
 *    it corresponds to, as configured by the user in the calibration UI.
 *
 *  fwd_points / ref_points:
 *    Sparse, sorted-by-mv arrays holding only the calibration points the user has
 *    actually configured (typically a few dozen), instead of one slot per possible
 *    millivolt value. fwd_point_count / ref_point_count track how many entries in
 *    each array are valid. Kept sorted ascending by mv so millivolt_to_dbm() can
 *    find the bracketing pair with a single forward scan.
 *
 *  MAX_CAL_POINTS:
 *    Upper bound on configured calibration points per table. Far above any
 *    realistic calibration curve; extra entries beyond this are ignored on load.
 ****************************************************************************************************************************/
struct CalPoint {
  int16_t mv;
  double dbm;
};

const int MAX_CAL_POINTS = 200;
CalPoint fwd_points[MAX_CAL_POINTS];
CalPoint ref_points[MAX_CAL_POINTS];
int fwd_point_count = 0;
int ref_point_count = 0;


/****************************************************************************************************************************
 *  ANALOG / DERIVED MEASUREMENT VARIABLES
 *
 *  voltage_fwd / voltage_ref:
 *    Raw ADC readings corresponding to forward and reflected RF detector outputs.
 *
 *  fwd_dbm / ref_dbm:
 *    Computed power levels in dBm derived from voltage measurements.
 *
 *  fwd_watt / ref_watt:
 *    Converted absolute RF power values in watts.
 *
 *  iii:
 *    Generic index/counter variable, typically used for buffer iteration or sampling loops.
 ****************************************************************************************************************************/
int voltage_fwd, voltage_ref;
double fwd_dbm = 0, ref_dbm = 0;
double fwd_watt = 0, ref_watt = 0;
byte iii = 0;


/****************************************************************************************************************************
 *  CONFIGURATION RENDERING / WEB INTERFACE STATE
 *
 *  conf_content:
 *    Holds dynamically generated configuration content.
 *
 *  conf_textareas:
 *    Accumulates HTML textarea elements for configuration UI rendering.
 *
 *  conf_config_table:
 *    Stores HTML table representation of configuration entries.
 ****************************************************************************************************************************/
String conf_content;
String conf_textareas = "";
String conf_config_table = "";


/****************************************************************************************************************************
 *  BAND / FREQUENCY DOMAIN CONFIGURATION
 *
 *  band:
 *    Active frequency band identifier (e.g., "70cm").
 *
 *  default_band:
 *    Fallback band used at startup if no selection is stored.
 *
 *  band_fwd / band_ref:
 *    Derived keys used for storing forward/reflected values per band.
 *
 *  band_list:
 *    Supported amateur radio bands for selection in UI and logic partitioning.
 ****************************************************************************************************************************/
String band = "";
String default_band = "70cm";
String band_fwd = band + "_fwd";
String band_ref = band + "_ref";
String band_list[] = { "1.25cm", "3cm", "6cm", "9cm", "13cm", "23cm", "70cm", "2m", "HF" };


/****************************************************************************************************************************
 *  GPIO ASSIGNMENTS (HARDWARE INTERFACE)
 *
 *  IO2_FWD:
 *    Analog input pin used for forward power detector signal.
 *
 *  IO4_REF:
 *    Analog input pin used for reflected power detector signal.
 *
 *  IO14_TEMP:
 *    OneWire bus pin used for external temperature sensor communication.
 ****************************************************************************************************************************/
int IO2_FWD = 2;
int IO4_REF = 4;

const int IO14_TEMP = 14;


/****************************************************************************************************************************
 *  TEMPERATURE SENSOR SUBSYSTEM (ONEWIRE + DALLAS TEMPERATURE)
 *
 *  OneWire:
 *    Low-level communication protocol instance bound to IO14_TEMP.
 *
 *  DallasTemperature:
 *    High-level driver for DS18B20-class temperature sensors.
 *    Provides calibrated temperature readings in °C by default.
 *
 *  sensors:
 *    Global sensor controller object used to query temperature values
 *    and manage sensor discovery on the OneWire bus.
 ****************************************************************************************************************************/
OneWire oneWire(IO14_TEMP);
DallasTemperature sensors(&oneWire);

/****************************************************************************************************************************
 *  WEB SERVER INITIALIZATION
 *
 *  WebServer server(80):
 *    Instantiates an HTTP server object listening on TCP port 80 (standard HTTP).
 *    This server is responsible for handling incoming client requests from browsers
 *    or other HTTP clients in the local network.
 *
 *    In this embedded context (ESP32-based WT32_ETH01), the server typically serves:
 *      - Dashboard HTML pages
 *      - Configuration UI
 *      - REST-like endpoints for measurement data (RF power, SWR, temperature)
 *
 *    The server object internally manages request routing, handler registration,
 *    and response transmission over the Ethernet stack.
 ****************************************************************************************************************************/
WebServer server(80);


/****************************************************************************************************************************
 *  STATIC NETWORK CONFIGURATION
 *
 *  These IP parameters define a fixed (non-DHCP) network configuration.
 *
 *  myIP:
 *    Static IPv4 address assigned to the device on the local LAN.
 *    Must be unique within the subnet to avoid IP conflicts.
 *
 *  myGW (Gateway):
 *    Default gateway IP address, typically the router.
 *    Used for routing traffic outside the local subnet.
 *
 *  mySN (Subnet Mask):
 *    Defines the network range. 255.255.255.0 indicates a /24 subnet,
 *    allowing 254 usable host addresses in the 192.168.2.x range.
 *
 *  myDNS:
 *    DNS server used for domain name resolution.
 *    In this configuration it matches the gateway router.
 ****************************************************************************************************************************/
IPAddress myIP(192, 168, 2, 198);
IPAddress myGW(192, 168, 2, 1);
IPAddress mySN(255, 255, 255, 0);
IPAddress myDNS(192, 168, 2, 1);


/****************************************************************************************************************************
 *  FILE SYSTEM ACCESS: SPIFFS READ OPERATION
 *
 *  Function: readFile(fs::FS &fs, const char *path)
 *
 *  Purpose:
 *    Reads the full content of a file stored in SPIFFS (SPI Flash File System)
 *    and returns it as a single Arduino String object.
 *
 *  Parameters:
 *    fs:
 *      Reference to a filesystem object (typically SPIFFS).
 *
 *    path:
 *      Absolute path of the file inside SPIFFS (e.g. "/index.html").
 *
 *  Operation:
 *    1. Opens the file in read mode.
 *    2. Validates that the file exists and is not a directory.
 *    3. Iteratively reads byte-by-byte until EOF (file.available() == false).
 *    4. Appends each byte to a String buffer.
 *    5. Closes the file handle.
 *    6. Returns the full file content as a string.
 *
 *  Error Handling:
 *    - If the file cannot be opened or is a directory, an empty string is returned.
 *    - Diagnostic messages are printed to Serial for debugging.
 *
 *  Performance Consideration:
 *    - String concatenation in a loop can cause heap fragmentation on ESP32
 *      for large files; acceptable for small HTML/CSS/JS assets typically used here.
 ****************************************************************************************************************************/
String readFile(fs::FS &fs, const char *path) {
  Serial.printf("Reading file: %s\r\n", path);

  File file = fs.open(path);
  if (!file || file.isDirectory()) {
    Serial.println("failed to open file for reading");
    return "";
  }
  String ret = "";
  Serial.println("read from file:");
  while (file.available()) {
    ret += char(file.read());
  }
  file.close();
  return ret;
}


/****************************************************************************************************************************
 *  FILE SYSTEM ACCESS: SPIFFS WRITE OPERATION
 *
 *  Function: writeFile(fs::FS &fs, const char *path, const char *message)
 *
 *  Purpose:
 *    Writes a null-terminated character buffer into a file stored in SPIFFS.
 *    Used for persisting configuration data, logs, or small structured text files.
 *
 *  Parameters:
 *    fs:
 *      Reference to filesystem object (typically SPIFFS).
 *
 *    path:
 *      Destination file path inside SPIFFS (e.g. "/config.json").
 *
 *    message:
 *      Null-terminated string containing the data to be written.
 *
 *  Operation:
 *    1. Opens (or creates) the file in FILE_WRITE mode.
 *    2. Validates successful file handle acquisition.
 *    3. Writes the full message using file.print().
 *    4. Reports success or failure via Serial output.
 *    5. Closes the file to ensure data integrity and flush buffers.
 *
 *  Error Handling:
 *    - If file opening fails, function exits early and prints an error.
 *    - Write success is verified via return value of file.print().
 *
 *  Data Integrity Considerations:
 *    - Closing the file is essential to ensure flash commit on SPIFFS.
 *    - Frequent writes may contribute to flash wear over long-term usage.
 ****************************************************************************************************************************/
void writeFile(fs::FS &fs, const char *path, const char *message) {
  Serial.printf("Writing file: %s\r\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    Serial.println("file written");
  } else {
    Serial.println("file write failed");
  }
  file.close();
}

/**************************************************************************************************
 * Function: dbm_to_watt
 *
 * Purpose:
 * Converts a power level expressed in dBm into linear power in Watts.
 *
 * Theory:
 * dBm is a logarithmic power unit referenced to 1 milliwatt.
 * Conversion follows the standard relationship:
 * P(W) = 10^((dBm - 30) / 10)
 *
 * Parameters:
 * dbm (double)
 *   Input power level in dBm.
 *
 * Returns:
 * double
 *   Equivalent power in Watts.
 *
 * Notes:
 * - Uses base-10 exponentiation via pow().
 * - Assumes valid dBm input; no bounds checking is performed.
 **************************************************************************************************/
double dbm_to_watt(double dbm) {
  return pow(10.0, (dbm - 30.0) / 10.0);
}


/**************************************************************************************************
 * Function: is_val_out_of_bounds
 *
 * Purpose:
 * Determines whether a given millivolt (ADC) value lies outside the calibrated range
 * defined by the lowest and highest configured calibration points.
 *
 * Behavior:
 * - Looks at the selected sorted-by-mv point table (fwd_points or ref_points).
 * - Since it's sorted, the lowest/highest configured mv are just the first/last entries.
 *
 * Parameters:
 * mv (int)
 *   Measured millivolt value.
 *
 * fwd (bool)
 *   Selects which dataset to use:
 *   true  -> forward power table (fwd_points)
 *   false -> reflected power table (ref_points)
 *
 * Returns:
 * bool
 *   false -> value is within calibrated bounds
 *   true  -> value is outside calibrated bounds (or no calibration points exist)
 **************************************************************************************************/
bool is_val_out_of_bounds(int mv, bool fwd) {
  CalPoint *pts = fwd ? fwd_points : ref_points;
  int count = fwd ? fwd_point_count : ref_point_count;

  // no calibration data at all -> everything is "out of bounds"
  if (count == 0) {
    return true;
  }

  // pts[] is kept sorted ascending by mv, so the lowest/highest configured
  // voltages are simply the first and last entries -- no scan needed.
  return mv < pts[0].mv || mv > pts[count - 1].mv;
}


/**************************************************************************************************
 * Function: millivolt_to_dbm
 *
 * Purpose:
 * Converts a raw millivolt (ADC) value into a corresponding dBm value using linear
 * interpolation between the two nearest configured calibration points.
 *
 * Core concept:
 * Calibration data is stored as a sorted-by-mv array of (mv, dbm) points
 * (fwd_points / ref_points) holding only the points the user actually configured.
 *
 * Parameters:
 * mv (int)
 *   Input millivolt value to convert.
 *
 * fwd (bool)
 *   Selects dataset: true -> forward power table (fwd_points), false -> reflected (ref_points).
 *
 * Returns:
 * double
 *   Interpolated dBm value. Clamped to the nearest real calibration point if mv falls
 *   outside the calibrated range; 0 if the table has no calibration points at all.
 *
 * Notes:
 * - pts[] is kept sorted ascending by mv, so a single forward scan finds the bracket.
 * - "ascending" here means the calibration dBm values increase as mv increases; some
 *   detectors have an inverse relationship, so both directions are handled.
 * - Performance is O(n) in the number of *configured* calibration points (typically a
 *   few dozen), not O(3300) like the previous dense-array implementation.
 **************************************************************************************************/
double millivolt_to_dbm(int mv, bool fwd) {
  CalPoint *pts = fwd ? fwd_points : ref_points;
  int count = fwd ? fwd_point_count : ref_point_count;

  if (count == 0) {
    return 0;
  }
  if (count == 1) {
    return pts[0].dbm;
  }

  // Direction of the calibration curve: does dBm increase or decrease as mV increases?
  bool ascending = pts[count - 1].dbm >= pts[0].dbm;

  // Find the bracketing pair: last point with mv < input, next point with mv >= input.
  bool have_lastval = false;
  bool have_nextval = false;
  int lastkey = 0, nextkey = 0;
  double lastval = 0, nextval = 0;

  for (int i = 0; i < count; i++) {
    if (pts[i].mv < mv) {
      lastkey = pts[i].mv;
      lastval = pts[i].dbm;
      have_lastval = true;
    } else {
      nextkey = pts[i].mv;
      nextval = pts[i].dbm;
      have_nextval = true;
      break;
    }
  }

  // mv fell outside the calibrated range, or only one side of the
  // interpolation pair was found: clamp to the nearest real calibration
  // point instead of interpolating against a fabricated endpoint, which
  // would otherwise divide by zero below.
  if (!have_lastval) {
    return nextval;
  }
  if (!have_nextval) {
    return lastval;
  }
  if (lastkey == nextkey) {
    return lastval;
  }

  // Linear interpolation between the two nearest real calibration points
  // (lastkey/lastval and nextkey/nextval), normalized so the math works
  // regardless of whether the table is stored ascending or descending.
  double lowerkey = min(lastkey, nextkey);
  double higherkey = max(lastkey, nextkey);

  double lowerval = min(lastval, nextval);
  double higherval = max(lastval, nextval);

  double diffkey = higherkey - lowerkey;
  double diffval = max(lastval, nextval) - min(lastval, nextval);

  double result = 0;

  if (ascending) {
    result = lowerval + ((diffval / diffkey) * (mv - lowerkey));
  } else {
    result = higherval - ((diffval / diffkey) * (mv - lowerkey));
  }

  return result;
}


/****************************************************************************************************************************
 *  DIRECTIONAL COUPLER SAMPLING AND SIGNAL PROCESSING
 *
 *  This function acquires raw ADC measurements from two RF detector channels:
 *    - Forward power (FWD)
 *    - Reflected power (REF)
 *
 *  It performs oversampling, optional averaging, calibration mapping,
 *  and power correction (cable loss compensation).
 *
 *  HARDWARE CONTEXT:
 *    IO2_FWD -> ADC input for forward RF detector
 *    IO4_REF -> ADC input for reflected RF detector
 *
 *  SAMPLING STRATEGY:
 *    The function performs 50 consecutive ADC measurements per channel.
 *    During acquisition, it computes:
 *      - Cumulative sum (for averaging)
 *      - Maximum observed value (for peak detection)
 *
 *  SIGNAL PROCESSING MODES:
 *    Controlled by persistent configuration key:
 *      "s_power_avg"
 *
 *    If "true":
 *      - Uses arithmetic mean of 50 samples
 *      - Reduces noise sensitivity
 *
 *    If "false":
 *      - Uses maximum sampled value
 *      - Emphasizes peak envelope detection (PEP-like behavior)
 *
 *  OUTPUT VARIABLES:
 *    voltage_fwd -> processed forward detector voltage (mV)
 *    voltage_ref -> processed reflected detector voltage (mV)
 *
 *  CALIBRATION STEP:
 *    Raw millivolt values are converted into dBm using:
 *      millivolt_to_dbm()
 *
 *    Separate calibration is applied for forward and reflected paths,
 *    reflecting directional coupler asymmetry.
 *
 *  CABLE LOSS COMPENSATION:
 *    A configurable attenuation factor (in dB) is retrieved from NVS:
 *      "s_cable_loss"
 *
 *    Correction model:
 *      FWD path: fwd_dbm = fwd_dbm - cable_loss
 *      REF path: ref_dbm = ref_dbm + cable_loss
 *
 *    This reflects the physical assumption that:
 *      - Forward power is reduced by feedline loss
 *      - Reflected power appears higher when referenced at detector point
 *
 *  FINAL CONVERSION:
 *    Both corrected dBm values are converted into linear power units:
 *      fwd_watt = dbm_to_watt(fwd_dbm)
 *      ref_watt = dbm_to_watt(ref_dbm)
 *
 *  NOTES:
 *    - Sampling count (50) is a fixed trade-off between responsiveness and noise suppression.
 *    - Uses ESP32-specific analogReadMilliVolts() for absolute millivolt scaling.
 ****************************************************************************************************************************/
void read_directional_couplers() {
  int voltage_sum_fwd = 0;
  int voltage_sum_ref = 0;

  int voltage_fwd_max = 0;
  int voltage_ref_max = 0;
  int voltage_fwd_now = 0;
  int voltage_ref_now = 0;

  // Takes 50 samples and sums them up
  // figure out the highest values
  for (iii = 0; iii < 50; iii++) {
    voltage_fwd_now = analogReadMilliVolts(IO2_FWD);
    voltage_ref_now = analogReadMilliVolts(IO4_REF);
    voltage_sum_fwd += voltage_fwd_now;
    voltage_sum_ref += voltage_ref_now;
    voltage_fwd_max = max(voltage_fwd_now, voltage_fwd_max);
    voltage_ref_max = max(voltage_ref_now, voltage_ref_max);
    // small settling delay so consecutive ADC samples aren't correlated
    delayMicroseconds(100);
  }

  int voltage_fwd_raw = 0;
  int voltage_ref_raw = 0;
  if (config.getString(String("s_power_avg").c_str()) == "true") {
    // calculate the average value by deviding the above sum by 50
    voltage_fwd_raw = voltage_sum_fwd / 50;
    voltage_ref_raw = voltage_sum_ref / 50;
  } else {
    // take the highest value of the 50 samples
    voltage_fwd_raw = voltage_fwd_max;
    voltage_ref_raw = voltage_ref_max;
  }

  // Exponential moving average across polls: smooths out ADC/detector jitter
  // that would otherwise get amplified by steep calibration segments and
  // show up as jumpy bars, without needing more calibration points.
  static double voltage_fwd_ema = -1;
  static double voltage_ref_ema = -1;
  const double ema_alpha = 0.3;
  if (voltage_fwd_ema < 0) {
    voltage_fwd_ema = voltage_fwd_raw;
    voltage_ref_ema = voltage_ref_raw;
  } else {
    voltage_fwd_ema = ema_alpha * voltage_fwd_raw + (1 - ema_alpha) * voltage_fwd_ema;
    voltage_ref_ema = ema_alpha * voltage_ref_raw + (1 - ema_alpha) * voltage_ref_ema;
  }
  voltage_fwd = (int)round(voltage_fwd_ema);
  voltage_ref = (int)round(voltage_ref_ema);

  // calculate the dBm value from the voltage based on the calibration table
  fwd_dbm = millivolt_to_dbm(voltage_fwd, true);
  ref_dbm = millivolt_to_dbm(voltage_ref, false);

  // add cable loss to FWD dBm, substract cable loss from REF dBm
  double cable_loss = 0;
  cable_loss = config.getString(String("s_cable_loss").c_str()).toDouble();
  //Serial.println("cable loss: " + String(cable_loss));
  fwd_dbm = fwd_dbm - cable_loss;
  ref_dbm = ref_dbm + cable_loss;

  // calculate watt from dBm
  fwd_watt = dbm_to_watt(fwd_dbm);
  ref_watt = dbm_to_watt(ref_dbm);
}

/****************************************************************************************************************************
 *  HTTP HANDLER: handleRoot
 *
 *  PURPOSE
 *  This function serves the main dashboard web page to the client when the root URI ("/") is requested.
 *
 *  BEHAVIOR
 *  - Retrieves three separate embedded web assets:
 *      MAIN_page       → HTML structure of the dashboard UI
 *      DB_STYLESHEET   → CSS styling for layout and visual appearance
 *      JAVASCRIPT      → Client-side logic for dynamic updates and periodic polling
 *
 *  - Concatenates CSS + JavaScript + HTML into a single HTTP response payload.
 *    This implies the system uses a single-response page assembly approach rather than
 *    serving static files individually.
 *
 *  - Sends HTTP 200 OK response with MIME type "text/html" to the connected client.
 *
 *  DESIGN NOTES
 *  - This approach reduces HTTP request overhead on constrained embedded systems (ESP32).
 *  - It trades modular frontend delivery for simplicity and lower client round-trip complexity.
 ****************************************************************************************************************************/
/****************************************************************************************************************************
 *  OTA PASSWORD SETUP GATE
 *
 *  ota_password_is_default():
 *    True as long as the device is still using the factory-default OTA password. Every unit
 *    ships with the same default, so leaving it in place means anyone on the LAN can push
 *    firmware to the device via ArduinoOTA -- this is checked before serving the normal
 *    dashboard/config pages to force a real password to be set on first LAN use.
 *
 *  build_ota_setup_page():
 *    Renders a minimal, self-contained "set your OTA password" form. Deliberately independent
 *    of the dashboard/config stylesheets (which assume their own page structure) so this page
 *    works correctly even on a completely fresh device.
 ****************************************************************************************************************************/
bool ota_password_is_default() {
  return ota_password == String(OTA_DEFAULT_PASSWORD);
}

String build_ota_setup_page(String error_msg) {
  String html = "<!DOCTYPE html><html><meta charset=\"utf-8\">";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  html += "<title>Set OTA Password</title><style>";
  html += "body{background:#14181B;color:#E8E6E1;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Helvetica,Arial,sans-serif;";
  html += "display:flex;justify-content:center;padding:32px 16px;}";
  html += ".card{max-width:420px;width:100%;background:#1B2024;border:1px solid #2E363B;border-top:3px solid #D97C4A;";
  html += "border-radius:6px;padding:20px;}";
  html += "h1{font-size:16px;margin:0 0 12px 0;}";
  html += "p{font-size:13px;color:#8A9096;line-height:1.5;}";
  html += "label{display:block;font-size:12px;font-weight:700;letter-spacing:.04em;text-transform:uppercase;margin:14px 0 4px;}";
  html += "input{width:100%;box-sizing:border-box;background:#14181B;color:#E8E6E1;border:1px solid #2E363B;border-radius:4px;padding:8px;font-size:14px;}";
  html += "button{margin-top:16px;width:100%;background:#D97C4A;color:#14181B;border:none;border-radius:6px;padding:10px;font-size:13px;font-weight:700;cursor:pointer;}";
  html += ".error{color:#FF5A45;font-size:13px;margin:12px 0 0;}";
  html += "</style><div class=\"card\">";
  html += "<h1>Set an OTA update password</h1>";
  html += "<p>This device still uses the factory-default password for over-the-air firmware updates. "
          "Since every unit ships with the same default, leaving it in place lets anyone on this network "
          "flash new firmware to it. Set a password of your own to continue.</p>";
  if (error_msg != "") {
    html += "<p class=\"error\">" + error_msg + "</p>";
  }
  html += "<form method=\"POST\" action=\"/setotapass\">";
  html += "<label for=\"newpass\">New OTA password (min. 8 characters)</label>";
  html += "<input type=\"password\" id=\"newpass\" name=\"newpass\" minlength=\"8\" required>";
  html += "<label for=\"confirmpass\">Confirm password</label>";
  html += "<input type=\"password\" id=\"confirmpass\" name=\"confirmpass\" minlength=\"8\" required>";
  html += "<button type=\"submit\">Set password</button>";
  html += "</form></div></html>";
  return html;
}

/****************************************************************************************************************************
 *  HTTP HANDLER: handleSETOTAPASS
 *
 *  Validates and stores a new OTA password (must differ from the default, must be at least
 *  8 characters, and must be entered twice identically), then hot-reloads ArduinoOTA with it
 *  so the new password takes effect immediately without requiring a reboot.
 ****************************************************************************************************************************/
void handleSETOTAPASS() {
  if (server.method() != HTTP_POST) {
    server.send(200, "text/html", build_ota_setup_page(""));
    return;
  }

  String newpass = server.arg("newpass");
  String confirmpass = server.arg("confirmpass");

  if (newpass.length() < 8) {
    server.send(200, "text/html", build_ota_setup_page("Password must be at least 8 characters."));
    return;
  }
  if (newpass != confirmpass) {
    server.send(200, "text/html", build_ota_setup_page("Passwords did not match. Try again."));
    return;
  }
  if (newpass == String(OTA_DEFAULT_PASSWORD)) {
    server.send(200, "text/html", build_ota_setup_page("That is still the default password -- choose a different one."));
    return;
  }

  ota_password = newpass;
  global_config.putString(String("x_ota_pass").c_str(), ota_password);

  // Hot-reload ArduinoOTA with the new password so it takes effect immediately.
  ArduinoOTA.end();
  ArduinoOTA.setPassword(ota_password.c_str());
  ArduinoOTA.begin();

  handleRoot();
}

void handleRoot() {
  if (ota_password_is_default()) {
    server.send(200, "text/html", build_ota_setup_page(""));
    return;
  }
  String html = MAIN_page;
  String css = DB_STYLESHEET;
  String js = JAVASCRIPT;
  server.send(200, "text/html", css + js + html);
}


/****************************************************************************************************************************
 *  HTTP HANDLER: handleNotFound
 *
 *  PURPOSE
 *  Provides a diagnostic 404 response when a client requests a URI that is not registered
 *  in the web server routing table.
 *
 *  BEHAVIOR
 *  - Constructs a plaintext diagnostic message containing:
 *      • Requested URI (server.uri())
 *      • HTTP method (GET or POST)
 *      • Number of arguments provided
 *      • Key-value pairs of all request arguments
 *
 *  - Sends the constructed message with HTTP status 404 (Not Found).
 *
 *  DEBUGGING VALUE
 *  - This handler is primarily intended for development and troubleshooting.
 *  - It allows inspection of malformed or unexpected requests directly via browser or API client.
 *
 *  MEMORY CONSIDERATION
 *  - Uses Arduino String concatenation, which may lead to heap fragmentation
 *    in long-running deployments if overused or called frequently.
 ****************************************************************************************************************************/
void handleNotFound() {
  String message = F("File Not Found\n\n");

  message += F("URI: ");
  message += server.uri();
  message += F("\nMethod: ");
  message += (server.method() == HTTP_GET) ? F("GET") : F("POST");
  message += F("\nArguments: ");
  message += server.args();
  message += F("\n");

  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, F("text/plain"), message);
}


/****************************************************************************************************************************
 *  HTTP HANDLER: handleDATA
 *
 *  PURPOSE
 *  This function acts as the real-time telemetry endpoint for the dashboard.
 *  It collects RF measurement data, computes derived electrical parameters,
 *  and returns them to the frontend for live visualization.
 *
 *  EXECUTION MODEL
 *  - Invoked periodically via client-side polling (AJAX/fetch from dashboard JavaScript).
 *  - Each call triggers a fresh measurement cycle and recalculation of metrics.
 *
 *  CORE DATA FLOW
 *  1. read_directional_couplers()
 *     - Acquires raw ADC values from forward and reflected RF detector circuits.
 *     - Updates global measurement variables:
 *         voltage_fwd, voltage_ref
 *         fwd_dbm, ref_dbm
 *         fwd_watt, ref_watt
 *
 *  2. VSWR computation
 *     - Voltage Standing Wave Ratio is derived from forward/reflected power ratio.
 *     - Formula is applied only when forward power exceeds reflected power to avoid invalid sqrt ratios.
 *
 *        vswr = (1 + sqrt(ref_watt / fwd_watt)) / (1 - sqrt(ref_watt / fwd_watt))
 *
 *     - This is a standard RF engineering approximation assuming matched impedance system behavior.
 *
 *  3. Return-value sanitization
 *     - VSWR is validated (must be ≥ 1).
 *     - Invalid numerical states (NaN) are later sanitized in string conversion.
 *
 *  4. Derived RF metrics
 *     - Return Loss (RL) is computed:
 *
 *         rl = fwd_dbm - ref_dbm
 *
 *       This represents mismatch loss in dB.
 *
 *  CONFIGURATION-DRIVEN OUTPUT FORMATTING
 *  - All output strings are conditionally generated based on stored user preferences in NVS.
 *
 *  Examples:
 *    b_show_mV   → controls display of raw ADC voltage readings
 *    b_show_dBm  → controls display of logarithmic RF power representation
 *    b_show_watt → controls display of linear power output in watts
 *
 *  This allows the frontend to selectively render UI elements without changing firmware logic.
 *
 *  ADDITIONAL CONFIGURATION VALUES
 *  - s_vswr_thresh:
 *      Threshold value defining when VSWR is considered critical.
 *
 *  - s_ant_name:
 *      User-defined antenna label used in UI display contexts.
 *
 *  - b_vswr_beep:
 *      Enables/disables audible warning when VSWR exceeds threshold.
 *
 *  OUT-OF-BOUNDS DETECTION
 *  - is_val_out_of_bounds() is called for forward and reflected voltages.
 *  - This likely implements ADC sanity checking or calibration range validation.
 *  - Helps detect sensor disconnects, saturation, or invalid readings.
 ****************************************************************************************************************************/
void handleDATA() {
  read_directional_couplers();

  double vswr = 0;

  if (fwd_watt > ref_watt) {
    vswr = (1 + sqrt(ref_watt / fwd_watt)) / (1 - sqrt(ref_watt / fwd_watt));
  }

  String vswr_str = "-1";
  String fwd_watt_str = "";
  String ref_watt_str = "";

  if (vswr >= 1) {
    vswr_str = String(vswr);
  }

  double rl = fwd_dbm - ref_dbm;

  // get vswr_threshold from general config
  String vswr_threshold = config.getString(String("s_vswr_thresh").c_str());

  String voltage_fwd_str = "";
  String voltage_ref_str = "";
  if (config.getString(String("b_show_mV").c_str()) != "false") {
    voltage_fwd_str = String(voltage_fwd) + " mV";
    voltage_ref_str = String(voltage_ref) + " mV";
  }

  String fwd_dbm_str = "";
  String ref_dbm_str = "";
  if (config.getString(String("b_show_dBm").c_str()) != "false") {
    fwd_dbm_str = String(fwd_dbm, 2);
    ref_dbm_str = String(ref_dbm, 2);
  }

  if (config.getString(String("b_show_watt").c_str()) != "false") {
    fwd_watt_str = String(fwd_watt, 10);
  }

  if (config.getString(String("b_show_watt").c_str()) != "false") {
    ref_watt_str = String(ref_watt, 10);
  }

  String rl_str = "-- ";
  if (rl > 0) {
    rl_str = (String(rl));
  }

  // A reading outside the calibrated range, or a malformed calibration
  // table, can still surface as "nan"/"inf" in these derived strings.
  // Sanitize all of them, not just rl_str, so a bad ADC sample never
  // reaches the frontend as garbage text driving the LED bars.
  fwd_dbm_str.replace("nan", "-- ");
  ref_dbm_str.replace("nan", "-- ");
  fwd_watt_str.replace("nan", "-- ");
  ref_watt_str.replace("nan", "-- ");
  fwd_dbm_str.replace("inf", "-- ");
  ref_dbm_str.replace("inf", "-- ");
  fwd_watt_str.replace("inf", "-- ");
  ref_watt_str.replace("inf", "-- ");
  rl_str.replace("nan", "-- ");
  rl_str.replace("inf", "-- ");

  String antenna_name = config.getString(String("s_ant_name").c_str());
  String vswr_beep = config.getString(String("b_vswr_beep").c_str());

  bool fwd_oob = is_val_out_of_bounds(voltage_fwd, true);
  bool ref_oob = is_val_out_of_bounds(voltage_ref, false);

  /****************************************************************************************************************************
 *  RESPONSE SERIALIZATION FOR FRONTEND COMMUNICATION
 *
 *  This code block constructs a single semicolon-separated payload string that is transmitted
 *  to the frontend via HTTP. The format is positional, meaning each field is identified strictly
 *  by its index after splitting on ';' in the client-side JavaScript.
 *
 *  Design rationale:
 *  - Minimizes HTTP overhead by sending compact scalar data instead of structured JSON.
 *  - Ensures deterministic ordering for low-complexity parsing on constrained clients.
 *  - Enables backward-compatible extension by appending new fields at the end.
 *
 *  IMPORTANT:
 *  - The frontend MUST parse this string using strict index mapping.
 *  - Any change in ordering will break UI interpretation unless synchronized with frontend code.
 ****************************************************************************************************************************/


  // Generate a semicolon separated string that will be sent to the frontend
  String output = fwd_watt_str + ";";                                   // data[0]: FWD power in Watt
  output += fwd_dbm_str + ";";                                          // data[1]: FWD dBm value
  output += voltage_fwd_str + ";";                                      // data[2]: FWD voltage
  output += ref_watt_str + ";";                                         // data[3]: REF power in Watt
  output += ref_dbm_str + ";";                                          // data[4]: REF dBm value
  output += voltage_ref_str + ";";                                      // data[5]: REF voltage
  output += vswr_str + ";";                                             // data[6]: VSWR value
  output += rl_str + ";";                                               // data[7]: RL value
  output += band + ";";                                                 // data[8]: band (e.g. "70cm")
  output += String(vswr_threshold) + ";";                               // data[9]: VSWR threshold (e.g. "3")
  output += antenna_name + ";";                                         // data[10]: Name of antenna (e.g. "X200")
  output += vswr_beep + ";";                                            // data[11]: should it beep if VSWR is too high? (true/false)
  output += config.getString(String("s_max_led_pwr_f").c_str()) + ";";  // data[12]: highest value in Watt for the FWD LED graph (e.g. "100")
  output += config.getString(String("s_max_led_pwr_r").c_str()) + ";";  // data[13]: highest value in Watt for the REF LED graph (e.g. "1")
  output += config.getString(String("s_max_led_vswr").c_str()) + ";";   // data[14]: highest value in Watt for the VSWR LED graph (e.g. "3")
  output += String(fwd_oob) + ";";                                      // data[15]: Is the FWD voltage out of bounds? (true/false)
  output += String(ref_oob) + ";";                                      // data[16]: Is the REF voltage out of bounds? (true/false)
  output += config.getString(String("b_show_led_fwd").c_str()) + ";";   // data[17]: Show the FWD LED bar graph? (true/false)
  output += config.getString(String("b_show_led_ref").c_str()) + ";";   // data[18]: Show the REF LED bar graph? (true/false)
  output += config.getString(String("b_show_led_vswr").c_str()) + ";";  // data[19]: Show the VSWR LED bar graph? (true/false)
  output += version + ";";                                              // data[20]: program version
  output += getTemp();                                                  // data[21]: temperature (final field, no trailing ';')

  server.send(200, "text/plain", output);
}

/****************************************************************************************************************************
 *  handleCONFIG()
 *
 *  PURPOSE:
 *    This function generates and serves the HTML configuration page of the device’s web interface.
 *
 *  TRIGGER:
 *    Invoked when the user clicks the "configuration" button on the main dashboard page.
 *
 *  RESPONSIBILITY:
 *    - Ensures required HTML subcomponents (text areas and configuration tables) are initialized.
 *    - Dynamically constructs an HTML document as a single string.
 *    - Injects CSS, configuration UI elements, and current runtime state (band selection, version).
 *    - Sends the resulting HTML page to the connected client via the embedded web server.
 *
 *  OUTPUT:
 *    HTTP response (200 OK) containing a fully rendered configuration page in HTML format.
 ****************************************************************************************************************************/

// main function for displaying the configuration page
// invoked by the "configuration" button on the dashboard page
void handleCONFIG() {
  if (ota_password_is_default()) {
    server.send(200, "text/html", build_ota_setup_page(""));
    return;
  }

  /**************************************************************************************************************************
   *  LAZY INITIALIZATION OF UI COMPONENTS
   *
   *  The configuration UI is partially precomputed and cached in global strings.
   *  If these caches are empty, they are generated on demand.
   **************************************************************************************************************************/
  if (conf_textareas == "") {
    build_textareas();   // Generates HTML input fields for detector translation configuration
  }

  if (conf_config_table == "") {
    build_config_table(); // Generates HTML table for general configuration parameters
  }

  /**************************************************************************************************************************
   *  BASE STYLESHEET LOADING
   *
   *  CFG_STYLESHEET contains CSS rules required for layout and visual styling of the configuration page.
   *  It is inserted at the beginning of the HTML document.
   **************************************************************************************************************************/
  String css = CFG_STYLESHEET;
  conf_content = css;

  /**************************************************************************************************************************
   *  HTML PAGE STRUCTURE CONSTRUCTION
   *
   *  The page is constructed incrementally by concatenating HTML fragments into a single string.
   *  This includes grid layout containers, titles, forms, and content sections.
   **************************************************************************************************************************/

  conf_content += "<div class='grid-container'>";

  /**************************************************************************************************************************
   *  TITLE SECTION
   *
   *  Displays static page title.
   **************************************************************************************************************************/
  conf_content += "<div id='title_box' class='titlebox maintitlebox'>";
  conf_content += "Configuration</div>";

  /**************************************************************************************************************************
   *  BAND SELECTION UI
   *
   *  Provides a dropdown form allowing the user to select the active frequency band.
   *  Submits automatically on change via POST request to /selectband.
   **************************************************************************************************************************/
  conf_content += "<div id='title_box' class='bandbox maintitlebox'>";
  conf_content += "<form method='POST' action='/selectband'>";
  conf_content += "Band: <label for='bands'></label><select class='button' onchange='this.form.submit()'' id='band' name='bands' size='1'>";

  /**************************************************************************************************************************
   *  DYNAMIC BAND LIST GENERATION
   *
   *  Iterates over the predefined band_list array and generates <option> elements.
   *  The currently active band is marked as "selected".
   **************************************************************************************************************************/
  for (int i = 0; i < sizeof band_list / sizeof band_list[0]; i++) {
    String selected = "";
    if (band_list[i] == band) {
      selected = "selected";  // Marks current band as active in dropdown
    }
    conf_content += "<option value='" + band_list[i] + "' " + selected + " >" + band_list[i] + "</option>";
  }

  conf_content += "</select></form>";
  conf_content += "</div>";

  /**************************************************************************************************************************
   *  SECTION: DETECTOR TRANSLATION CONFIGURATION
   *
   *  This section provides calibration controls mapping:
   *  ADC detector voltage (mV) -> RF power level (dBm).
   **************************************************************************************************************************/
  conf_content += "<div class='subtitle1 subtitlebox'>Translation Detector Voltage /mV to RF-Power Level /dBm</div>";

  conf_content += "<div class='translationitems contentbox'>";
  conf_content += conf_textareas;  // Prebuilt HTML input elements for calibration
  conf_content += "</div>";

  /**************************************************************************************************************************
   *  SECTION: GENERAL CONFIGURATION
   *
   *  Displays general system configuration options such as thresholds,
   *  display settings, and hardware behavior flags.
   **************************************************************************************************************************/
  conf_content += "<div class='subtitle2 subtitlebox'>General Configuration Items</div>";
  conf_content += "<div class='configitems contentbox'>";
  conf_content += conf_config_table; // Prebuilt HTML table of configuration parameters
  conf_content += "</div>";

  /**************************************************************************************************************************
   *  FOOTER SECTION
   *
   *  Provides navigation back to dashboard and displays firmware version information.
   **************************************************************************************************************************/
  conf_content += "<div class='footerbox'>";
  conf_content += "<form method='POST' action='/'><button class='linkbutton' value='back' name='back' type='submit'>Back to Dashboard</button> - Version: " + version + " </form>";
  conf_content += "</div>";

  /**************************************************************************************************************************
   *  FINALIZE HTML DOCUMENT
   *
   *  Closes grid container and HTML document structure.
   **************************************************************************************************************************/
  conf_content += "</div>";
  conf_content += "</html>";

  /**************************************************************************************************************************
   *  HTTP RESPONSE TRANSMISSION
   *
   *  Sends the constructed HTML page to the client using HTTP status 200 (OK)
   *  with MIME type "text/html".
   **************************************************************************************************************************/
  server.send(200, "text/html", conf_content);
}


/****************************************************************************************************************************
 *  build_textareas()
 *
 *  PURPOSE
 *  This function constructs an HTML calibration interface for forward (FWD) and reflected (REF)
 *  RF measurement data. It dynamically generates a form containing two textareas that allow the
 *  user to view and edit calibration mappings between ADC-derived millivolt values and computed
 *  power values (typically dBm).
 *
 *  OVERALL FUNCTIONAL ROLE IN SYSTEM
 *  The system uses stored calibration curves (persisted in SPIFFS as text files) to translate
 *  raw ADC readings into meaningful RF power metrics. This function:
 *    - Loads existing calibration data from filesystem
 *    - Converts it into in-memory arrays
 *    - Generates an editable HTML representation
 *    - Exposes it via the web interface for manual calibration adjustment
 *
 *  INPUT SOURCES (FILESYSTEM DEPENDENCY)
 *    - "/<band>fwd.txt"
 *        Contains forward power calibration data for the currently selected band.
 *
 *    - "/<band>ref.txt"
 *        Contains reflected power calibration data for the currently selected band.
 *
 *  Both files are read from SPIFFS and expected to contain serialized numeric mappings.
 *
 *  INTERNAL DATA FLOW
 *    1. readFile(SPIFFS, ...)
 *         Reads calibration text files from flash filesystem into String buffers.
 *
 *    2. clear_fwd_ref_array()
 *         Resets the calibration point counts (fwd_point_count, ref_point_count) to 0.
 *
 *    3. save_string_to_array(...)
 *         Parses the loaded calibration strings into sorted-by-mv point arrays:
 *           - fwd_points[] / fwd_point_count for the forward calibration curve
 *           - ref_points[] / ref_point_count for the reflected calibration curve
 *
 *    4. HTML generation loop
 *         Iterates over just the configured points (fwd_point_count / ref_point_count)
 *         and formats each as "mv:dbm", appended line-by-line into HTML textarea elements.
 *
 *  IMPORTANT IMPLEMENTATION DETAIL
 *    - Only configured points are rendered; there is no fixed-size scan.
 *
 *  OUTPUT FORMAT (WEB UI)
 *    The generated HTML consists of:
 *      - A <form> targeting POST endpoint "/modcal"
 *      - A table with two columns:
 *          Column 1: FWD calibration values
 *          Column 2: REF calibration values
 *      - Two <textarea> fields containing editable calibration mappings
 *      - A submit button labeled "Save Calibration Data"
 *
 *  USER INTERACTION MODEL
 *    - User edits calibration values directly in textareas
 *    - Each line follows format:
 *        index:value
 *    - Submitting the form triggers recalibration persistence via backend handler
 *
 *  STATE OUTPUT
 *    - The final HTML string is stored in global variable:
 *        conf_textareas
 *    - This variable is later injected into the web UI rendering pipeline
 *
 *  MEMORY / PERFORMANCE CHARACTERISTICS
 *    - Point arrays are sparse (only configured points are stored), so HTML generation
 *      cost scales with the number of calibration points actually configured, not a
 *      fixed 3300-entry scan.
 *    - No streaming output is used; full HTML is constructed in RAM as a String.
 *
 *  SIDE EFFECTS
 *    - Reads from SPIFFS filesystem
 *    - Mutates global point arrays: fwd_points, ref_points (and their counts)
 *    - Mutates global UI state: conf_textareas
 *
 ****************************************************************************************************************************/
void build_textareas() {
  String fwd = readFile(SPIFFS, String("/" + band + "fwd.txt").c_str());
  String ref = readFile(SPIFFS, String("/" + band + "ref.txt").c_str());

  clear_fwd_ref_array();

  save_string_to_array(fwd, fwd_points, fwd_point_count);
  save_string_to_array(ref, ref_points, ref_point_count);

  String tbl = "<form action=\"/modcal\" method=\"POST\">";
  tbl += "<table class='styled-table'>";
  tbl += "<thead><tr><td>" + band + " FWD (mV:dBm)</td><td>" + band + " REF (mV:dBm)</td></tr></thead>";
  tbl += "<tr><td>";
  tbl += "<textarea id='fwd_textarea' name='fwd_textarea' rows='22'>";
  for (int i = 0; i < fwd_point_count; i++) {
    tbl += String(fwd_points[i].mv) + ":" + String(fwd_points[i].dbm, 5) + "\n";
  }
  tbl += "</textarea>";
  tbl += "</td><td>";
  tbl += "<textarea id='ref_textarea' name='ref_textarea' rows='22'>";
  for (int i = 0; i < ref_point_count; i++) {
    tbl += String(ref_points[i].mv) + ":" + String(ref_points[i].dbm, 5) + "\n";
  }
  tbl += "</textarea>";
  tbl += "</td></tr></table>";
  tbl += "<button class='button' value='save' name='save' type='submit'>Save Calibration Data</button>";
  tbl += "</form>";
  conf_textareas = tbl;
}


/****************************************************************************************************************************
 *  build_config_table()
 *
 *  PURPOSE
 *  This function dynamically generates an HTML configuration table used in the device’s web interface.
 *  The table represents all configurable system parameters defined in the global configuration schema
 *  (band_config_items, band_config_defaults, band_config_nice_names).
 *
 *  The output is stored in the global string `conf_config_table`, which is later rendered in the web UI.
 *
 *  OVERVIEW OF OPERATION
 *  1. Initializes an HTML form targeting the "/modcfg" endpoint using HTTP POST.
 *  2. Creates a styled HTML table container.
 *  3. Iterates over all configuration keys defined in `band_config_items`.
 *  4. For each key:
 *     - Skips entries prefixed with "x_" (reserved or disabled configuration keys).
 *     - Attempts to retrieve the persisted value from NVS storage via Preferences (`config`).
 *     - If no value exists ("xxx" sentinel), initializes it with the default value.
 *     - Determines the correct HTML input type based on the stored value:
 *         - Boolean-like "true"  → checkbox (checked)
 *         - Boolean-like "false" → checkbox (unchecked)
 *         - Otherwise           → text input field
 *  5. Uses human-readable labels from `band_config_nice_names` for UI presentation.
 *  6. Finalizes the table and appends a submit button to persist configuration changes.
 *
 *  This function only populates `conf_config_table`; it does not send an HTTP response.
 *  Callers are responsible for calling handleCONFIG() afterward if a response should be
 *  sent (handleCONFIG() itself does this when lazily filling an empty cache; handleMODCFG()
 *  does it explicitly after saving). Previously this function called handleCONFIG() itself,
 *  which caused handleCONFIG()'s cache-fill path to send two HTTP responses per request.
 *
 *  IMPORTANT DESIGN BEHAVIOR
 *  - The function performs lazy initialization of missing configuration keys.
 *  - Boolean values are encoded as strings ("true"/"false") rather than native types.
 *  - HTML generation is performed via string concatenation rather than templating.
 *  - The function assumes strict positional alignment across:
 *        band_config_items
 *        band_config_defaults
 *        band_config_nice_names
 *
 *  SIDE EFFECTS
 *  - May write default values into persistent NVS storage if keys are missing.
 *  - Mutates global string `conf_config_table`.
 *
 *  LIMITATIONS / IMPLICIT ASSUMPTIONS
 *  - No HTML escaping is performed on stored values (potential injection surface if inputs are untrusted).
 *  - Checkbox logic assumes only "true"/"false" string values for boolean semantics.
 *  - Fixed input length attribute (`valuelength=16`) is applied to all non-boolean inputs.
 ****************************************************************************************************************************/
void build_config_table() {
  conf_config_table = "<form action=\"/modcfg\" method=\"POST\">";
  conf_config_table += "<table class='styled-table'>";
  //conf_config_table += "<thead><tr><td>Key</td><td>Value</td></td></tr></thead>";
  for (int i = 0; i < sizeof band_config_items / sizeof band_config_items[0]; i++) {
    if (!band_config_items[i].startsWith("x_")) {
      String stored_val = config.getString(band_config_items[i].c_str(), "xxx");
      if (stored_val == "xxx") {
        config.putString(band_config_items[i].c_str(), band_config_defaults[i]);
        stored_val = config.getString(band_config_items[i].c_str(), "");
      }
      conf_config_table += "<tr><td>";
      conf_config_table += band_config_nice_names[i];
      conf_config_table += "</td><td>";

      if (String(stored_val).equalsIgnoreCase("true")) {
        conf_config_table += "<input type='checkbox' name='" + band_config_items[i] + "' id='" + band_config_items[i] + "' value='true' checked>";
      } else if (String(stored_val).equalsIgnoreCase("false")) {
        conf_config_table += "<input type='checkbox' name='" + band_config_items[i] + "' id='" + band_config_items[i] + "' value='false'>";
      } else {
        conf_config_table += "<input name='" + band_config_items[i] + "' value='" + String(stored_val) + "' valuelength=16>";
      }
      conf_config_table += "</td></tr>";
    }
  }
  conf_config_table += "</table><button class='button' value='save' name='save' type='submit'>Save Configuration</button></form>";
}

/****************************************************************************************************************************
 *  handleMODCAL()
 *
 *  PURPOSE
 *  -------
 *  This function processes calibration data submitted from the configuration web page.
 *  It updates the internal forward (FWD) and reflected (REF) measurement lookup tables
 *  for the currently selected frequency band.
 *
 *  The function acts as a bridge between:
 *    - Web UI input (textarea-based calibration data)
 *    - Internal runtime calibration point arrays (fwd_points / ref_points)
 *    - Persistent storage in SPIFFS filesystem
 *
 *  INPUT SOURCES
 *  -------------
 *  The function expects HTTP request arguments provided via the global web server object:
 *
 *    server.arg("fwd_textarea")
 *      - Multi-line string containing forward power calibration values.
 *      - Each line is expected to represent a numeric value or mapping entry.
 *
 *    server.arg("ref_textarea")
 *      - Multi-line string containing reflected power calibration values.
 *
 *  A newline character is appended to each input string to ensure proper parsing
 *  by downstream processing functions (save_string_to_array()).
 *
 *  INTERNAL PROCESSING STEPS
 *  -------------------------
 *  1. INPUT NORMALIZATION
 *     - The received FWD and REF strings are extended with newline termination.
 *     - This ensures compatibility with line-based parsing logic.
 *
 *  2. STATE RESET
 *     - clear_fwd_ref_array() is called to reset all existing calibration data.
 *     - This prevents mixing old and newly submitted calibration values.
 *
 *  3. ARRAY POPULATION
 *     - save_string_to_array() parses the textual calibration input into sorted
 *       point arrays:
 *         fwd_points[] / fwd_point_count
 *         ref_points[] / ref_point_count
 *
 *  4. SERIALIZATION FOR PERSISTENCE (FWD)
 *     - The forward point array is serialized into a string format:
 *         "mv:dbm"
 *       one entry per line, for every configured point.
 *     - Values are formatted with 5 decimal places for precision consistency.
 *     - The resulting string is written to SPIFFS:
 *         /<band>fwd.txt
 *
 *  5. SERIALIZATION FOR PERSISTENCE (REF)
 *     - The reflected array is serialized in the same manner as FWD.
 *     - Stored as:
 *         /<band>ref.txt
 *
 *  6. UI REBUILD
 *     - build_textareas() regenerates the HTML representation of the updated arrays.
 *
 *  7. CONFIGURATION PAGE REFRESH
 *     - handleCONFIG() is called to immediately reload or re-render the configuration page
 *       so that the user sees updated calibration values without manual refresh.
 *
 *  DATA FORMAT IN SPIFFS
 *  ---------------------
 *  Each stored file contains lines of the form:
 *
 *      index:value
 *
 *  Example:
 *      0:0.12345
 *      1:0.23456
 *
 *  DESIGN NOTES / BEHAVIORAL CHARACTERISTICS
 *  -----------------------------------------
 *  - This function fully overwrites existing calibration data for the selected band.
 *  - A calibration point of exactly 0 dBm is a valid, persisted point (unlike the old
 *    dense-array implementation, which could not distinguish 0 dBm from "unset").
 *  - Calibration resolution and accuracy depend on save_string_to_array() parsing logic.
 *  - The function assumes global state: `band`, `fwd_points`, `ref_points`, and `server`.
 *
 *  SIDE EFFECTS
 *  ------------
 *  - Modifies global point arrays: fwd_points, ref_points
 *  - Writes files to SPIFFS filesystem
 *  - Triggers UI regeneration and config page reload
 *  - Performs full replacement of calibration dataset for the active band
 *
 ****************************************************************************************************************************/
void handleMODCAL() {
  // Guard against accidental/malformed GET requests (link prefetch, crawlers,
  // typed URLs): this endpoint is only meant to be reached via the calibration
  // form's POST. A GET here would otherwise wipe all calibration data for the
  // current band with empty textarea values.
  if (server.method() != HTTP_POST) {
    handleCONFIG();
    return;
  }

  String fwd = server.arg("fwd_textarea") + "\n";
  String ref = server.arg("ref_textarea") + "\n";
  clear_fwd_ref_array();
  save_string_to_array(fwd, fwd_points, fwd_point_count);
  save_string_to_array(ref, ref_points, ref_point_count);

  String fwd_of_array = "";
  for (int i = 0; i < fwd_point_count; i++) {
    fwd_of_array += String(fwd_points[i].mv) + ":" + String(fwd_points[i].dbm, 5) + "\n";
  }
  writeFile(SPIFFS, String("/" + band + "fwd.txt").c_str(), fwd_of_array.c_str());

  String ref_of_array = "";
  for (int i = 0; i < ref_point_count; i++) {
    ref_of_array += String(ref_points[i].mv) + ":" + String(ref_points[i].dbm, 5) + "\n";
  }
  writeFile(SPIFFS, String("/" + band + "ref.txt").c_str(), ref_of_array.c_str());

  build_textareas();
  handleCONFIG();
}

/****************************************************************************************************************************
 *  FUNCTION: clear_fwd_ref_array
 *
 *  PURPOSE:
 *    Resets the forward (FWD) and reflected (REF) calibration point tables.
 *
 *  BEHAVIOR:
 *    - Sets fwd_point_count and ref_point_count to 0.
 *    - The underlying fwd_points[]/ref_points[] contents are left as-is but are no
 *      longer considered valid, since every reader is bounded by the point count.
 *
 *  SIDE EFFECTS:
 *    - The in-memory calibration tables are cleared; callers are expected to
 *      immediately repopulate them via save_string_to_array() (build_textareas()
 *      and handleMODCAL() both do this).
 *
 *  TYPICAL USE CASES:
 *    - Device reset or reinitialization
 *    - Band change or calibration reset
 *    - User-triggered "clear history" action in web interface
 *****************************************************************************************************************************/
void clear_fwd_ref_array() {
  fwd_point_count = 0;
  ref_point_count = 0;
}

/****************************************************************************************************************************
 *  save_string_to_array
 *
 *  PURPOSE
 *  Parses the "key:value" calibration text (from the frontend textarea, or a file loaded from
 *  SPIFFS) into a sorted-by-mv array of CalPoint entries.
 *
 *  INPUT FORMAT
 *  `table_data` contains rows separated by '\n', each in the form "mv:dbm", e.g.:
 *      0:0.12345
 *      1500:23.4
 *
 *  PARAMETERS
 *  table_data - serialized calibration table text.
 *  arr[]      - destination CalPoint array (fwd_points or ref_points).
 *  count      - output parameter: number of valid points written into arr[].
 *
 *  BEHAVIOR
 *  - Rows without a valid "mv:dbm" pair, or with mv outside the plausible ADC range
 *    (0..4095 mV), are skipped rather than corrupting memory.
 *  - A repeated mv value overwrites the earlier point for that mv (upsert), matching the
 *    "last one wins" behavior of the original dense-array implementation.
 *  - Once all rows are parsed, the array is sorted ascending by mv so millivolt_to_dbm()
 *    can find the interpolation bracket with a single forward scan.
 *  - Points beyond MAX_CAL_POINTS are ignored (a generous cap; real calibration curves need
 *    only a few dozen points).
 ****************************************************************************************************************************/
void save_string_to_array(String table_data, CalPoint arr[], int &count) {
  count = 0;
  int r = 0;
  for (int i = 0; i <= table_data.length(); i++) {
    if (i == table_data.length() || table_data[i] == '\n') {
      if (i - r > 1) {
        String row = table_data.substring(r, i);
        int sep = row.indexOf(':');
        if (sep > 0) {
          int key = row.substring(0, sep).toInt();
          double val = row.substring(sep + 1).toDouble();
          if (key >= 0 && key <= 4095) {
            int existing = -1;
            for (int k = 0; k < count; k++) {
              if (arr[k].mv == key) {
                existing = k;
                break;
              }
            }
            if (existing >= 0) {
              arr[existing].dbm = val;
            } else if (count < MAX_CAL_POINTS) {
              arr[count].mv = key;
              arr[count].dbm = val;
              count++;
            }
          }
        }
      }
      r = (i + 1);
    }
  }

  // insertion sort ascending by mv (count is small, so O(n^2) is negligible)
  for (int a = 1; a < count; a++) {
    CalPoint tmp = arr[a];
    int b = a - 1;
    while (b >= 0 && arr[b].mv > tmp.mv) {
      arr[b + 1] = arr[b];
      b--;
    }
    arr[b + 1] = tmp;
  }
}

// =============================================================================
// Function: handleMODCFG
//
// Purpose:
//   Processes incoming HTTP requests from the configuration web interface and
//   updates the persistent configuration storage (NVS via Preferences) for the
//   currently selected band.
//
// High-level behavior:
//   - Iterates over the predefined configuration key set (band_config_items).
//   - Reads corresponding HTTP arguments from the web server request.
//   - Distinguishes between boolean and non-boolean configuration values.
//   - Writes normalized values into persistent storage.
//   - Rebuilds the configuration table used by the web UI.
//
// Important design details:
//   - Boolean configuration keys are identified by the prefix "b_".
//   - Boolean values are NOT read directly from request payloads in a strict
//     type sense; instead, presence/absence of HTTP arguments determines state.
//   - Missing boolean arguments are interpreted as "false" (unchecked HTML checkbox behavior).
//   - Present boolean arguments are interpreted as "true".
//   - All non-boolean values are stored verbatim from the HTTP argument string.
//
// Web server dependency:
//   - Relies on global 'server' object (WebServer instance) providing:
//       * hasArg(key): checks if parameter exists in request
//       * arg(key): retrieves parameter value as string
//
// Persistence layer:
//   - Uses ESP32 Preferences (NVS) via 'config.putString(...)'.
//   - All values are stored as strings regardless of logical type,
//     enabling uniform retrieval and simplified serialization.
//
// UI update step:
//   - After updating all configuration keys, the cached HTML configuration
//     table string is cleared.
//   - build_config_table() regenerates conf_config_table from updated persistent values.
//   - handleCONFIG() is then called explicitly to actually send the HTTP response
//     (build_config_table() itself only populates the cache, it does not respond).
//
// Complexity:
//   - O(n) over number of configuration keys, where n = sizeof(band_config_items).
// =============================================================================
void handleMODCFG() {
  // Guard against accidental/malformed GET requests: a GET here would blank
  // every non-boolean config value (empty server.arg()) and set every
  // boolean config value to "false" (missing checkbox args).
  if (server.method() != HTTP_POST) {
    handleCONFIG();
    return;
  }

  for (int i = 0; i < sizeof band_config_items / sizeof band_config_items[0]; i++) {
    if (!server.hasArg(band_config_items[i]) and band_config_items[i].startsWith("b_")) {
      config.putString(band_config_items[i].c_str(), "false");
    } else if (server.hasArg(band_config_items[i]) and band_config_items[i].startsWith("b_")) {
      config.putString(band_config_items[i].c_str(), "true");
    } else {
      config.putString(band_config_items[i].c_str(), server.arg(band_config_items[i]));
    }
  }
  conf_config_table = "";
  build_config_table();
  handleCONFIG();
}

/****************************************************************************************************************************
 *  handleBAND()
 *
 *  PURPOSE
 *  This function processes a user-initiated band selection from the web configuration interface.
 *  It updates the active operating band, rebinds all band-dependent configuration namespaces,
 *  and reloads the corresponding calibration/configuration data stored in non-volatile memory (NVS).
 *
 *  EXECUTION CONTEXT
 *  This function is typically invoked as an HTTP request handler in response to a user action
 *  in the configuration web page (e.g., selecting a radio frequency band from a dropdown menu).
 *
 *  HIGH-LEVEL BEHAVIOR
 *  1. Reads the selected band identifier from the HTTP request parameter "bands".
 *  2. Updates internal band state variables used for forward/reflected power key derivation.
 *  3. Persists the selected band in global NVS storage for retention across reboots.
 *  4. Closes the currently active configuration namespace.
 *  5. Opens a new configuration namespace specific to the selected band.
 *  6. Resets intermediate HTML/UI buffers used for configuration rendering.
 *  7. Delegates to handleCONFIG() to regenerate and serve updated configuration UI/content.
 *
 *  DETAILED STEP BEHAVIOR
 *
 *  - band = server.arg("bands");
 *      Retrieves the HTTP query/post parameter named "bands" from the active web server request.
 *      This parameter represents the user-selected frequency band as a string.
 *
 *  - band_fwd = band + "_fwd";
 *  - band_ref = band + "_ref";
 *      Constructs derived configuration keys used elsewhere in the system to separate
 *      forward and reflected power calibration/storage per band.
 *
 *  - global_config.putString(...)
 *      Stores the selected band persistently in the "global_config" NVS namespace.
 *      The key "x_selected_band" is used to restore the last selected band after reboot.
 *
 *  - config.end();
 *      Closes the current Preferences (NVS) namespace to ensure no stale handle remains
 *      before switching to a different band-specific configuration namespace.
 *
 *  - String bnd_cnf = "config_" + band;
 *      Constructs a new namespace name for band-specific configuration storage.
 *      Example: "config_70cm", "config_2m", etc.
 *
 *  - config.begin(bnd_cnf.c_str(), false);
 *      Opens the newly selected band-specific configuration namespace in read/write mode.
 *      This ensures subsequent config operations target the correct band dataset.
 *
 *  - conf_textareas = "";
 *  - conf_config_table = "";
 *      Clears previously generated HTML/UI fragments related to configuration rendering.
 *      This avoids mixing UI state between different bands.
 *
 *  - handleCONFIG();
 *      Triggers regeneration of configuration data and/or UI output for the newly selected band.
 *      This typically rebuilds the configuration interface using the updated NVS context.
 *
 *  DESIGN NOTES
 *  - This function implements a dynamic namespace switching mechanism for per-band calibration data.
 *  - It relies heavily on ESP32 NVS (Preferences) to isolate configuration per frequency band.
 *  - UI regeneration is performed immediately after state transition to ensure consistency
 *    between backend configuration and frontend representation.
 *
 *  SIDE EFFECTS
 *  - Changes global band state (band, band_fwd, band_ref).
 *  - Switches active NVS namespace in `config`.
 *  - Modifies persistent storage (global_config).
 *  - Resets UI-related global buffers.
 *  - Triggers configuration UI regeneration via handleCONFIG().
 ****************************************************************************************************************************/
void handleBAND() {
  // Guard against accidental/malformed GET requests: a GET here (empty
  // "bands" arg) would persist an empty band name and open a NVS namespace
  // for it, corrupting the selected-band state.
  if (server.method() != HTTP_POST) {
    handleCONFIG();
    return;
  }

  String requested_band = server.arg("bands");
  bool valid_band = false;
  for (int i = 0; i < sizeof band_list / sizeof band_list[0]; i++) {
    if (band_list[i] == requested_band) {
      valid_band = true;
      break;
    }
  }
  if (!valid_band) {
    // Unknown/malformed band name: ignore the request rather than creating
    // an orphaned "config_<garbage>" NVS namespace.
    handleCONFIG();
    return;
  }

  band = requested_band;
  band_fwd = band + "_fwd";
  band_ref = band + "_ref";
  global_config.putString(String("x_selected_band").c_str(), band);
  config.end();
  String bnd_cnf = "config_" + band;
  config.begin(bnd_cnf.c_str(), false);
  conf_textareas = "";
  conf_config_table = "";
  //build_config_table();
  //build_textareas();
  handleCONFIG();
}


/****************************************************************************************************************************
 *  TEMPERATURE ACQUISITION FUNCTION
 *
 *  Function: getTemp()
 *
 *  Purpose:
 *    This function reads the current temperature from a DS18B20-compatible sensor
 *    connected via the OneWire bus (configured globally through the `sensors` object).
 *
 *  Output:
 *    Returns a formatted string containing:
 *      - A label ("Temp: ")
 *      - The measured temperature value (1 decimal precision)
 *      - The unit ("°C" or "°F")
 *
 *    If temperature display is disabled via configuration, an empty string is returned.
 *
 *  Configuration dependencies (stored in ESP32 NVS via Preferences `config`):
 *    - "b_show_temp"
 *        Controls whether temperature output is enabled.
 *        Expected values: "true" / "false"
 *
 *    - "b_celsius"
 *        Selects temperature unit mode.
 *        Any value other than "false" is interpreted as Celsius mode.
 *        Only explicit string "false" switches to Fahrenheit mode.
 *
 *  Sensor behavior:
 *    - sensors.requestTemperatures() triggers a blocking ~750ms conversion (12-bit
 *      default resolution). Since this function is called on every /readDATA poll
 *      and the web server is single-threaded, a fresh conversion is only requested
 *      at most once every TEMP_REFRESH_INTERVAL_MS (5s); the cached Celsius/Fahrenheit
 *      values (last_temp_c / last_temp_f, static locals) are served on polls in between.
 *    - sensors.getTempCByIndex(0) / getTempFByIndex(0) read the first detected sensor.
 *
 *  Validity filtering:
 *    - The function checks `temp_float > -100` as a heuristic validity gate.
 *      DS18B20 devices typically return -127°C when disconnected or invalid.
 *      The chosen threshold suppresses obviously invalid readings.
 *
 *  Notes on implementation details:
 *    - All configuration reads convert keys to C-string via String(...).c_str().
 *      This is required by the Preferences API which expects const char* keys.
 *
 *    - The function builds the output string incrementally using String concatenation,
 *      which is acceptable on ESP32 but may contribute to heap fragmentation in
 *      long-running systems if called at high frequency.
 *
 *  Edge cases:
 *    - If the sensor is not present or reading fails, "--" is returned as value.
 *    - If temperature display is disabled, the function returns an empty string.
 ****************************************************************************************************************************/
String getTemp() {
  String ret = "";
  if (config.getString(String("b_show_temp").c_str()) == "true") {
    // sensors.requestTemperatures() blocks for ~750ms per DS18B20 conversion
    // (12-bit default resolution). getTemp() is called on every /readDATA poll,
    // and the web server is single-threaded, so doing a fresh conversion every
    // poll stalls the whole server for most of a second, once per poll -- making
    // poll cadence (and therefore the dashboard) irregular. Temperature changes
    // slowly, so only re-convert on a timer and serve the cached reading between
    // refreshes.
    static const unsigned long TEMP_REFRESH_INTERVAL_MS = 5000;
    static unsigned long last_temp_read_ms = 0;
    static bool have_temp_reading = false;
    static float last_temp_c = -127;
    static float last_temp_f = -127;

    if (!have_temp_reading || millis() - last_temp_read_ms >= TEMP_REFRESH_INTERVAL_MS) {
      sensors.requestTemperatures();
      last_temp_c = sensors.getTempCByIndex(0);
      last_temp_f = sensors.getTempFByIndex(0);
      last_temp_read_ms = millis();
      have_temp_reading = true;
    }

    String label = "Temp: ";
    String temp_string = "--";
    String unit = "";
    float temp_float;
    if (config.getString(String("b_celsius").c_str()) != "false") {
      unit = "°C";
      temp_float = last_temp_c;
    } else {
      unit = "°F";
      temp_float = last_temp_f;
    }
    if (temp_float > -100){
      temp_string = String(temp_float, 1);
    }
    ret = label + temp_string + unit;
  }
  return ret;
}

/****************************************************************************************************************************
 *  INITIALIZATION ROUTINE (setup)
 *
 *  This function is executed once at boot. It is responsible for:
 *  - Initializing serial debugging
 *  - Starting sensor subsystems
 *  - Configuring Ethernet (WT32_ETH01 / LAN8720)
 *  - Bringing up the web server and registering endpoints
 *  - Mounting SPIFFS filesystem
 *  - Restoring persistent configuration (NVS)
 *  - Preparing runtime UI data structures
 *
 *  Execution order is critical because several subsystems depend on prior initialization
 *  (e.g. Ethernet requires event handlers and sensor configuration is independent but must
 *  be ready before measurement endpoints are used).
 ****************************************************************************************************************************/
void setup() {

  /**********************************************************************************************************************
   * SERIAL DEBUG INTERFACE
   *
   * Initializes UART at 115200 baud for logging system status, network state,
   * and runtime diagnostics.
   **********************************************************************************************************************/
  Serial.begin(115200);

  /**********************************************************************************************************************
   * TEMPERATURE SENSOR INITIALIZATION
   *
   * Initializes the DallasTemperature library, which performs:
   * - OneWire bus discovery on the configured GPIO pin
   * - Sensor enumeration (e.g., DS18B20 devices)
   * - Internal calibration and resolution setup (library default behavior)
   **********************************************************************************************************************/
  sensors.begin();


  Serial.print("\nStarting AdvancedWebServer on " + String(ARDUINO_BOARD));
  Serial.println(" with " + String(SHIELD_TYPE));
  Serial.println(WEBSERVER_WT32_ETH01_VERSION);

  /**********************************************************************************************************************
   * ETHERNET EVENT SYSTEM REGISTRATION
   *
   * Registers callback handlers for Ethernet PHY state changes.
   * Required before ETH.begin() so that link-up/link-down events are properly processed.
   **********************************************************************************************************************/
  WT32_ETH01_onEvent();

  /**********************************************************************************************************************
   * ETHERNET PHY INITIALIZATION
   *
   * Starts the LAN8720 Ethernet interface via ESP32 ETH driver.
   * ETH_PHY_ADDR and ETH_PHY_POWER define hardware-specific configuration parameters.
   **********************************************************************************************************************/
  ETH.begin(ETH_PHY_ADDR, ETH_PHY_POWER);

  /**********************************************************************************************************************
   * STATIC IP CONFIGURATION (OPTIONAL)
   *
   * Configures a static network identity for the device.
   * If this line is omitted, DHCP will be used instead.
   *
   * Parameters:
   *  - myIP  : static IP address
   *  - myGW  : gateway address
   *  - mySN  : subnet mask
   *  - myDNS : DNS server
   **********************************************************************************************************************/
  ETH.config(myIP, myGW, mySN, myDNS);

  /**********************************************************************************************************************
   * NETWORK SYNCHRONIZATION BLOCKING CALL
   *
   * Blocks execution until Ethernet link is established and an IP address is assigned.
   * Ensures web server is only started after network stack is fully operational.
   **********************************************************************************************************************/
  WT32_ETH01_waitForConnect();

  /**********************************************************************************************************************
   * ADC CONFIGURATION
   *
   * Sets ADC resolution to 12-bit:
   * - Range: 0–4095
   * - Required for consistent RF detector sampling accuracy
   *
   * Done early and independent of SPIFFS/NVS so measurement is ready regardless
   * of what happens below.
   **********************************************************************************************************************/
  analogReadResolution(12);

  /**********************************************************************************************************************
   * PERSISTENT CONFIGURATION INITIALIZATION (GLOBAL NVS)
   *
   * Opens NVS namespace "config" for reading/writing system-wide settings.
   * Retrieves selected frequency band from non-volatile storage.
   * Independent of SPIFFS, so this runs regardless of filesystem mount state.
   **********************************************************************************************************************/
  global_config.begin("config", false);
  band = global_config.getString(String("x_selected_band").c_str());

  /**********************************************************************************************************************
   * OTA PASSWORD
   *
   * Loads the OTA password from NVS, falling back to (and persisting) the factory default on
   * first-ever boot. As long as it's still the default, handleRoot()/handleCONFIG() refuse to
   * serve the normal UI and force a real password to be set first.
   **********************************************************************************************************************/
  ota_password = global_config.getString(String("x_ota_pass").c_str());
  if (ota_password == "") {
    ota_password = OTA_DEFAULT_PASSWORD;
    global_config.putString(String("x_ota_pass").c_str(), ota_password);
  }

  /**********************************************************************************************************************
   * DEFAULT BAND FALLBACK LOGIC
   *
   * If no band has been stored previously, initialize it with the predefined default
   * and persist it into NVS.
   **********************************************************************************************************************/
  if (band == "") {
    global_config.putString(String("x_selected_band").c_str(), default_band);
    band = default_band;
  }

  /**********************************************************************************************************************
   * BAND-SPECIFIC CONFIGURATION CONTEXT
   *
   * Each frequency band has its own configuration namespace in NVS.
   * This allows per-band calibration and display settings.
   **********************************************************************************************************************/
  String bnd_cnf = "config_" + band;
  config.begin(bnd_cnf.c_str(), false);

  /**********************************************************************************************************************
   * SPIFFS FILESYSTEM INITIALIZATION
   *
   * Mounts internal flash filesystem used for serving static assets
   * (HTML/CSS/JS files, configuration resources).
   *
   * FORMAT_SPIFFS_IF_FAILED:
   *  If mounting fails, filesystem may be reformatted automatically depending on config.
   *
   * IMPORTANT: a mount failure here used to `return` out of setup() entirely,
   * meaning the web server never started at all -- the device went completely
   * silent with nothing but a serial print, on a device that's often mounted
   * somewhere inconvenient to reach. Now a failure only degrades calibration
   * features (readFile() already handles missing files gracefully, returning
   * an empty string), while the dashboard, network, OTA, and watchdog still
   * come up normally.
   **********************************************************************************************************************/
  bool spiffs_ok = SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED);
  if (!spiffs_ok) {
    Serial.println("SPIFFS Mount Failed -- calibration data unavailable, continuing without it");
  }

  /**********************************************************************************************************************
   * WEB SERVER ROUTE REGISTRATION
   *
   * Defines HTTP endpoints and maps them to handler functions:
   *
   *  "/"            -> Root dashboard interface
   *  "/readDATA"    -> Live measurement data endpoint (RF power, SWR, etc.)
   *  "/config"      -> Configuration UI page
   *  "/modcfg"      -> Configuration modification handler
   *  "/selectband"  -> Frequency band selection handler
   *  "/modcal"      -> Calibration adjustment handler
   **********************************************************************************************************************/
  server.on(F("/"), handleRoot);
  server.on("/readDATA", handleDATA);
  server.on("/config", handleCONFIG);
  server.on("/modcfg", handleMODCFG);
  server.on("/selectband", handleBAND);
  server.on("/modcal", handleMODCAL);
  server.on("/setotapass", handleSETOTAPASS);

  /**********************************************************************************************************************
   * FALLBACK ROUTE HANDLING
   *
   * Registers a catch-all handler for undefined HTTP routes.
   * Ensures proper 404-style behavior and debugging visibility.
   **********************************************************************************************************************/
  server.onNotFound(handleNotFound);

  /**********************************************************************************************************************
   * START HTTP SERVER
   *
   * Begins listening for incoming TCP connections on port 80 (default).
   * At this point the device becomes reachable via browser.
   **********************************************************************************************************************/
  server.begin();

  Serial.print(F("HTTP EthernetWebServer is @ IP : "));
  Serial.println(ETH.localIP());

  /**********************************************************************************************************************
   * mDNS -- reachable as "<device_hostname>.local" instead of only the
   * hardcoded static IP. device_hostname is defined near the top of this
   * file for easy editing before flashing.
   **********************************************************************************************************************/
  if (MDNS.begin(device_hostname.c_str())) {
    MDNS.addService("http", "tcp", 80);
    Serial.print(F("mDNS responder started: http://"));
    Serial.print(device_hostname);
    Serial.println(F(".local"));
  } else {
    Serial.println(F("mDNS responder failed to start"));
  }

  /**********************************************************************************************************************
   * OTA (Over-The-Air) firmware updates via LAN -- lets you reflash this
   * device (e.g. from the Arduino IDE's Network Ports) without needing
   * physical USB access, which matters for a meter mounted somewhere remote.
   **********************************************************************************************************************/
  ArduinoOTA.setHostname(device_hostname.c_str());
  ArduinoOTA.setPassword(ota_password.c_str());
  ArduinoOTA.onStart([]() {
    Serial.println(F("OTA update starting"));
  });
  ArduinoOTA.onEnd([]() {
    Serial.println(F("OTA update complete"));
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA progress: %u%%\r\n", (progress * 100) / total);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA error [%u]\r\n", error);
  });
  ArduinoOTA.begin();

  /**********************************************************************************************************************
   * UI CONFIGURATION RENDERING
   *
   * Builds HTML form elements (textareas, tables, etc.) representing
   * the current configuration state for the web interface. Safe to call
   * even if SPIFFS failed to mount above (readFile() degrades gracefully).
   **********************************************************************************************************************/
  build_textareas();

  /**********************************************************************************************************************
   * WATCHDOG TIMER
   *
   * If loop() doesn't check in (via esp_task_wdt_reset()) within
   * WDT_TIMEOUT_SECONDS, the device reboots instead of staying silently
   * wedged -- important for a meter that isn't easily power-cycled by hand.
   * Enabled last, after every blocking setup step (Ethernet wait, mDNS,
   * OTA) has already completed, so none of that startup time counts against it.
   **********************************************************************************************************************/
  esp_task_wdt_init(WDT_TIMEOUT_SECONDS, true);
  esp_task_wdt_add(NULL);
}


/****************************************************************************************************************************
 *  MAIN EXECUTION LOOP
 *
 *  This function runs continuously after setup() completes.
 *
 *  server.handleClient():
 *    - Checks for incoming TCP connections, parses HTTP requests, dispatches
 *      registered route handlers, and maintains web UI responsiveness.
 *
 *  ArduinoOTA.handle():
 *    - Services pending over-the-air firmware update requests.
 *
 *  esp_task_wdt_reset():
 *    - Feeds the watchdog timer to confirm the loop is still alive.
 ****************************************************************************************************************************/
void loop() {
  server.handleClient();
  ArduinoOTA.handle();
  esp_task_wdt_reset();
}