/****************************************************************************************************************************
  Remote Power/SWR Meter - a solution to remotely measure RF power and VSWR over ethernet

  For Ethernet shields using WT32_ETH01 (ESP32 + LAN8720)
  Uses WebServer_WT32_ETH01, a library for the Ethernet LAN8720 in WT32_ETH01 to run WebServer

  Author: Michael Clemens, DK1MI
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


/****************************************************************************************************************************
 *  SOFTWARE VERSIONING
 *
 *  version:
 *    Human-readable firmware version string used for UI display, debugging, and update tracking.
 ****************************************************************************************************************************/
String version = "1.0.2";


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
 *  RAW MEASUREMENT STORAGE BUFFERS
 *
 *  fwd_array / ref_array:
 *    Large circular/history buffers storing forward (FWD) and reflected (REF) RF measurements.
 *    Each entry represents a sampled ADC-derived value used for averaging, smoothing,
 *    or waveform-like visualization (VU meter behavior).
 *
 *  Size 3300:
 *    Indicates long-term sample history for smoothing and time-series analysis.
 ****************************************************************************************************************************/
double fwd_array[3300] = {};
double ref_array[3300] = {};


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
 * defined by the first and last non-zero entries in the lookup table.
 *
 * Behavior:
 * - Scans the selected lookup table (fwd_array or ref_array).
 * - Identifies the lowest and highest index positions containing valid data.
 * - Compares the input value against this valid range.
 *
 * Parameters:
 * mv (int)
 *   Measured millivolt value (used as index into calibration table).
 *
 * fwd (bool)
 *   Selects which dataset to use:
 *   true  -> forward power table (fwd_array)
 *   false -> reflected power table (ref_array)
 *
 * Returns:
 * bool
 *   false -> value is within calibrated bounds
 *   true  -> value is outside calibrated bounds
 *
 * Implementation details:
 * - A value of 0 in the table is treated as "unset / invalid entry".
 * - The function assumes that valid calibration data is continuous between bounds.
 **************************************************************************************************/
bool is_val_out_of_bounds(int mv, bool fwd) {
  double stored_val = 0;
  int key_a = 0;
  int key_b = 0;

  // searches for the first key (voltage) that has a value (dBm)
  for (int i = 0; i < 3300; i++) {
    if (fwd) {
      stored_val = fwd_array[i];
    } else {
      stored_val = ref_array[i];
    }
    if (stored_val != 0) {
      key_a = i;
      break;
    }
  }

  // searches for the last key (voltage) that has a value (dBm)
  for (int i = 3299; i > 0; i--) {
    if (fwd) {
      stored_val = fwd_array[i];
    } else {
      stored_val = ref_array[i];
    }
    if (stored_val != 0) {
      key_b = i;
      break;
    }
  }

  int lowerkey = min(key_a, key_b);   // takes both values found above and assigns the lower key
  int higherkey = max(key_a, key_b);  // takes both values found above and assigns the higher key

  // returns false if given voltage is between the lowest and highest configured voltages
  // returns true if voltage is out of bounds
  if (lowerkey <= mv and mv <= higherkey)
    return false;
  else {
    return true;
  }
}


/**************************************************************************************************
 * Function: millivolt_to_dbm
 *
 * Purpose:
 * Converts a raw millivolt (ADC index) value into a corresponding dBm value
 * using a lookup table with linear interpolation between adjacent entries.
 *
 * Core concept:
 * The system stores calibration data as a mapping:
 *   millivolt index -> measured dBm value
 *
 * This function:
 * 1. Determines whether the table is ascending or descending.
 * 2. Finds the two nearest calibration points surrounding the input value.
 * 3. Prepares values for interpolation (actual interpolation occurs later in code).
 *
 * Parameters:
 * mv (int)
 *   Input millivolt index to convert.
 *
 * fwd (bool)
 *   Selects dataset:
 *   true  -> forward power table (fwd_array)
 *   false -> reflected power table (ref_array)
 *
 * Returns:
 * double
 *   Corresponding dBm value (interpolated in subsequent logic).
 *
 * Important internal variables:
 * lastval / nextval
 *   Neighboring calibration dBm values used for interpolation.
 *
 * lastkey / nextkey
 *   Corresponding indices in the lookup table.
 *
 * ascending
 *   Indicates whether calibration data increases or decreases with index.
 *
 * Notes:
 * - Zero entries are treated as invalid/uninitialized table slots.
 * - Table is assumed to contain monotonic or near-monotonic calibration data.
 * - Performance is O(n) due to full table scan.
 **************************************************************************************************/
