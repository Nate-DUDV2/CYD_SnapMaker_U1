# K2 / K2 Plus Support + Printer-Type Setting (Phase 1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let one firmware be set to Snapmaker U1 or Creality K2 (K2 and K2 Plus) at setup or on the web page, and show the K2's CFS units (up to 2 × 4 slots) where the U1 shows toolheads.

**Architecture:** A new `printerType` NVS setting picks the backend inside the existing v2.0.6 poll task. K2 is Moonraker, like the U1, but with a different object query plus the `box` (CFS) object. CFS parsing lives in a small pure-C++ library (`lib/PrinterParse`) so it can be unit-tested on the PC with the PlatformIO native environment. Everything else stays in `src/main.cpp`, following the file's existing single-file style.

**Tech Stack:** PlatformIO, Arduino-ESP32, ArduinoJson 7, WiFiManager 2.0.17, TFT_eSPI/LovyanGFX, Unity (native tests, host gcc 13 from Strawberry is on PATH).

**Spec:** `docs/superpowers/specs/2026-11-23-multi-printer-k2-kobra-design.md`. Phase 1 covers everything in it except the Kobra backend and Kobra certificate handling, which get their own plan after the Kobra spike (per "Calls made during review → Phasing").

## Global Constraints

- Default `printerType` is 0 = U1. A v2.0.6 screen updated to this firmware must come up as U1 with all settings and calibration intact.
- NVS namespace stays `"printer"`. Preferences stays open for the firmware's lifetime (never call `preferences.end()`).
- The UI never does network I/O except `sendGcode()`. All polling stays in `printerPollTask` on core 0.
- K2 port: try 7125 first; if it fails, try 4408 and remember which one worked (until reboot).
- The 2.8" CYD (classic ESP32, no PSRAM) must keep working on both types.
- Phase 1 offers only "Snapmaker U1" and "Creality K2" in both dropdowns. Kobra is not shown until phase 2.
- `remain_len` is a percentage (0–100).
- Nate commits and pushes himself. "Checkpoint" steps mean: stop and tell Nate the task is ready to commit, with a suggested message. Don't run `git commit`.
- New version: `FW_VERSION "v2.1.0"`.

## Review Focus

