# CYD Snapmaker U1 Screen

A custom, standalone touchscreen firmware for the Snapmaker U1 (or any Klipper/Moonraker printer) using the "Cheap Yellow Display" (CYD) ESP32 board. 

I built this because I wanted a dedicated, physical screen for my printer without having to deal with the overhead of a Raspberry Pi screen. This firmware talks directly to Moonraker's API to give you live stats, temperature control, toolhead management, and file launching—all from a $15 screen. Link: https://amzn.to/4fEUpME

**By Nate's Print Shop**

---

## ✨ Features
* **Direct Moonraker Integration:** No middleware needed. Connects directly to your printer's IP over Wi-Fi.
* **Live Filament Syncing:** Automatically pulls filament colors and types directly from Klipper to the screen.
* **Advanced Print Launching:** Lets you toggle Auto Bed Leveling, Timelapse, and map the main extruder (T0) to different tools right before you hit print. (Requires a MicroSD card).
* **On-Screen Configuration:** No need to hardcode your Wi-Fi or Printer IP. You can edit the printer IP and screen name right from the settings tab using the built-in touch keyboard.
* **Web Flasher:** You don't even need to download any code to install this. Just plug it in and flash from your browser.

## 🛠️ Hardware Requirements
* **The Screen:** ESP32-2432S028R (Usually sold as the "ESP32 Cheap Yellow Display" or CYD). Ensure you get the version with **Resistive Touch** (XPT2046 chip) and the 2.8" LCD.
* **MicroSD Card:** Formatted to FAT32. This is required if you want to use the advanced file patching/print launching features. 
* **A good Micro-USB / USB-C cable:** Make sure it supports data, not just charging!

---

## ⚡ How to Install (The Easy Way)

You do not need to download VS Code or compile anything yourself. 

1. Plug your CYD screen into your computer using a data cable.
2. Open Google Chrome or Microsoft Edge.
3. Go to the Web Flasher here: **[CYD Snapmaker Web Flasher](https://Nate-DUDV2.github.io/CYD_SnapMaker_U1/web-flasher/)**
4. Click **Connect**, select your ESP32's COM port, and hit Install. 

---

## ⚙️ Initial Setup Guide

Once the screen is flashed, here is how to get it connected to your printer.

### 1. Connect to Wi-Fi
On first boot (or if it loses connection), the screen will show a **"Wi-Fi Setup Required"** page.
* Grab your phone or laptop and look for a new Wi-Fi network called `Snapmaker-U1-Display`. Connect to it.
* A login page should pop up automatically (if it doesn't, open your browser and go to `192.168.4.1`).
* Select your home Wi-Fi network, type in your password, and save. The screen will reboot.

### 2. Touch Calibration
After connecting to Wi-Fi for the first time, it will ask you to calibrate the screen. 
* Use a stylus or a pen (don't use your fat finger for this part!) and tap the red markers exactly as they appear in the corners. This saves your calibration data to the screen permanently.

### 3. Link Your Printer
By default, the screen looks for a printer at `192.168.87.125`. To change this:
1. Tap the **Settings (S)** icon on the bottom of the left sidebar.
2. Tap **Edit IP**.
3. Use the numpad to type in your actual printer's IP address.
4. Hit **SAVE**. 
5. The screen will now connect to your printer! You can also use the **Edit Name** button to change what the printer is called on the home screen.

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
3. The `platformio.ini` is already set up for the CYD board with the correct TFT_eSPI settings. 
4. Make your changes in `src/main.cpp`.
5. Hit the PlatformIO **Upload** button.

*Note: If you mess up your touch calibration while developing, just go to the Settings tab and hit the "Calibrate" button to wipe it and start over.*