double millivolt_to_dbm(int mv, bool fwd) {
  double lastval = 0;
  double nextval = 0;
  int lastkey = 0;
  int nextkey = 0;
  double stored_val = 0;
  bool ascending = true;

  int lowest_key_in_table = 0;
  int highest_key_in_table = 0;

  // check if table is ascending or descending
  double asc_tmp_val = 0;
  for (int i = 0; i < 3300; i++) {
    if (fwd) {
      stored_val = fwd_array[i];
    } else {
      stored_val = ref_array[i];
    }
    if (stored_val != 0) {
      if (asc_tmp_val == 0) {
        asc_tmp_val = stored_val;
      } else if (stored_val > asc_tmp_val) {
        ascending = true;
        break;
      } else if (stored_val < asc_tmp_val) {
        ascending = false;
        break;
      }
    }
  }
  // checks if the voltage values are opposite to the dBm values or
  // if both, voltage and dBm values are ascending
  if (ascending) {
    for (int i = 0; i < 3300; i++) {
      if (fwd) {
        stored_val = fwd_array[i];
      } else {
        stored_val = ref_array[i];
      }
      if (stored_val != 0) {
        if (lowest_key_in_table == 0) {
          lowest_key_in_table = i;  //finds the lowest voltage value stored in the table
        }
        highest_key_in_table = i;  // we will have the highest voltage value in the table at the end of the loop
        if (i < mv) {
          lastval = stored_val;
          lastkey = i;
        } else {
          nextval = stored_val;
          nextkey = i;
          break;
        }
      }
    }
  } else {
    for (int i = 3300; i > 0; i--) {
      if (fwd) {
        stored_val = fwd_array[i];
      } else {
        stored_val = ref_array[i];
      }
      if (stored_val != 0) {
        if (lowest_key_in_table == 0) {
          lowest_key_in_table = i;  //finds the lowest voltage value stored in the table
        }
        highest_key_in_table = i;  // we will have the highest voltage value in the table at the end of the loop
        if (i > mv) {
          lastval = stored_val;
          lastkey = i;
        } else {
          nextval = stored_val;
          nextkey = i;
          break;
        }
      }
    }
  }
}

/****************************************************************************************************************************
 *  LINEAR INTERPOLATION AND VALUE MAPPING LOGIC
 *
 *  This function performs piecewise linear interpolation between two calibration points.
 *  It is typically used in RF measurement systems to convert raw ADC millivolt readings
 *  into calibrated physical units (e.g., dBm).
 *
 *  INPUT VARIABLES (implicit from surrounding scope):
 *    lastkey  -> lower calibration key (e.g., lower mV reference point)
 *    nextkey  -> upper calibration key
 *    lastval  -> calibrated value corresponding to lastkey
 *    nextval  -> calibrated value corresponding to nextkey
 *    mv       -> current measured millivolt value to be mapped
 *    ascending-> direction flag indicating monotonicity of calibration curve
 *
 *  COMPUTATION STEPS:
 *
 *  1. Normalize key/value ordering:
 *     lowerkey  = min(lastkey, nextkey)
 *     higherkey = max(lastkey, nextkey)
 *     lowerval  = min(lastval, nextval)
 *     higherval = max(lastval, nextval)
 *
 *     This ensures robustness regardless of whether calibration points are stored
 *     in ascending or descending order.
 *
 *  2. Compute deltas:
 *     diffkey = |nextkey - lastkey|
 *     diffval = |nextval - lastval|
 *
 *     These define the slope of the calibration segment.
 *
 *  3. Linear interpolation:
 *     If ascending == true:
 *         result = lowerval + slope * (mv - lowerkey)
 *     Else:
 *         result = higherval - slope * (mv - lowerkey)
 *
 *     Where slope = diffval / diffkey
 *
 *  4. Return:
 *     The interpolated calibrated value corresponding to input mv.
 *
 *  NOTE:
 *    This is effectively a piecewise linear transfer function commonly used
 *    in sensor calibration curves for RF power detection.
 ****************************************************************************************************************************/
double lowerkey = min(lastkey, nextkey);
double higherkey = max(lastkey, nextkey);

double lowerval = min(lastval, nextval);
double higherval = max(lastval, nextval);

