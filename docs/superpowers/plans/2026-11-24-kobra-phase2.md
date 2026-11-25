# Kobra S1 / Kobra X (phase 2) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Add "Anycubic Kobra" as printer type 2: the screen pairs with the Kobra over LAN, watches it over MQTT/TLS, and shows temps, job state, progress, time left and the ACE Pro's 4 slots. Watch-only.

**Architecture:** Message parsing is pure C++ in `lib/PrinterParse` (`KobraState` + `kobraApplyMessage`), unit-tested on the PC. The network side (pairing over HTTP :18910, MQTT/TLS :9883 with PubSubClient + WiFiClientSecure, client cert from NVS) runs in the existing poll task when type = Kobra and fills the same `PrinterSnapshot`. The ACE reuses the K2 `FilUnit` filament tab.

**Tech Stack:** ESP32 Arduino (espressif32 6.x), PubSubClient 2.8, mbedtls (MD5, AES-CBC, base64), ArduinoJson 7, Unity (native).

**Spec:** `docs/superpowers/specs/2026-11-23-multi-printer-k2-kobra-design.md`

## Global Constraints
- NVS `printerType`: 0 = U1, 1 = K2, 2 = Kobra. Existing screens stay U1.
- The Anycubic client certificate/key are never in the firmware or repo. Users paste PEM on the web page; stored in NVS (`kbCert`, `kbKey`).
- Without a certificate the screen says to upload it on the web page instead of connecting.
- Kobra is watch-only: no pause/cancel, temps, speed/fan pop-ups, Move tab or Print tab; web dashboard read-only.
- Network work stays in the poll task (never on the UI loop).
- On boards without PSRAM, the radar tab is disabled in Kobra mode.

## Review Focus
1. Messages larger than the MQTT buffer, or not JSON: dropped without crashing.
2. Partial ACE updates (only `drying_status`, only `loaded_slot`) must not wipe known slot data.
3. Kobra S1 field names (`curr_nozzle`, `curr_bed`) as well as X/Kobra 3 names (`curr_nozzle_temp`, `curr_hotbed_temp`).
4. Pairing failure / login refused: retries with back-off, screen shows Offline, no reboot loop.
5. U1 and K2 behaviour unchanged.

## Spike substitute (Ruling)
The printers are not reachable from the dev PC, so the spike cannot run first. The firmware's `/kobra` web page shows pairing/MQTT status and the last raw MQTT messages, so the first connection doubles as the capture. Field names come from the open-source hass-anycubic_cloud integration (real traffic) plus KobraCommandCenter's S1 names.

---

### Task 1: Kobra message parser (native tests)
**Files:** Modify `lib/PrinterParse/PrinterParse.h/.cpp`; Create `test/test_printer_parse/kobra_fixtures.h`; Modify `test/test_printer_parse/test_main.cpp`.

Interfaces (produces):
```cpp
struct KobraState { bool online; bool haveTemps; float nozzle, nozzleTarget, bed, bedTarget;
  char state[12]; char file[64]; float progress; int remainMin; int elapsedMin; int fan; int speed;
  bool haveAce; FilUnit ace; };
void kobraInit(KobraState &s);
bool kobraApplyMessage(JsonVariantConst msg, KobraState &s);   // true if recognised
PrinterType sanitizePrinterType(int raw);  // now also accepts 2 -> PT_KOBRA
```
FilUnit gains `int drying` (-1 unknown, 0 off, 1 drying) and `int dryRemainMin`.

State mapping (Moonraker words the UI already uses): print `printing/preheating/checking/downloading/resuming/resumed/updated` -> "printing"; `pausing/paused` -> "paused"; `finished` -> "complete"; `stoped/stopping` -> "cancelled"; `failed` -> "error"; `status` `free` -> "standby" (when not paused/printing). `lastWill` `offline` -> online=false.

Tests: sanitize 2; S1 temps (`curr_nozzle`); X temps (`curr_nozzle_temp`); print progress/remain/file/state; pause; finished; status free; fan/speed; ACE getInfo (material, `color:[r,g,b]`, empty slot, temp, drying); partial drying update keeps slots; lastWill offline; garbage message returns false.

### Task 2: Kobra setting, pairing, MQTT backend
**Files:** `src/main.cpp`, `platformio.ini` (add `knolleary/PubSubClient@^2.8`).
- Portal + web selects get "Anycubic Kobra S1 / X" (value 2).
- Pairing per spec steps 1-4 (NTP time for `ts`), creds cached in NVS `kbDev/kbUser/kbPass/kbModel`; re-pair when MQTT login is refused (rc 4/5) or creds missing.
- MQTT: client id = deviceId; subscribe `anycubic/anycubicCloud/v1/+/public/{m}/{d}/#`, `.../printer/app/{m}/{d}/#`, `.../printer/{m}/{d}/#`. Every 30 s publish read-only queries (`info/query`, `multiColorBox/getInfo`) to `.../web/printer/{m}/{d}/{type}`.
- Poll task stack 16 KB in Kobra mode; buffer 12 KB with PSRAM, 6 KB without.
- Raw log ring (8 msgs × 700 chars) + `/kobra` page: status, cert upload (PEM textareas), re-pair button, raw log.

### Task 3: Kobra screens and web
- Sidebar {"H","F","","","S"}; taps on blank slots ignored. Filament tab header "ACE PRO" + drying state. Home: NOZZLE TARGET card, no temp/speed/fan/pause pop-ups. Web dashboard read-only, `/control` refused. `sendGcode` and `fetchKlipperFiles` skip non-Moonraker printers.

### Task 4: Release
- `FW_VERSION "v2.2.0"`, README Kobra how-to (pfx -> PEM `openssl` commands), web-flasher bump, all 5 boards build, native suite green, final review.
