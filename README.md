# Remote Power/SWR Meter

This repository is a fork of DK1MI’s original project. It was cloned here for preservation and personal use. The original authorship and credit remain with DK1MI.

This WT32/ESP32-based project, combined with a directional coupler setup, allows you to remotely monitor the output power and SWR of your station via a web browser.

To achieve this, it reads two voltages supplied by the directional couplers. From these, the respective power is calculated with the help of a table created by the user.

Special thanks to Matthias DD1US for support with the directional coupler setup, software testing, and ideas for additional features.

## Changelog

## v1.0.2

- Added rudimentary PEP implementation for testing
  - Can be enabled or disabled via the config page
- Added temperature reading
  - Requires a DS18B20 sensor on pin IO14
  - Temperature display can be enabled or disabled via the config page
  - Display in Celsius or Fahrenheit can be toggled via the config page
  - Requires the installation of the following libraries:
    - OneWire
    - DallasTemperature

## Prerequisites

### Hardware

The following hardware is required for this project:

- WT32-ETH01 development board
- USB-to-serial adapter (FTDI)
- Directional coupler setup that outputs one voltage between 0 and 3.3 V each for forward and reflected power

Matthias DD1US uses the following components in his implementations, among others:

- Ericsson coupler + AD8318 detector
- Narda coupler + AD8313 detector

My own implementation uses the directional coupler from an old SWR/power meter.

The wiring was done according to the picture shown further below.

### Software / Libraries

- Arduino IDE
- Project code: https://github.com/dl3hc/wt32powermeter
- Libraries:
  - WebServer_WT32_ETH01: https://github.com/dl3hc/WebServer_WT32_ETH01-fork
  - DallasTemperature
  - OneWire

## Downloading and Setting Up the Arduino IDE

The following steps are required to compile and upload the code to the board:

1. Download and install Arduino IDE 2.1:  
   https://wiki-content.arduino.cc/en/software

2. Follow this guide to install the ESP32 board definitions:  
   https://randomnerdtutorials.com/installing-esp32-arduino-ide-2-0/

3. Select the correct board in the Arduino IDE:  
   **Tools → Board → esp32 → ESP32 Dev Module**

4. Install all required libraries:  
   **Tools → Manage Libraries →** search for **WebServer_WT32_ETH01** and install it

## Downloading the Software

1. Download the code from the repository:  
   `git clone https://github.com/dl3hc/wt32powermeter`

2. Open the file `wt32powermeter.ino` in the Arduino IDE or start it by double-clicking it.

---

## Programming the Board

1. Connect the board to a USB-to-serial adapter as shown in the picture below.

![Programming](docs/remote-power-meter-8.png)

2. Select the correct COM port in the Arduino IDE:  
   **Tools → Port → Select Port**

3. Click **Upload** (top left, arrow pointing to the right).

---

## Connecting the Board to the Directional Coupler

The two pins **IO0** and **GND** only need to be bridged during programming. Remove the bridge again after programming.

![Connection](docs/remote-power-meter-9.png)

---

## Configuration

The following code blocks in `wt32powermeter.ino` can be adapted to your needs.

### Network Configuration