double diffkey = max(lastkey, nextkey) - min(lastkey, nextkey);
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
  }

  if (config.getString(String("s_power_avg").c_str()) == "true") {
    // calculate the average value by deviding the above sum by 50
    voltage_fwd = voltage_sum_fwd / 50;
    voltage_ref = voltage_sum_ref / 50;
  } else {
    // take the highest value of the 50 samples
    voltage_fwd = voltage_fwd_max;
    voltage_ref = voltage_ref_max;
  }

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
void handleRoot() {
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
  rl_str.replace("nan", "-- ");

  String antenna_name = config.getString(String("s_ant_name").c_str());
  String vswr_beep = config.getString(String("b_vswr_beep").c_str());

  bool fwd_oob = is_val_out_of_bounds(voltage_fwd, true);
  bool ref_oob = is_val_out_of_bounds(voltage_ref, false);
}

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


// Generate a semicolon seperated string that will be sent to the frontend
String output = fwd_watt_str + ";";                                   // data[0]: FWD power in Watt
// Appends forward power in watts as the first telemetry field. This is a derived value
// computed from RF detector measurements and formatted as a string for transmission.

output += fwd_dbm_str + ";";                                          // data[1]: FWD dBm value
// Appends forward power expressed in dBm. This is a logarithmic representation of RF power,
// typically derived from voltage readings via calibration curves.

output += voltage_fwd_str + ";";                                      // data[2]: FWD voltage
// Raw or scaled ADC voltage corresponding to the forward RF detector input channel.

output += ref_watt_str + ";";                                         // data[3]: REF power in Watt
// Reflected power expressed in watts, representing power not absorbed by the load.

output += ref_dbm_str + ";";                                          // data[4]: REF dBm value
// Reflected RF power in dBm, providing logarithmic scaling for reflected signal strength.

output += voltage_ref_str + ";";                                      // data[5]: REF voltage
// ADC voltage corresponding to the reflected RF detector input channel.

output += vswr_str + ";";                                             // data[6]: VSWR value
// Voltage Standing Wave Ratio (VSWR), computed from forward and reflected power ratio.
// Indicates impedance matching quality of the transmission line.

output += rl_str + ";";                                               // data[7]: RL value
// Return Loss (RL) in dB, derived from reflection coefficient.
// Higher values indicate better impedance matching.

output += band + ";";                                                 // data[8]: band (e.g. "70cm")
// Current operating frequency band identifier used for labeling and configuration scope.

output += String(vswr_threshold) + ";";                               // data[9]: VSWR threshold (e.g. "3")
// Configured alarm threshold for VSWR. Values above this trigger warning behavior.

output += antenna_name + ";";                                         // data[10]: Name of antenna (e.g. "X200")
// User-defined antenna identifier used for UI display and configuration context.

output += vswr_beep + ";";                                            // data[11]: should it beep if VSWR is too high? (true/false)
// Boolean flag controlling audible alarm behavior when VSWR exceeds threshold.

output += config.getString(String("s_max_led_pwr_f").c_str()) + ";";  // data[12]: highest value in Watt for the FWD LED graph (e.g. "100")
// Upper bound scaling parameter for forward power LED bar visualization.

output += config.getString(String("s_max_led_pwr_r").c_str()) + ";";  // data[13]: highest value in Watt for the REF LED graph (e.g. "1")
// Upper bound scaling parameter for reflected power LED bar visualization.

output += config.getString(String("s_max_led_vswr").c_str()) + ";";   // data[14]: highest value in Watt for the VSWR LED graph (e.g. "3")
// Upper bound scaling parameter for VSWR LED visualization scaling.

output += String(fwd_oob) + ";";                                      // data[15]: Is the FWD voltage out of bounds? (true/false)
// Status flag indicating whether forward detector input is outside calibrated or safe range.

output += String(ref_oob) + ";";                                      // data[16]: Is the REF voltage out of bounds? (true/false)
// Status flag indicating whether reflected detector input is outside calibrated or safe range.

output += config.getString(String("b_show_led_fwd").c_str()) + ";";   // data[17]: Show the FWD LED bar graph? (true/false)
// UI configuration flag controlling visibility of forward power LED bar graph.

output += config.getString(String("b_show_led_ref").c_str()) + ";";   // data[18]: Show the REF LED bar graph? (true/false)
// UI configuration flag controlling visibility of reflected power LED bar graph.

output += config.getString(String("b_show_led_vswr").c_str()) + ";";  // data[19]: Show the VSWR LED bar graph? (true/false)
// UI configuration flag controlling visibility of VSWR LED bar graph.

