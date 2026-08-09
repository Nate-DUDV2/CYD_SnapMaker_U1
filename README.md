# CYD Snapmaker U1 Screen

A custom, standalone touchscreen firmware for the Snapmaker U1 (or any Klipper/Moonraker printer) using the "Cheap Yellow Display" (CYD) ESP32 board. 

I built this because I wanted a dedicated, physical screen for my printer without having to deal with the overhead of a Raspberry Pi screen. This firmware talks directly to Moonraker's API to give you live stats, temperature control, toolhead management, and file launching—all from a $15 screen.

* [Hosyond 2.8" Screen:](https://amzn.to/4fEUpME)
* [Hosyond 4.0" Screen (Recommended):](https://amzn.to/4hWfHH3)
* [AITRIP CYD 2.8:](https://amzn.to/3UnLPtl)

**By Nate's Print Shop**

---

## ✨ Features
* **Direct Moonraker Integration:** No middleware needed. Connects directly to your printer's IP over Wi-Fi.
* **Web UI Command Terminal (NEW):** Access a powerful web dashboard from your PC/Phone (e.g., `http://u1-display-XXXX.local`). View hardware diagnostics, tweak settings, manage filament, and upload to the SD card wirelessly.
* **Environment Monitoring (NEW):** Wire up a BME280/BMP280 sensor to track room Temperature, Humidity, and Pressure. The Web UI even calculates a "3D Printing Environment Status" to warn you if your room is too damp or cold for good printing!
* **Built-in Theme Engine (NEW):** Swap between Dark Mode, Light Mode, Cyberpunk, Retro Terminal, or build your own custom color scheme via the Web UI.
* **Live Filament Syncing:** Automatically pulls filament colors and types directly from Klipper to the screen.
* **Advanced Print Launching:** Toggle Auto Bed Leveling, Timelapse, and map the main extruder (T0) to different tools right before you hit print. (Requires a MicroSD card).
* **Battery Monitor:** Optional on-screen battery percentage tracking for portable/wireless setups.

---

## 🛠️ Hardware Requirements
* **The Screen:** ESP32-2432S028R (2.8") or the ESP32-4827S043 (4.0"). Ensure you get the version with **Resistive Touch** (XPT2046 chip).
* **MicroSD Card:** Formatted to FAT32. This is required if you want to use the advanced file patching/print launching features. 
* **A good USB Cable:** Make sure it supports data, not just charging!
* **BME280 Sensor (Optional):** If using the 4.0" screen, you can wire a BME280 sensor ([like this one from Adafruit](https://www.adafruit.com/product/2652#technical-details)) to the I2C pins (`SDA: 32`, `SCL: 25`) for live room climate tracking.

---

## ⚡ How to Install (The Easy Way)

You do not need to download VS Code or compile anything yourself!

1. Plug your CYD screen into your computer using a data cable.
2. Open Google Chrome or Microsoft Edge.
3. Go to the Web Flasher here: **[CYD Snapmaker Web Flasher](https://Nate-DUDV2.github.io/CYD_SnapMaker_U1/web-flasher/)**
4. Click **Connect**, select your ESP32's COM port, choose your screen size, and hit Install. 

---

## ⚙️ Initial Setup Guide

Once the screen is flashed, here is how to get it connected to your printer.

### 1. Connect to Wi-Fi
On first boot (or if it loses connection), the screen will show a **"Wi-Fi Setup Required"** page.
* Grab your phone or laptop and look for a new Wi-Fi network called `Snapmaker-U1-XXXX`. Connect to it.
* A login page should pop up automatically (if it doesn't, open your browser and go to `192.168.4.1`).
* Select your home Wi-Fi network, type in your password, and save. The screen will reboot.

### 2. Touch Calibration
After connecting to Wi-Fi for the first time, it will ask you to calibrate the screen. 
* Use a stylus or a pen (don't use your fat finger for this part!) and tap the red markers exactly as they appear in the corners. This saves your calibration data to the screen permanently.

### 3. Link Your Printer
By default, the screen looks for a printer at `192.168.1.100`. To change this:
1. Tap the **Settings (S)** icon on the bottom of the left sidebar.
2. Tap **Edit IP**.
3. Use the numpad to type in your actual printer's IP address and hit **SAVE**. 
4. The screen will now connect to your printer! You can also use the **Edit Name** button to change what the printer is called on the home screen.

### 4. The Web Dashboard (Advanced Settings)
To access Themes, Sensor Calibration, and Display modes:
1. Open a web browser on your PC or phone (must be on the same Wi-Fi network).
2. Go to the exact URL shown on your screen's Settings tab under **URL:** (By default, it will be `http://u1-display-XXXX.local`, where XXXX is the last 4 digits of your screen's MAC address).
3. Go to the **Hardware Diags** tab to customize your screen!

---

## 🖨️ How the "Print" Tab Works
To use the Print tab effectively, **you must have a MicroSD card inserted into the CYD screen.**

When you select a file and turn on custom options (like Advanced Tool Mapping), the ESP32 downloads the gcode from Klipper, modifies the file on the local SD card in a folder called `dump_zone`, and then pushes the patched file back to the printer to launch it. 

Without an SD card, the screen has nowhere to store the temporary files while it works!

---

## 💻 Compiling from Source (For Developers)

If you want to tweak the UI, change colors, or add features:
1. Clone this repo.
2. Open the folder in **VS Code** with the **PlatformIO** extension installed.
3. The `platformio.ini` is already set up with multiple environments (`cyd_28`, `cyd_28_dual_usb`, `cyd_40`).
4. Set your default environment at the top of the `.ini` file.
5. Make your changes in `src/main.cpp` and hit the PlatformIO **Upload** button.

*Note: If you mess up your touch calibration while developing, just go to the Settings tab and hit the "Calibrate" button to wipe it and start over.*