1. **CFS layout variants:** one CFS only; the second CFS at `T3` (Nate's K2) or `T2`; a unit with `state` `"None"`; no `box` object at all (K2 without CFS). Expect 4 slots, 8 slots, or "No CFS detected", never a crash. Tests: Task 2.
2. **Bad slot data:** material code `""`, `"-1"`, `"0"` or unknown; color missing; `remain_len` of -1 or >100; arrays shorter than 4. Expect the "—" label with a grey circle, a clamped or hidden percentage, and no out-of-bounds reads. Tests: Task 2.
3. **Upgrade and garbage settings:** no `printerType` key (v2.0.6 upgrade), or a stored value of 2 or 255. Expect U1. Tests: Task 2 (`sanitizePrinterType`) plus a device check in Task 3.
4. **Port fallback:** 7125 closed but 4408 open means it works over 4408. Both closed means "Offline" within about 3 polls while the UI stays responsive. Device check: Task 4.
5. **K2 type on U1-only screens:** Print tab tapped, toolhead modal opened, web filament form posted. Expect the Print tab to be ignored, no tool modal, and the filament form not shown or rejected. Device check: Task 5.

---

### Task 1: Spike: capture real `box` data from the K2 and K2 Plus

**Files:**
- Create: `test/test_printer_parse/k2_box_fixtures.h`

**Interfaces:**
- Produces: `static const char K2_BOX_DUAL[]`, `K2_BOX_SINGLE[]` (if a single-CFS capture is possible, e.g. one unit unplugged; otherwise hand-made from the dual one by setting the second unit's `state` to `"None"`), `K2_BOX_NONE[]` (a status object with no `box`). Each holds the JSON of `result.status` from Moonraker. Also produces the **slot color field name and format**, written as a comment at the top of the header and used by Task 2.

- [ ] **Step 1:** Ask Nate for the K2 and K2 Plus IP addresses and to turn both on with the CFS loaded.
- [ ] **Step 2:** From the PC, for each printer run
  `curl -s "http://<IP>:7125/printer/objects/query?box&extruder&heater_bed&print_stats&display_status&toolhead&fan&gcode_move"`.
  If 7125 doesn't answer, repeat on port 4408 and note which port each printer answered on.
  Expected: JSON with `result.status.box` containing `T1` and a second unit key.
- [ ] **Step 3:** Record in the header comment the following:
  - which key the second unit uses on each printer (`T2`/`T3`);
  - the color field name and its format (e.g. `"color_value": ["0FF0000", ...]` or `#RRGGBB`);
  - the types of `temperature` and `dry_and_humidity` (number or string);
  - what an empty slot looks like.
- [ ] **Step 4:** Paste the `result.status` objects as raw string literals into `k2_box_fixtures.h`.
- [ ] **Checkpoint:** Tell Nate what was found (one short paragraph). If the color field is missing entirely, slots show a material-only grey circle and the plan continues.

### Task 2: `PrinterParse` library and native tests

**Files:**
- Create: `lib/PrinterParse/PrinterParse.h`, `lib/PrinterParse/PrinterParse.cpp`
- Create: `test/test_printer_parse/test_main.cpp`
- Modify: `platformio.ini` (add `[env:native]`)

**Interfaces:**
- Consumes: the fixtures and color field from Task 1.
- Produces (no Arduino types, so it compiles natively):
```cpp
enum PrinterType : uint8_t { PT_U1 = 0, PT_K2 = 1, PT_KOBRA = 2 };
PrinterType sanitizePrinterType(int raw);        // phase 1: 1 -> PT_K2, anything else -> PT_U1
const char *printerTypeLabel(PrinterType t);     // "SNAPMAKER U1", "CREALITY K2", "ANYCUBIC KOBRA"

const int MAX_FIL_UNITS = 2;
struct FilSlot { bool present; char material[12]; bool hasColor; uint32_t rgb; int remainPct; }; // remainPct -1 = unknown
struct FilUnit { bool present; char key[4]; bool hasEnv; float temp; float humidity; FilSlot slot[4]; };

int  parseK2Box(JsonVariantConst status, FilUnit out[MAX_FIL_UNITS]); // status = result.status; returns units found 0..2
void decodeCrealityMaterial(const char *code, char *out, size_t n);   // "" / "-1" / "0" -> "Empty"
uint16_t rgb888To565(uint32_t rgb);
```
- `parseK2Box` rules:
  - Unit 1 is `T1`.
  - Unit 2 is `T3` if its `state` is present and not `"None"`, else `T2` under the same rule (this mirrors the Creality app's `GetSecondBoxKey`).
  - Units are packed in order, so a lone `T3` becomes `out[0]`.
  - A slot is `present` only if its decoded material is not `"Empty"`.
  - `remainPct` is clamped to 0..100, and -1 when missing or negative.
- `decodeCrealityMaterial` table, copied from `Creality/Creality/MainWindow.xaml.cs` `DecodeMaterial`:
  - 001001 PLA, 001002 PLA-Silk, 001003 PLA-CF, 001004 CR-PLA, 001005 Hyper PLA
  - 002001 ABS, 005001 PLA, 006001 PETG, 006002 PETG, 007001 ABS
  - 011001 TPU, 012001 PA-CF, 013001 PC
  - Otherwise: a substring match on PLA/PETG/ABS/TPU/PA|NYLON→Nylon/PC; else the raw code, truncated to fit.

- [ ] **Step 1: Add `[env:native]` to `platformio.ini`**
```ini
[env:native]
platform = native
board =
framework =
extra_scripts =
test_framework = unity
lib_deps = bblanchon/ArduinoJson@^7.0.0
build_flags = -std=gnu++17
```
  If the vendored `lib/ArduinoJson` (6.17.2) gets picked up instead (the symptom is a compile error on `JsonDocument doc;`), add `lib_ignore = ArduinoJson` and change the dep to `bblanchon/ArduinoJson@7.4.2`. Confirm with the build log line `ArduinoJson @ 7.x`.

- [ ] **Step 2: Write the failing tests** in `test_main.cpp` (Unity, `setUp`/`tearDown` empty, `main` runs them all):
  - `test_sanitize_type`: `sanitizePrinterType(0)==PT_U1`, `(1)==PT_K2`, `(2)==PT_U1`, `(255)==PT_U1`, `(-1)==PT_U1`.
  - `test_label`: `strcmp(printerTypeLabel(PT_K2),"CREALITY K2")==0`.
  - `test_decode_material`:
    - `"001001"`→`"PLA"`, `"006002"`→`"PETG"`, `"-1"`→`"Empty"`, `""`→`"Empty"`
    - `"XYZ-PETG-9"`→`"PETG"`, `"999999"`→`"999999"`
  - `test_rgb565`: `rgb888To565(0xFF0000)==0xF800`, `(0x00FF00)==0x07E0`, `(0x0000FF)==0x001F`.
  - `test_box_dual`: `K2_BOX_DUAL` gives 2 units. `out[1].key` equals the second key recorded in Task 1. Each slot's material and `remainPct` match the capture (assert the real values from the fixture).
  - `test_box_single`: `K2_BOX_SINGLE` gives 1 unit.
  - `test_box_none`: `K2_BOX_NONE` gives 0.
  - `test_box_t3_only`: inline JSON `{"box":{"T1":{"state":"None"},"T3":{"state":"connect","material_type":["001001","-1","",null],"remain_len":[50,-1,150,null]}}}` gives 1 unit with key `"T3"`:
    - slot 0: present PLA, 50%
    - slot 1: not present, -1
    - slot 2: not present, `remainPct` 100
    - slot 3: not present, -1
  - `test_box_short_arrays`: `material_type` with 2 entries leaves slots 2 and 3 not present (no crash).
  - `test_color`: slot color from the Task 1 field gives `hasColor` and the expected `rgb`; a slot with no color gives `hasColor==false`.

- [ ] **Step 3: Run the tests to confirm they fail**
  Run: `pio test -e native`
  Expected: build or link failure (`parseK2Box` undefined).

- [ ] **Step 4: Implement `PrinterParse.h/.cpp`** with the signatures and rules above. Parse with `JsonVariantConst` only (no allocation). Use `strlcpy`-style bounded copies (write a local `copyStr` helper; `strlcpy` isn't on every host libc).

- [ ] **Step 5: Run the tests to confirm they pass**
  Run: `pio test -e native`
  Expected: `10 Tests 0 Failures`.

- [ ] **Step 6: Confirm the ESP32 build still links with the library present**
  Run: `pio run -e cyd_28`
  Expected: `SUCCESS`.

- [ ] **Checkpoint:** "feat: add PrinterParse library (K2 CFS parsing) with native tests"

### Task 3: Printer-type setting (portal, web page, boot screen, header)

**Files:**
- Modify: `src/main.cpp`:
  - globals near line 465
  - setup prefs load near line 1657
  - WiFiManager params at lines 1738–1751
  - `drawBootLogo` at line 769
  - Home header in `updateDynamicUI` at line 3217
  - `handleTech` at line 3757
  - `handleSave` at line 4053
  - `FW_VERSION` at line 18

**Interfaces:**
- Consumes: `PrinterType`, `sanitizePrinterType`, `printerTypeLabel` (Task 2).
- Produces: global `PrinterType printerType;` loaded in setup with `sanitizePrinterType(preferences.getInt("printerType", 0))`. Saved with `preferences.putInt("printerType", v)`.

- [ ] **Step 1: Bump the version** to `"v2.1.0"`. Add `#include <PrinterParse.h>`, then the global, then the load in setup *before* `drawBootLogo()` is called.
- [ ] **Step 2: Setup portal.** Add a WiFiManager custom-HTML parameter called `printer_type`. It renders a `<select name='printer_type'>` with options `0` "Snapmaker U1" and `1` "Creality K2 / K2 Plus", with the current value pre-selected. Because WiFiManager custom HTML has no `getValue()` for raw HTML, read it in the save callback with `wm.server->arg("printer_type")`, then sanitize and save. Rename the IP field's label from "U1 IP Address" to "Printer IP Address".
- [ ] **Step 3: Web page.** On the Hardware & Config page (`handleTech`), add the same dropdown (form field `printer_type`) above the IP field. In `handleSave`, if `printer_type` is present and differs after sanitizing, save it. The existing save flow then reboots. If the current save path doesn't reboot, call `ESP.restart()` after the response only when the type changed.
- [ ] **Step 4: Labels.**
  - The boot screen shows `printerTypeLabel(printerType)` on its own line under the version line.
  - The Home header stays `printerName : state`, except when `printerName` is still the factory default `"Snapmaker U1"` and the type isn't U1. Then it uses `printerTypeLabel(printerType)`, so a K2 screen never says "Snapmaker U1". A name the user set always wins.
- [ ] **Step 5: Build and flash the 2.8" (COM4) with existing v2.0.6 settings on it**
  Run: `pio run -e cyd_28 -t upload --upload-port COM4` (only if the build succeeded)
  Expected:
  - The boot screen shows "v2.1.0" and "SNAPMAKER U1".
  - The IP, name and touch calibration are unchanged.
  - The Home tab polls the U1 as before.
- [ ] **Step 6: Web check.** Open `http://192.168.87.70/tech` and switch the type to Creality K2. Expected: the screen reboots and the boot screen shows "CREALITY K2". Switch back to U1 and confirm it reboots to U1.
- [ ] **Checkpoint:** "feat: printer type setting in setup portal and web page"

### Task 4: K2 backend in the poll task

**Files:**
- Modify: `src/main.cpp`:
  - `PrinterSnapshot` at line 3440
  - `queryPrinter` at line 3461
  - `queryEstimate` at line 3516
  - `printerPollTask` at line 3534
  - `applyPrinterSnapshot` at line 3574
  - `sendGcode` at line 3609

**Interfaces:**
- Consumes: `parseK2Box`, `FilUnit`, `MAX_FIL_UNITS`, `printerType`.
- Produces:
  - `PrinterSnapshot` gains `FilUnit units[MAX_FIL_UNITS]; int unitCount = 0;`
  - Globals `FilUnit cfsUnits[MAX_FIL_UNITS]; int cfsUnitCount = 0;`, copied in `applyPrinterSnapshot`
  - Global `volatile uint16_t printerPort = 7125;`, used by `queryPrinter`, `queryEstimate` and `sendGcode` (and `fetchKlipperFiles` / upload, which stay U1-only but should use it too for consistency)

- [ ] **Step 1: Make `queryPrinter(const char *ip, uint16_t port, PrinterSnapshot &s)` port-aware.**
  - For `PT_K2`, query `print_stats&toolhead&extruder&heater_bed&fan&gcode_move&display_status&box`.
  - Map `extruder` into `toolTemp[0]`/`toolTarget[0]` and set `activeTool = 0`.
  - Fill `units`/`unitCount` from `parseK2Box(status, s.units)`.
  - Skip `print_task_config` and the filament arrays.
  - U1 keeps today's query exactly.
- [ ] **Step 2: Port fallback in `printerPollTask`, for K2 only.**
  - If a poll fails on the current port and the type is K2, try the other port (7125↔4408) once in the same cycle. If that succeeds, set `printerPort` to it.
  - Failure counting (3 fails means offline) is unchanged.
- [ ] **Step 3: Copy the units in `applyPrinterSnapshot`.** U1 filament handling stays unchanged and only runs for `PT_U1`.
- [ ] **Step 4: Device check.** Flash the 2.8", set the type to K2 and the IP to the K2. Use `PERF_DEBUG` and a temporary `Serial.printf` of the unit count and the slot materials. Expected:
  - The serial log shows the same slot materials as `curl` in Task 1.
  - The Home tab shows the K2's bed and nozzle temperatures, state, file and time left.
  - Then set the IP to the K2 Plus and confirm the same.
  - Remove the temporary print before the checkpoint.
- [ ] **Step 5: Offline check.** Turn the K2 off, or set a wrong IP. Expected: the Home tab shows "Offline" within about 3 polls, and tabs still switch instantly.
- [ ] **Step 6: Control check on the K2.**
  - Set the bed preset to 60, then 0.
  - Change speed and fan.
  - Pause then resume during a test print (Nate starts the print).
  - Home all from the Move tab.
  Expected: each one acts on the K2 (via `printerPort`).
- [ ] **Checkpoint:** "feat: Creality K2 backend with CFS polling and 4408 fallback"

### Task 5: Per-type screens (filament tab, sidebar, Home, web)

**Files:**
- Modify: `src/main.cpp`:
  - `drawSidebar` at line 2637
  - `drawToolheadTab` at line 3059
  - `updateDynamicUI` tab 1 branch at line 3293 and Home branch at line 3213
  - touch handling for tab 0/1/3 at lines 2324–2402
  - the tab-switch code at line 2290
  - web home/filament form in `handleRoot` at line 3664 and `handleSaveFilament` at line 4174

**Interfaces:**
- Consumes: `cfsUnits`, `cfsUnitCount`, `printerType`, `rgb888To565`.
- Produces: `void drawFilamentTab();` and `int cfsPage = 0;` (0 or 1, used only on small screens).

- [ ] **Step 1: Sidebar for `PT_K2`.**
  - Labels are `H, F, M, "", S`, with "F" for Filament and the Print slot drawn blank.
  - Taps on index 3 are ignored in the tab-switch code.
- [ ] **Step 2: `drawFilamentTab()`, drawn when `currentTab == 1 && printerType == PT_K2`.**
  - Big screens (`BOARD_WAVESHARE_43`, `BOARD_SUNTON_70`): both units at once, as two rows of 4 cards.
  - Small screens: one unit of 4 cards, using the existing card geometry from `drawToolheadTab`.
  - Each card shows:
    - the slot label (`1A`..`1D` / `2A`..`2D`)
    - a color circle (grey `0x7BEF` if `!hasColor` or not present)
    - the material (or "—")
    - the remaining amount as `NN%` (blank if -1)
  - Header line:
    - `"CFS 1  <temp>C  <hum>%"`, or `"CFS 1 / CFS 2"` on small screens with two units, the active one in cyan;
    - `"No CFS detected"` when `cfsUnitCount == 0`.
  - The dynamic part redraws in `updateDynamicUI` like tab 1 does today.
- [ ] **Step 3: Touch on the filament tab.**
  - On small screens with two units, a tap on the header toggles `cfsPage` and redraws.
  - Card taps do nothing for K2 (no tool modal).
- [ ] **Step 4: Home tab for K2.**
  - The "ACTIVE TOOL" card is relabelled "NOZZLE" and shows `toolTemp[0]`.
  - Tapping it does nothing.
  - The tool-temp preset card targets T0.
- [ ] **Step 5: Web for K2.**
  - The home page hides the filament form and the per-tool temperature inputs except a single nozzle field.
  - `handleSaveFilament` returns 400 when the type isn't U1.
- [ ] **Step 6: Device check on both screens** (2.8" on COM4, 7" Sunton), K2 type, against the K2 (second unit on `T3`) and the K2 Plus:
  - Expected:
    - 8 slots: on the 7" in two rows; on the 2.8" via the header toggle.
    - The materials and percentages match the Creality app.
  - Also check:
    - a tap on the blank Print slot does nothing;
    - a tap on a filament card opens nothing.
- [ ] **Step 7: U1 regression on both screens.**
  - Set the type back to U1.
  - The T tab shows the 4 toolheads with the tool modal.
  - The Print tab lists files.
  - Filament edits from the web page still save.
- [ ] **Checkpoint:** "feat: CFS filament tab and per-type screens for K2"

### Task 6: Release build and docs

**Files:**
- Modify: `web-flasher/manifest_*.json` (version 2.1.0), `web-flasher/index.html` (version label), `README.md` (new "Supported printers" section)
- Output: `web-flasher/*.bin` for all 5 boards

- [ ] **Step 1: Build all boards** (in the background):
  `pio run -e sunton_70 -e waveshare_43_s3 -e cyd_40 -e cyd_28 -e cyd_28_dual_usb`
  Expected:
  - 5× `SUCCESS`.
  - `copy_bins.py` refreshes all 5 firmware binaries.
  - If any env reports "up to date" and doesn't copy, copy from `.pio/build/<env>/firmware.bin` by hand (this happened with v2.0.6).
- [ ] **Step 2: Check the 2.8" heap budget.** Record the free heap on the 2.8" in K2 mode after 5 minutes of polling, using a one-off `Serial.printf("heap %u\n", ESP.getFreeHeap())` under `PERF_DEBUG`. Expected: above 40 KB. Note the number in the spec's Calls section for phase 2.
- [ ] **Step 3: README.** Add a "Supported printers" section:
  - Snapmaker U1 (full control).
  - Creality K2 / K2 Plus:
    - watch + basic controls
    - CFS shown read-only
    - choose the printer type in the setup portal or on the web page Hardware & Config
    - ports 7125/4408
  - "Anycubic Kobra: coming".
- [ ] **Step 4: Bump the versions** in the manifests and `index.html` to 2.1.0. Expected: `grep -n "2.0.6" web-flasher/` returns nothing.
- [ ] **Checkpoint:**
  - Tell Nate that v2.1.0 is ready to commit, and list the changed files.
  - Remind him the OneDrive repo is his git copy, and offer to copy the files over.
  - Suggest the commit message "v2.1.0: Creality K2 / K2 Plus support with CFS filament tab".