output += version + ";";                                              // data[20]: program version
// Firmware version string used for UI synchronization and diagnostic traceability.

output += getTemp();                                                  // data[21]: temperature
// Appends current temperature reading from the attached sensor subsystem.
// This is the final field and intentionally not followed by a semicolon
// to terminate the payload string cleanly.

server.send(200, "text/plane", output);
// Sends the constructed semicolon-delimited payload to the HTTP client.
// MIME type "text/plane" (note: non-standard spelling) is used for lightweight plaintext transmission.

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
 *         Resets global calibration arrays (fwd_array, ref_array) to a known empty state.
 *
 *    3. save_string_to_array(...)
 *         Parses the loaded calibration strings and populates the numeric arrays:
 *           - fwd_array[] for forward calibration curve
 *           - ref_array[] for reflected calibration curve
 *
 *    4. HTML generation loop
 *         Iterates over the full fixed-size arrays and formats each non-zero entry as:
 *           "index:value"
 *         appended line-by-line into HTML textarea elements.
 *
 *  IMPORTANT IMPLEMENTATION DETAIL
 *    - The loop uses:
 *        sizeof fwd_array / sizeof fwd_array[0]
 *      to determine array length at compile time.
 *    - Only non-zero values are rendered, meaning sparse calibration entries are allowed.
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
 *    - Array size is fixed (FWD/REF buffers are large, ~3300 entries),
 *      so HTML generation may be moderately expensive in embedded context.
 *    - No streaming output is used; full HTML is constructed in RAM as a String.
 *
 *  SIDE EFFECTS
 *    - Reads from SPIFFS filesystem
 *    - Mutates global arrays: fwd_array, ref_array
 *    - Mutates global UI state: conf_textareas
 *
 ****************************************************************************************************************************/
