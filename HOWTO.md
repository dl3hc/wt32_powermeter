# WT32 Power/SWR Meter — Quick Start

## 1. First boot

1. Flash the sketch via USB (Arduino IDE, board set to your WT32-ETH01 variant).
2. Connect the WT32-ETH01 to your LAN via Ethernet.
3. Power it on. By default it uses a **static IP: `192.168.2.198`** (gateway/DNS `192.168.2.1`, subnet `255.255.255.0`) — make sure your network matches, or edit `myIP`/`myGW`/`mySN`/`myDNS` near the top of the sketch before flashing if it doesn't.
4. Open `http://192.168.2.198/` in a browser — or, once mDNS has had a moment to advertise, `http://powermeter.local/`.

## 2. Set your OTA password (required before anything else works)

The very first thing you'll see is **not** the dashboard — it's a "Set an OTA update password" form. Every unit ships with the same default OTA password (`changeme`), and until you change it, the dashboard and config pages refuse to load. This exists because an unauthenticated OTA listener is effectively a firmware-flashing backdoor for anyone on your network.

- Enter a new password (8+ characters) twice, submit.
- It's saved to flash (NVS) immediately and takes effect without a reboot.
- After this, the dashboard loads normally on every future visit — this screen only reappears if the stored password is ever reset back to the literal default.

## 3. Using the dashboard

The main page shows three instrument modules — **FWD Power**, **REF Power**, **VSWR** — each with a numeric readout and an LED bargraph, refreshed twice a second. A module's border glows red if its value is out of the calibrated range or VSWR exceeds your configured threshold (with an optional beep). Antenna name, band, temperature, and firmware version sit in the header/footer strips.

## 4. Configuration page

Click **Configuration** at the bottom of the dashboard to:
- Pick the active **band** (each band has its own saved calibration and settings).
- Edit the **mV → dBm calibration table** for FWD/REF as `mv:dbm` lines, one point per line.
- Adjust general settings: VSWR warning threshold, cable loss compensation, which readouts/LED bars are shown, average-vs-peak power display, temperature unit, etc.

## 5. Updating firmware over the network (OTA)

Once the device has booted at least once with a real password set:
- In the Arduino IDE, go to **Tools → Port** — the device should appear under "Network Ports" as `powermeter at <ip>` (or whatever hostname you set via `device_hostname` in the sketch).
- Select it, hit Upload as normal. You'll be prompted for the OTA password you set in step 2.
- Progress and errors print to Serial if you have USB connected at the same time, but it's not required.

## 6. Fallback: updating firmware via USB

**Always available, regardless of network state, OTA password, or whether the device is even reachable on the LAN.** Just plug in USB, select the correct serial port in the Arduino IDE, and upload — this talks directly to the ESP32's ROM bootloader and doesn't go through any of the application code above at all. If the device is completely unreachable on the network (bad static IP, Ethernet issue, etc.), this is your way back in.

## 7. If you forget the OTA password

A plain USB reflash **won't** fix this on its own — flashing the sketch normally only overwrites the application partition, not NVS (where the password lives), so the old password survives. To force it back to the default and re-trigger the setup screen:
- In the Arduino IDE: **Tools → Erase Flash → "All Flash Contents"**, then upload.
- Or via command line: `esptool.py erase_flash` before the next upload.

This wipes *all* stored settings (calibration, band selection, OTA password, everything in NVS), not just the password — it's a full factory reset, not a targeted password recovery. Worth knowing before you reach for it.

## 8. Reliability notes

- A hardware watchdog auto-reboots the device if the main loop ever hangs for more than 15 seconds (`WDT_TIMEOUT_SECONDS` in the sketch) — no more silent freezes on a unit you can't easily walk over to power-cycle.
- A SPIFFS mount failure no longer takes down the whole web server — the dashboard and network stay up even if calibration data can't load.