```cpp
ETH.begin(ETH_PHY_ADDR, ETH_PHY_POWER);

// Static IP, leave without this line to get IP via DHCP
//ETH.config(myIP, myGW, mySN, myDNS);

WT32_ETH01_waitForConnect();
````

By default, `wt32powermeter` is configured to obtain an IP address automatically via DHCP. If that is what you want, no changes are necessary.

If you want to use a static IP, activate the following line by removing the two leading slashes:

```cpp
//ETH.config(myIP, myGW, mySN, myDNS);
```

Define the desired network configuration in the following block:

```cpp
// Select the IP address according to your local network
IPAddress myIP(192, 168, 1, 100);
IPAddress myGW(192, 168, 1, 1);
IPAddress mySN(255, 255, 255, 0);
IPAddress myDNS(192, 168, 1, 1);
```

---

## Selectable Bands

To add or remove bands, adjust the following code block:

```cpp
String band = "";
String default_band = "70cm";
String band_fwd = band + "_fwd";
String band_ref = band + "_ref";
String band_list[] = { "1.25cm", "3cm", "6cm", "9cm", "13cm", "23cm", "70cm", "2m", "HF" };
```

* Extend or shorten `band_list[]` as needed
* Set `default_band` to the desired default band

---

## Accessing the Web Interface

Open the following address in your preferred browser:

```text
http://YOUR_IP_ADDRESS
```

Example:

```text
http://192.168.1.100
```

The IP address is either the one defined in the code or one assigned dynamically via DHCP. If DHCP is used, you can find the address in your router dashboard under the connected network devices.

---

## Usage

The first step is to configure the connected directional couplers. To do this, click **Configuration** in the left footer of the page.

![Usage](docs/remote-power-meter-10.png)

---

## Calibration Data

First, select the band you want to configure using the dropdown menu labeled **Band** in the top right.

The actual calibration values shown here are not important; they were used only for debugging purposes. For your own setup, you need to determine suitable values. Enter the `mV:dBm` value pairs for **FWD** and **REF** here, then click **Save Calibration Data**.

You can enter the values in any order; manual sorting is not necessary. After saving, the data will be sorted automatically and displayed correctly.

### How the calibration was performed in my setup

1. The components were connected as follows:
   **IC-7300 → Remote Power Meter → known-good power meter → dummy load**

2. Set the band to **20m** and the mode to **FM**.

3. Set the power to **1 W**, pressed PTT, noted the measured power shown by the reference power meter and the voltage measured by the Remote Power Meter. These values were used for the **FWD** calibration table.

4. Repeated the previous step until maximum power was reached.

5. Then the Remote Power Meter was removed from the chain, turned around, and inserted again.

6. Measured again at **1 W**, pressed PTT, noted power and voltage. These values were used for the **REF** calibration table.

7. The dBm values for each entry were calculated from the notes and each line was replaced with the format
   `voltage:dBm`

8. Both tables were then pasted into the Remote Power Meter configuration page.

---

## General Configuration Options

The following general configuration parameters are available to customize the application:

* **Show voltage in mV (yes/no):**
  Displays the measured voltage in millivolts or hides it.

* **Show power level in dBm (yes/no):**
  Displays the power level in dBm or hides it.

* **Show power in watts (yes/no):**
  Displays the power in watts or hides it.

* **VSWR threshold that triggers a warning (e.g. 3):**
  Any calculated value above the configured threshold triggers a visual and optionally audible warning.

* **Beep if VSWR threshold is exceeded (yes/no):**
  Emits a beep when the threshold configured above is exceeded. This only works in some browsers.

* **Name of the antenna:**
  Freely definable name of the antenna for this band.

* **Max. FWD power displayed by LED bar graph in W (e.g. 100):**
  Sets the upper limit for the FWD power display in the LED bar graph.

* **Max. REF power displayed by LED bar graph in W (e.g. 100):**
  Sets the upper limit for the REF power display in the LED bar graph.

* **Max. VSWR displayed by LED bar graph (e.g. 3):**
  Sets the upper limit for the VSWR LED bar graph.

* **Show LED graph for FWD power (yes/no):**
  Enables or disables the LED/VU-meter-style display for forward power.

* **Show LED graph for REF power (yes/no):**
  Enables or disables the LED/VU-meter-style display for reflected power.

* **Show LED graph for VSWR (yes/no):**
  Enables or disables the LED/VU-meter-style display for VSWR.

* **Cable loss in dB (e.g. 3):**
  Sets the cable loss of your system, which is then taken into account in the calculations.

After making the desired changes, click **Save Configuration**.

---

## Warning / Disclaimer

This software contains no security mechanisms at all. There is no input/output sanitization or validation. In addition, there is no authentication or authorization mechanism implemented. Any person or system inside the network can access the application, read and modify configuration values, and retrieve information about monitored devices.

**Do not make the application publicly accessible. Do not expose it to the internet.**

If you are unhappy with the current state of the software, you are welcome to contribute, submit improvements, and open pull requests.

---

## Tags
`#Arduino #ESP32 #HamRadio #Remote`