void build_textareas() {
  String fwd = readFile(SPIFFS, String("/" + band + "fwd.txt").c_str());
  String ref = readFile(SPIFFS, String("/" + band + "ref.txt").c_str());

  clear_fwd_ref_array();

  save_string_to_array(fwd, fwd_array);
  save_string_to_array(ref, ref_array);

  String tbl = "<form action=\"/modcal\" method=\"POST\">";
  tbl += "<table class='styled-table'>";
  tbl += "<thead><tr><td>" + band + " FWD (mV:dBm)</td><td>" + band + " REF (mV:dBm)</td></tr></thead>";
  tbl += "<tr><td>";
  tbl += "<textarea id='fwd_textarea' name='fwd_textarea' rows='22'>";
  for (int i = 0; i < sizeof fwd_array / sizeof fwd_array[0]; i++) {
    if (fwd_array[i] != 0) {
      tbl += String(i) + ":" + String(fwd_array[i], 5) + "\n";
    }
  }
  tbl += "</textarea>";
  tbl += "</td><td>";
  tbl += "<textarea id='ref_textarea' name='ref_textarea' rows='22'>";
  for (int i = 0; i < sizeof ref_array / sizeof ref_array[0]; i++) {
    if (ref_array[i] != 0) {
      tbl += String(i) + ":" + String(ref_array[i], 5) + "\n";
    }
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
 *  7. Calls `handleCONFIG()` to process or render the generated configuration view.
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
 *  - Triggers `handleCONFIG()` at the end, which likely controls HTTP response flow.
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
  handleCONFIG();
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
 *    - Internal runtime calibration arrays (fwd_array / ref_array)
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
 *     - save_string_to_array() parses the textual calibration input
 *       and writes numeric values into:
 *         fwd_array[]
 *         ref_array[]
 *
 *  4. SERIALIZATION FOR PERSISTENCE (FWD)
 *     - The forward array is serialized into a string format:
 *         "index:value"
 *       one entry per line.
 *     - Only non-zero values are included to reduce storage usage.
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
 *  - Zero-valued entries are treated as "empty" and are not persisted.
 *  - Calibration resolution and accuracy depend on save_string_to_array() parsing logic.
 *  - The function assumes global state: `band`, `fwd_array`, `ref_array`, and `server`.
 *
 *  SIDE EFFECTS
 *  ------------
 *  - Modifies global arrays: fwd_array, ref_array
 *  - Writes files to SPIFFS filesystem
 *  - Triggers UI regeneration and config page reload
 *  - Performs full replacement of calibration dataset for the active band
 *
 ****************************************************************************************************************************/
void handleMODCAL() {
  String fwd = server.arg("fwd_textarea") + "\n";
  String ref = server.arg("ref_textarea") + "\n";
  clear_fwd_ref_array();
  save_string_to_array(fwd, fwd_array);
  save_string_to_array(ref, ref_array);

  String fwd_of_array = "";
  for (int i = 0; i < sizeof fwd_array / sizeof fwd_array[0]; i++) {
    if (fwd_array[i] != 0) {
      fwd_of_array += String(i) + ":" + String(fwd_array[i], 5) + "\n";
    }
  }
  writeFile(SPIFFS, String("/" + band + "fwd.txt").c_str(), fwd_of_array.c_str());

  String ref_of_array = "";
  for (int i = 0; i < sizeof ref_array / sizeof ref_array[0]; i++) {
    if (ref_array[i] != 0) {
      ref_of_array += String(i) + ":" + String(ref_array[i], 5) + "\n";
    }
  }
  writeFile(SPIFFS, String("/" + band + "ref.txt").c_str(), ref_of_array.c_str());

  build_textareas();
  handleCONFIG();
}

/****************************************************************************************************************************
 *  FUNCTION: clear_fwd_ref_array
 *
 *  PURPOSE:
 *    Resets the complete history buffers for forward (FWD) and reflected (REF) RF measurements.
 *
 *  BEHAVIOR:
 *    - Iterates over both global arrays fwd_array[] and ref_array[].
 *    - Sets every element in both arrays to 0.0.
 *    - Effectively clears all stored measurement history data.
 *
 *  TECHNICAL DETAILS:
 *    - Array length is derived at runtime using:
 *        sizeof(fwd_array) / sizeof(fwd_array[0])
 *      This ensures the loop automatically matches the declared buffer size.
 *
 *    - Both arrays are assumed to be parallel structures:
 *        fwd_array[x] → forward power sample at index x
 *        ref_array[x] → reflected power sample at same index x
 *
 *    - No bounds checking is required because iteration is strictly limited
 *      to the compile-time known array size.
 *
 *  SIDE EFFECTS:
 *    - All previously recorded RF measurement data is permanently lost.
 *    - Any running averaging, smoothing, or visualization logic depending on
 *      these buffers will immediately reset to zero state.
 *
 *  TYPICAL USE CASES:
 *    - Device reset or reinitialization
 *    - Band change or calibration reset
 *    - User-triggered "clear history" action in web interface
 *****************************************************************************************************************************/
void clear_fwd_ref_array() {
  for (int x = 0; x < sizeof(fwd_array) / sizeof(fwd_array[0]); x++) {
    fwd_array[x] = 0;
    ref_array[x] = 0;
  }
}

/****************************************************************************************************************************
 *  save_string_to_array
 *
 *  PURPOSE
 *  This function parses a structured text representation of a calibration table (received from the frontend UI)
 *  and converts it into a numeric lookup array of type double.
 *
 *  INPUT FORMAT ASSUMPTION
 *  The function expects `table_data` to contain multiple rows separated by newline characters ('\n').
 *  Each row is expected to contain one or more key-value pairs in the format:
 *
 *      key:value
 *
 *  where:
 *      - key   is an integer index used as the array position
 *      - value is a floating-point number stored at arr[key]
 *
 *  Multiple key-value pairs may exist per row, separated by newline or repeated delimiters.
 *
 *  PARAMETERS
 *  table_data
 *      A String containing the serialized calibration table coming from the web frontend.
 *
 *  arr
 *      Target array of type double where parsed values are stored.
 *      The index of each value is determined by the parsed integer key.
 *
 *  INTERNAL VARIABLES
 *  r
 *      Start index of the current row within the input string.
 *
 *  t
 *      Row counter (currently unused for logic; may have been intended for diagnostics or debugging).
 *
 *  i
 *      Iteration index over the entire input string.
 *
 *  row
 *      Substring representing a single line (row) extracted from the input.
 *
 *  r2
 *      Start index within a row for parsing key-value segments.
 *
 *  t2
 *      Counter for parsed key-value pairs per row (not used outside function scope).
 *
 *  key
 *      Integer index extracted from the substring before ':'.
 *      Used directly as array index into `arr`.
 *
 *  val
 *      Floating-point value extracted from substring after ':'.
 *      Stored into arr[key].
 *
 *  OPERATIONAL BEHAVIOR
 *  1. The function scans the full input string character by character.
 *  2. Each newline character indicates the end of a row.
 *  3. Each row is extracted via substring(r, i).
 *  4. Within each row, the function scans for ':' delimiters.
 *  5. For each "key:value" pair:
 *         - key is parsed as integer
 *         - value is parsed as double
 *         - arr[key] is assigned value
 *
 *  EDGE CASES / IMPLICIT BEHAVIOR
 *  - Empty rows or rows with length <= 1 are ignored.
 *  - No bounds checking is performed on `key`, so invalid indices may corrupt memory.
 *  - Malformed numeric strings will be converted to 0 by toInt()/toDouble().
 *  - The condition `i == table_data.length()` inside the loop is logically unreachable
 *    because loop termination occurs at i < length; it remains harmless but redundant.
 *
 *  COMPLEXITY
 *  Time complexity: O(n * m) in worst case (n = input length, m = average row length)
 *  Space complexity: O(1) additional (aside from temporary String objects created via substring)
 ****************************************************************************************************************************/
void save_string_to_array(String table_data, double arr[]) {
  int r = 0, t = 0;
  for (int i = 0; i < table_data.length(); i++) {
    if (table_data[i] == '\n' || i == table_data.length()) {
      if (i - r > 1) {
        String row = table_data.substring(r, i);
        t++;
        int r2 = 0, t2 = 0;
        for (int j = 0; j < row.length(); j++) {
          if (row[j] == ':' || row[j] == '\n') {
            if (j - r2 > 1) {
              int key = row.substring(r2, j).toInt();
              double val = row.substring(j + 1).toDouble();
              arr[key] = val;
              t2++;
            }
            r2 = (j + 1);
          }
        }
      }
      r = (i + 1);
    }
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
//   - build_config_table() is called to regenerate the UI representation
//     based on updated persistent values.
//
// Complexity:
//   - O(n) over number of configuration keys, where n = sizeof(band_config_items).
// =============================================================================
void handleMODCFG() {
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
  band = server.arg("bands");
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
 *    - sensors.requestTemperatures()
 *        Triggers a blocking temperature conversion on the OneWire bus.
 *        All connected sensors perform a measurement cycle.
 *
 *    - sensors.getTempCByIndex(0)
 *        Reads the first detected sensor in Celsius.
 *
 *    - sensors.getTempFByIndex(0)
 *        Reads the first detected sensor in Fahrenheit.
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
    String label = "Temp: ";
    String temp_string = "--";
    String unit = "";
    float temp_float;
    sensors.requestTemperatures();
    if (config.getString(String("b_celsius").c_str()) != "false") {
      unit = "°C";
      temp_float = sensors.getTempCByIndex(0);
    } else {
      unit = "°F";
      temp_float = sensors.getTempFByIndex(0);
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

  /**********************************************************************************************************************
   * SPIFFS FILESYSTEM INITIALIZATION
   *
   * Mounts internal flash filesystem used for serving static assets
   * (HTML/CSS/JS files, configuration resources).
   *
   * FORMAT_SPIFFS_IF_FAILED:
   *  If mounting fails, filesystem may be reformatted automatically depending on config.
   **********************************************************************************************************************/
  if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }

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
   * ADC CONFIGURATION
   *
   * Sets ADC resolution to 12-bit:
   * - Range: 0–4095
   * - Required for consistent RF detector sampling accuracy
   **********************************************************************************************************************/
  analogReadResolution(12);

  /**********************************************************************************************************************
   * PERSISTENT CONFIGURATION INITIALIZATION (GLOBAL NVS)
   *
   * Opens NVS namespace "config" for reading/writing system-wide settings.
   * Retrieves selected frequency band from non-volatile storage.
   **********************************************************************************************************************/
  global_config.begin("config", false);
  band = global_config.getString(String("x_selected_band").c_str());

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
   * UI CONFIGURATION RENDERING
   *
   * Builds HTML form elements (textareas, tables, etc.) representing
   * the current configuration state for the web interface.
   **********************************************************************************************************************/
  build_textareas();
}


/****************************************************************************************************************************
 *  MAIN EXECUTION LOOP
 *
 *  This function runs continuously after setup() completes.
 *  Its primary responsibility is to process incoming HTTP client requests.
 *
 *  server.handleClient():
 *    - Checks for incoming TCP connections
 *    - Parses HTTP requests
 *    - Dispatches registered route handlers
 *    - Maintains web UI responsiveness
 *
 *  The loop is intentionally lightweight because the ESP32 web server is event-driven.
 ****************************************************************************************************************************/
void loop() {
  server.handleClient();
}