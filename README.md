# CYD Snapmaker Screen

A standalone touchscreen for your 3D printer, built on cheap ESP32 display boards. No Raspberry Pi, no cloud, no middleware: the screen joins your Wi-Fi and talks straight to the printer.

It started as a screen for the **Snapmaker U1** and now also runs **Creality K2** printers and **Anycubic Kobra S1 / X** printers.

**By Nate's Print Shop**

---

## 🖨️ Supported printers

| Printer | Live status | Pause / Stop | Move / jog | Print files | Filament |
|---|:-:|:-:|:-:|:-:|---|
| **Snapmaker U1** (and other Klipper/Moonraker printers) | ✅ | ✅ | ✅ | ✅ | 4 toolheads: type, colour, load/unload |
| **Creality K2 / K2 Plus / K2 Pro** | ✅ | ✅ | ✅ | ✅ | CFS (up to 2 units): type, colour, load/unload |
| **Anycubic Kobra S1 / Kobra X** | ✅ | ✅ | ✅ | ✅ | ACE Pro: type, colour, load/unload, drying status |

Each screen follows one printer. Pick the printer type on the Wi-Fi setup page (first boot), or later on the web dashboard under **Hardware & Config**.

---

## 🖥️ Supported screens

| Screen | Web flasher entry | Notes |
|---|---|---|
| [Waveshare ESP32-S3 4.3"](https://amzn.to/4g1MUQd) | 4.3" Waveshare | Capacitive touch |
| Waveshare ESP32-S3-Touch-LCD-7 (800×480) | 7.0" Waveshare … LCD-7 | Same firmware as the 4.3" |
| Waveshare ESP32-S3-Touch-LCD-7B (1024×600) | 7.0" Waveshare … LCD-7B | New, not yet tested on hardware |
| Sunton ESP32-8048S070C (7") | 7.0" Sunton | Capacitive touch |
| Sunton / DIYmalls ESP32-8048S050C (5") | 5.0" Sunton | Capacitive touch |
| [Hosyond 4.0"](https://amzn.to/4hWfHH3) (ESP32-4827S043, ST7796) | 4.0" CYD | Resistive touch |
| [Hosyond 2.8"](https://amzn.to/4fEUpME) / [AITRIP CYD 2.8"](https://amzn.to/3UnLPtl) (ESP32-2432S028R) | 2.8" CYD Standard | Resistive touch, one USB port |
| 2.8" CYD with USB-C + micro-USB | 2.8" CYD Dual-USB | ST7789 display |

> **7" boards:** the Sunton and Waveshare 7" boards are different hardware. Check the model printed on the board and pick the matching entry.

The bigger ESP32-S3 screens (4.3", 5", 7") have extra RAM and are the smoothest, especially with a Kobra. The 2.8" and 4.0" work with every printer but have less memory to spare.

---

## ⚡ Install (no coding needed)

1. Plug the screen into your computer with a **data** USB cable (not a charge-only one).
2. Open **Chrome** or **Edge**.
3. Go to the **[Web Flasher](https://nate-dudv2.github.io/CYD_SnapMaker_U1/web-flasher/)**.
4. Choose your screen, click **Install**, and pick the screen's COM port.

Close slicers or serial monitors first, since they can hold the COM port. If the port won't open on an ESP32-S3 board, hold **BOOT**, tap **RESET**, release **BOOT**, and try again.

---

## ⚙️ First-time setup

### 1. Wi-Fi
On first boot the screen shows **"Wi-Fi Setup Required"**.
1. On your phone or laptop, join the Wi-Fi network **`Snapmaker-U1-XXXX`**.
2. A setup page opens (if not, go to `http://192.168.4.1`).
3. Pick your home Wi-Fi, enter the password, and set the **printer type** and **printer IP**. Save; the screen restarts.

### 2. Touch calibration (2.8" and 4.0" only)
Resistive screens ask you to tap 4 crosshairs once. Use a stylus or pen tip. The capacitive screens (4.3", 5", 7") need no calibration.

### 3. Printer
Set or change the printer later on the screen (**S** tab → Edit IP) or on the web dashboard.

| Printer | What it needs |
|---|---|
| **Snapmaker U1 / Klipper** | The printer's IP. Moonraker on port 7125. |
| **Creality K2** | The printer's IP. The screen uses Moonraker on port 7125 and falls back to 4408 by itself. |
| **Anycubic Kobra S1 / X** | Turn on **LAN Mode** on the printer (Settings → Network → LAN Mode), then enter its IP. The screen pairs by itself; no certificate or Anycubic account needed. |

Anycubic Slicer, the Anycubic app and other tools can stay connected to a Kobra at the same time.

---

## ✨ What's on the screen

The sidebar tabs are **H**ome, **F**ilament (or **T**ools on the U1), **M**ove, **P**rint and **S**ettings.

- **Home:** state, temperatures, progress, time left, speed and fan, with **Pause** and **Stop**.
- **Filament (K2 / Kobra):** every CFS / ACE slot with its material, colour and amount left. Tap a slot to:
  - set its **material** (or type your own with **CUSTOM**),
  - set its **colour** (or enter any **HEX** colour),
  - **Load** or **Unload** it (disabled while printing; the K2 heats the nozzle for that material first).
- **Tools (U1):** each toolhead's material, colour, temperature presets, attach/park and load/unload.
- **Move:** jog X / Y / Z and home.
- **Print:** start files stored on the printer, its USB stick (Kobra), or this screen's **microSD card** (uploaded to the printer, then started).
  - **U1:** turn on bed levelling or timelapse, or send T0 to another tool before printing.
  - **K2:** choose which **CFS slot** the print's first colour comes from.
- **Settings:** printer IP and name, calibration, themes, battery and sensor options.

### Web dashboard
Open the address shown on the screen's **S** tab (usually `http://u1-display-XXXX.local`) from any device on the same Wi-Fi:
- **Dashboard:** live status and the filament slots.
- **Hardware & Config:** printer type and IP, themes, sensors, board info and free memory.
- **Kobra Link** (Kobra only): connection status, memory, the last raw printer messages, and **Pair again**.
- **Audio Player** (when sound is turned on): play and upload sound files.
- Upload files to the screen's SD card wirelessly.

### Extras
- **Themes:** Dark, Light, Cyberpunk, Retro Terminal, or your own colours.
- **Room sensor (optional):** a BME280 on the 4.0" (`SDA 32`, `SCL 25`) shows room temperature, humidity and pressure, with a "good for printing?" check.
- **Battery percentage** for portable setups.

---

## 💾 microSD card

A FAT32 microSD card in the screen is needed for:
- printing files **from the screen's SD card**,
- U1 print options (bed levelling, timelapse, tool mapping) and the K2 **CFS slot** picker, which rewrite the file on the card before sending it,
- sound effects.

You can insert a card at any time: it's picked up the next time you open the SD file list. Only `.gcode` and `.3mf` files in the card's top folder are listed.

---

## 🧩 Troubleshooting

- **Nothing happens after flashing a 7":** you probably picked the other brand's 7" entry. Check the board's model and reflash.
- **Kobra shows offline:** make sure LAN Mode is on and the IP is right, then use **Kobra Link → Pair again**.
- **2.8" with a Kobra:** it works, with less memory to spare. If its printer link drops for a few minutes, the screen restarts itself (at most once every 30 minutes).
- **K2 slot shows the wrong material name:** the CFS stores Creality material codes, so typed names are saved as their family (for example "PLA+" → PLA).

---

## 💻 Building from source

1. Clone this repo and open it in **VS Code** with the **PlatformIO** extension.
2. Pick an environment in `platformio.ini`:
   `cyd_28`, `cyd_28_dual_usb`, `cyd_40`, `waveshare_43_s3`, `waveshare_70b`, `sunton_50`, `sunton_70`.
3. Build and upload. Every build copies its `.bin` files into `web-flasher/` automatically.

Unit tests for the printer parsing code run on your PC: `pio test -e native`.

Code layout:
- `src/main.cpp`: screen, touch, web dashboard, U1 and K2 (Moonraker).
- `src/kobra_backend.*`: Anycubic Kobra LAN pairing and MQTT.
- `lib/PrinterParse/`: printer data parsing (K2 CFS, Kobra reports, material codes), covered by the tests in `test/`.
