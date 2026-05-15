/****************************************************************************************************************************
  Remote Power/SWR Meter - a solution to remotely measure RF power and VSWR over ethernet

  For Ethernet shields using WT32_ETH01 (ESP32 + LAN8720)
  Uses WebServer_WT32_ETH01, a library for the Ethernet LAN8720 in WT32_ETH01 to run WebServer

  Author: Michael Clemens, DK1MI
  Licensed under GPLv3 license (see LICENSE.md)

  VU meter code was taken from https://github.com/tomnomnom/vumeter, credits go to Tom Hudson (https://github.com/tomnomnom)
  
 *****************************************************************************************************************************/

#define DEBUG_ETHERNET_WEBSERVER_PORT Serial

// Debug Level from 0 to 4
#define _ETHERNET_WEBSERVER_LOGLEVEL_ 3

#define FORMAT_SPIFFS_IF_FAILED true


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

String version = "1.0.2";

Preferences config;
Preferences global_config;

String band_config_items[] = { "b_show_mV", "b_show_dBm", "b_show_watt", "s_vswr_thresh", "b_vswr_beep", "s_ant_name", "s_max_led_pwr_f", "s_max_led_pwr_r", "s_max_led_vswr", "b_show_led_fwd", "b_show_led_ref", "b_show_led_vswr", "s_cable_loss", "b_power_avg", "b_show_temp", "b_celsius" };
String band_config_defaults[] = { "true", "true", "true", "2", "true", " ", "100", "100", "3", "true", "true", "true", "0", "false", "false", "true" };
String band_config_nice_names[] = { "Show voltage in mV (yes/no)", "Show power level in dBm (yes/no)", "Show power in Watt (yes/no)", "VSWR threshold that triggers a warning (e.g. 3)", "Beep if VSWR threshold is exceeded (yes/no)", "Name of the antenna", "Max. FWD power displayed by LED bar graph in W (e.g. 100)", "Max. REF power displayed by LED bar graph in W (e.g. 100)", "Max. VSWR displayed by LED bar graph (e.g. 3)", "Show LED graph for FWD power (yes/no)", "Show LED graph for REF power (yes/no)", "Show LED graph for VSWR (yes/no)","Cable loss in db (e.g. 3)","Display average power instead of PEP? (yes=AVG/no=PEP)", "Display Temperature? (yes/no)", "Display Temperature in C or F? (yes=C/no=F)" };

double fwd_array[3300] = {};
double ref_array[3300] = {};

int voltage_fwd, voltage_ref;
double fwd_dbm = 0, ref_dbm = 0;
double fwd_watt = 0, ref_watt = 0;
byte iii = 0;

String conf_content;
String conf_textareas = "";
String conf_config_table = "";

String band = "";
String default_band = "70cm";
String band_fwd = band + "_fwd";
String band_ref = band + "_ref";
String band_list[] = { "1.25cm", "3cm", "6cm", "9cm", "13cm", "23cm", "70cm", "2m", "HF" };

int IO2_FWD = 2;
int IO4_REF = 4;

const int IO14_TEMP = 14;

OneWire oneWire(IO14_TEMP);
DallasTemperature sensors(&oneWire);

WebServer server(80);

// Select the IP address according to your local network
IPAddress myIP(192, 168, 2, 198);
IPAddress myGW(192, 168, 2, 1);
IPAddress mySN(255, 255, 255, 0);
IPAddress myDNS(192, 168, 2, 1);


// Reads a file from SPIFF and returns its content as a string
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

// Takes a string and writes it to a file on SPIFF
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


// converts dBm to Watt
double dbm_to_watt(double dbm) {
  return pow(10.0, (dbm - 30.0) / 10.0);
}

// checks if a given voltage is lower as the smallest value
// in the table or higher than the biggest value
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

// takes a voltage value and translates it
// to dBm based on the corresponding lookup table
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


// read voltages from both input pins
// calculates avaerage value of 50 measurements
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

// delivers the dashboard page in "index.h"
void handleRoot() {
  String html = MAIN_page;
  String css = DB_STYLESHEET;
  String js = JAVASCRIPT;
  server.send(200, "text/html", css + js + html);
}


// delivers a 404 page if a non-existant resouurce is requested
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


// executes the function to gather sensor data
// delivers gathered data to dashboard page
// invoked periodically by the dashboard page
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

  // Generate a semicolon seperated string that will be sent to the frontend
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
  output += getTemp();                                                  // data[21]: temperature
  server.send(200, "text/plane", output);
}

// main function for displaying the configuration page
// invoked by the "configuration" button on the dashboard page
void handleCONFIG() {
  if (conf_textareas == "") {
    build_textareas();
  }

  if (conf_config_table == "") {
    build_config_table();
  }

  String css = CFG_STYLESHEET;
  conf_content = css;

  conf_content += "<div class='grid-container'>";
  conf_content += "<div id='title_box' class='titlebox maintitlebox'>";
  conf_content += "Configuration</div>";
  conf_content += "<div id='title_box' class='bandbox maintitlebox'>";
  conf_content += "<form method='POST' action='/selectband'>";
  conf_content += "Band: <label for='bands'></label><select class='button' onchange='this.form.submit()'' id='band' name='bands' size='1'>";
  for (int i = 0; i < sizeof band_list / sizeof band_list[0]; i++) {
    String selected = "";
    if (band_list[i] == band) {
      selected = "selected";
    }
    conf_content += "<option value='" + band_list[i] + "' " + selected + " >" + band_list[i] + "</option>";
  }
  conf_content += "</select></form>";
  conf_content += "</div>";
  conf_content += "<div class='subtitle1 subtitlebox'>Translation Detector Voltage /mV to RF-Power Level /dBm</div>";

  conf_content += "<div class='translationitems contentbox'>";
  conf_content += conf_textareas;
  conf_content += "</div>";
  conf_content += "<div class='subtitle2 subtitlebox'>General Configuration Items</div>";
  conf_content += "<div class='configitems contentbox'>";
  conf_content += conf_config_table;
  conf_content += "</div>";
  conf_content += "<div class='footerbox'>";
  conf_content += "<form method='POST' action='/'><button class='linkbutton' value='back' name='back' type='submit'>Back to Dashboard</button> - Version: " + version + " </form>";
  conf_content += "</div>";
  conf_content += "</div>";
  conf_content += "</html>";
  server.send(200, "text/html", conf_content);
}


// generates the translation table for either the FWD or
// REF values
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


// generates the table with generic configuration items
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

// Handle request from the config page to change or add values
// to the XXXX value table for the selected band
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

// resets all vlaues in the FWD and REF array
void clear_fwd_ref_array() {
  for (int x = 0; x < sizeof(fwd_array) / sizeof(fwd_array[0]); x++) {
    fwd_array[x] = 0;
    ref_array[x] = 0;
  }
}

// after the user edited the calibration table via the frontend,
// this function takes the resulting string, detonates it and
// writes all rows into an array
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

// Handle request from the config page to change or add values
// to the general config value table for the selected band
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

// changes the band according to the user's selection
// regenerates the calibration tables and fills them
// with the values assigned to the respective band
// invoked by selecting a band from the select box of the config page
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


// reads the temperature from the aboce defined sensor
// and returns it in either C or F as a string
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

// initialization routine
void setup() {

  Serial.begin(115200);

  // initialize temperature sensor
  sensors.begin();


  Serial.print("\nStarting AdvancedWebServer on " + String(ARDUINO_BOARD));
  Serial.println(" with " + String(SHIELD_TYPE));
  Serial.println(WEBSERVER_WT32_ETH01_VERSION);

  // To be called before ETH.begin()
  WT32_ETH01_onEvent();

  ETH.begin(ETH_PHY_ADDR, ETH_PHY_POWER);

  // Static IP, leave without this line to get IP via DHCP
  ETH.config(myIP, myGW, mySN, myDNS);

  WT32_ETH01_waitForConnect();

  // activates single web server endpoints
  server.on(F("/"), handleRoot);
  server.on("/readDATA", handleDATA);
  server.on("/config", handleCONFIG);
  server.on("/modcfg", handleMODCFG);
  server.on("/selectband", handleBAND);
  server.on("/modcal", handleMODCAL);


  if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }

  server.onNotFound(handleNotFound);
  server.begin();

  Serial.print(F("HTTP EthernetWebServer is @ IP : "));
  Serial.println(ETH.localIP());

  analogReadResolution(12);

  global_config.begin("config", false);
  band = global_config.getString(String("x_selected_band").c_str());
  if (band == "") {
    global_config.putString(String("x_selected_band").c_str(), default_band);
    band = default_band;
  }
  String bnd_cnf = "config_" + band;
  config.begin(bnd_cnf.c_str(), false);

  build_textareas();
}

void loop() {
  server.handleClient();
}