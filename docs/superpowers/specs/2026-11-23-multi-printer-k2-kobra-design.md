# Multi-printer support: Creality K2 (CFS) and Anycubic Kobra (ACE Pro)

Date: 2026-11-23 · Firmware: CYD Snapmaker U1 Screen (currently v2.0.6) · Status: design, review answers folded in, awaiting approval

## Intent and why

Nate owns a **Creality K2 and a K2 Plus, each with two CFS units**, and two Anycubic printers, a
**Kobra S1** and a **Kobra X** (with ACE Pro), besides the Snapmaker U1. He wants the same touchscreen firmware to
**watch** those printers too, so a cheap ESP32 screen can sit next to each one.
Where the U1 shows its four toolheads, the screen should show that printer's
**filament unit**: CFS slots on the K2, ACE Pro slots on the Kobra.

Each screen is dedicated to one printer. The printer type is chosen at first-time
setup and can be changed later from the screen's web page.

## Goals

1. One firmware for all supported boards (2.8" CYD single/dual-USB, 4.0" CYD,
   Waveshare 4.3", Sunton 7") that can be set to **Snapmaker U1**, **Creality K2**
   or **Anycubic Kobra**.
2. Printer type selectable in the Wi-Fi setup portal (first boot) and on the web
   page (Hardware & Config tab). Changing it reboots the screen.
3. For every type, the Home tab shows: printer name + state (incl. "Offline"),
   bed and nozzle temperature, speed/fan where available, current file,
   progress bar and time remaining.
4. The toolhead tab ("T") becomes a **filament tab** for K2 and Kobra:
   - K2 / K2 Plus (same "Creality K2" type, both Moonraker): **all connected
     CFS units, up to 2 (8 slots)**, each slot with material, color and
     remaining amount as a percentage (`remain_len` is a percent, per the
     Creality app), plus each unit's temperature/humidity. A printer with one CFS shows 4.
   - Kobra (S1 and X): 4 slots with material and color, plus drying status if
     available. Each Kobra has a single 4-slot unit (Nate confirmed).
5. K2 keeps the controls that are plain Moonraker G-code: pause/resume, cancel
   (with the existing confirm pop-ups), bed/nozzle temperature presets, speed/fan,
   Move tab jogging and homing.
6. Existing U1 screens behave exactly as in v2.0.6 after updating (default type
   is U1, no re-setup needed).

## Non-goals (this version)

- One screen watching several printers at once (option "B"; may come later and
  should be able to build on this work).
- Any control of the Kobra (pause, temps, jogging, ACE drying). Kobra is
  watch-only in v1.
- Starting prints on K2 or Kobra (the Print tab and the U1 patched-print flow
  stay U1-only).
- More than one CFS unit on the K2 (v1 shows the first one present).
- Cameras.
- Other printer families from Nate's other apps (Voron, Giga, Prusa Mini,
  Centauri). Voron/Giga are plain Moonraker and would mostly work as "K2 without
  CFS", but they are not a goal here.

## Anti-goals (would make this a failure even if it works)

- Any regression for U1 users: touch, polling, settings, patched prints, the web
  page, and v2.0.6 fixes must be unchanged when the type is U1.
- Shipping Anycubic's MQTT client certificate/private key inside the public
  firmware or the public GitHub repo.
- The Kobra's encrypted MQTT connection freezing the UI. All network work stays
  off the UI loop, like the v2.0.6 background poll task.
- Running classic ESP32 boards out of memory (TLS + JSON on a 320 KB RAM chip
  with no PSRAM). The 2.8" CYD must keep working on all three types.

## Current state (what exists)

- `src/main.cpp` (~3,900 lines, single file) drives everything. Board
  differences are compile-time `#if` blocks; screen layout uses `PX()/PY()`
  scaling from a 320x240 design grid.
- Since v2.0.6, printer polling runs in a FreeRTOS task (`printerPollTask`,
  core 0) that fills a `PrinterSnapshot`; the UI loop applies it with
  `applyPrinterSnapshot()` and redraws with `updateDynamicUI()`. Commands go out
  through `sendGcode()` (Moonraker `/printer/gcode/script`).
- U1-specific pieces: 4 extruders (`extruder`..`extruder3`), `print_task_config`
  filament sync (`/printer/filament_detect/set`), `pick_extruder`/
  `park_extruder`/`ACTIVATE_EXTRUDER`/`AUTO_FEEDING`/`INNER_FILAMENT_UNLOAD`
  macros, the Active Tool pop-up, and the Print tab's patched-print flow
  (`print_task_config` variables, T0 remapping).
- Settings live in NVS (`Preferences`, namespace `"printer"`). The Wi-Fi setup
  portal is WiFiManager with custom parameters for printer IP and name.

### What Nate's desktop apps show about the two printers

**K2 (`Creality` project):** Moonraker on port **7125** (websocket and HTTP);
Creality's nginx proxy on **4408** is used as a fallback for G-code. CFS data is
the Moonraker object **`box`**, keyed by unit (`T1`, then `T2`/`T3`...). Each unit
has `state` (`"None"` when absent), `material_type[4]`, `remain_len[4]`,
`temperature`, `dry_and_humidity`. The app does not read slot **colors**, so the
color field name is still unknown.

**Kobra (`KobraCommandCenter` project):**
1. `GET http://IP:18910/info` returns `token` (32+ chars), `modelName`, `cn`.
2. Signature: `sign = md5( md5(token[0..16]) + ts + nonce )`, with `ts` in Unix
   ms and `nonce` a 6-char random alphanumeric string.
3. `POST http://IP:18910/ctrl?ts=..&nonce=..&sign=..&did=random` with body `{}`
   returns `data.info` (base64) and `data.token` (the IV).
4. AES-128-CBC/PKCS7 decrypt with key = `token[16..32]` (UTF-8) and IV =
   `data.token` gives JSON with `deviceId`, `username`, `password`, `modeId`.
5. MQTT over **TLS 1.2 on port 9883**, server certificate not verified,
   **client certificate** from `client.pfx` (PKCS#12, password-protected),
   client id = `deviceId`, user/pass from step 4.
6. Subscriptions: `anycubic/anycubicCloud/v1/{printer,printer/public,web,slicer}/…/{modelId}/{deviceId}/#`.
   Commands are published to `anycubic/anycubicCloud/v1/{web|slicer}/printer/{modelId}/{deviceId}/{type}`
   with `{type, action, msgid, timestamp, data}`.
7. **Unknowns:** the app's telemetry and ACE parsing guess between several field
   names (`curr_nozzle_temp`/`nozzle_temp`/..., `rfid`/`ace`/`slots`/`boxes`), and
   its status "poll" is only a simulation. So it is not confirmed whether the
   printer pushes status by itself or must be asked, or what the real field
   names are.

## Approaches considered

| Approach | Trade-off | Decision |
|---|---|---|
| **A. One firmware, printer type chosen at runtime** (setup portal + web page); a small per-type "backend" fills the existing `PrinterSnapshot` | One binary per board, as today. Slightly more code paths in `main.cpp`, and the Kobra TLS code is compiled into every build | **Chosen.** It's the only option that allows choosing the printer at setup, which Nate asked for |
| B. Separate firmware builds per printer type (more PlatformIO environments) | Smaller binaries, but 3× the web-flasher entries and no choosing at setup | Rejected: conflicts with choosing at setup/web |
| C. PC-side bridge that translates the Kobra into Moonraker for the screen | Keeps the ESP32 simple, but needs a PC always running | Rejected: the screen is meant to be standalone |
| D. Run Rinkhals (community Kobra firmware that adds Moonraker) | Would make the Kobra "just Moonraker" | Rejected for now: Nate runs stock Kobra firmware; worth revisiting if Kobra MQTT proves too heavy |

Separately, Nate chose **one screen per printer** (option A in conversation)
over one screen watching several printers (B), as the smaller first step.

## Design (decided parts)

### Printer type setting
- New NVS key `printerType`: 0 = U1 (default, so existing screens are
  unaffected), 1 = K2, 2 = Kobra.
- **Setup portal:** a "Printer type" dropdown next to the existing printer IP and
  name fields.
- **Web page:** the same dropdown on the Hardware & Config tab. Saving with a
  changed type reboots, as the save already does.
- The boot screen and Home header show the type, e.g. "CREALITY K2".

### Backends
The v2.0.6 poll task stays the single place that talks to the printer. It
dispatches on `printerType` and fills the same `PrinterSnapshot`, extended with a
**filament unit** block: up to 2 units × 4 slots of {present, material, color, remaining}
plus unit temperature, humidity and status text. The UI never talks to the
network directly; only `sendGcode()` does (U1/K2) and is unchanged.

- **U1:** exactly today's code.
- **K2:** Moonraker like the U1, but querying a single `extruder` plus `box`,
  without `extruder1..3` or `print_task_config`. Port 7125; if that fails, try
  4408 and remember which one worked. Slicer time-left lookup works unchanged.
- **Kobra:**
  - **Both Kobra S1 and Kobra X are supported** under the single "Anycubic
    Kobra" type. The model is detected from pairing (`modeId`: 20025 = S1,
    20030 = X, matching KobraCommandCenter), not picked by the user. The
    telemetry field names differ by model (S1: `curr_nozzle`/`curr_bed`, plus a
    chamber temperature; X: `curr_nozzle_temp`/`curr_hotbed_temp`, no chamber),
    so the parser handles both, per KobraCommandCenter's unit tests. The spike
    captures from both printers.
  - Pairing (steps 1–4 above) runs automatically when type = Kobra and no
    credentials are cached, or when MQTT login is refused. Credentials are cached
    in NVS.
  - The MQTT/TLS connection runs in the background task, using the client
    certificate and key stored on the screen. Incoming status messages update the
    snapshot. The connection reconnects on its own; after repeated failures the
    Home screen shows "Offline", like the other types.
  - If the printer needs to be asked for status, the task publishes that request
    every few seconds (exact message to be confirmed, see below).
  - No commands are published in v1 (watch-only).

### Client certificate handling
- The certificate and key are **never** in the firmware or the repo.
- On the web page, the Kobra section has an upload for the client certificate and
  key in **PEM** form. They're stored on the screen in NVS or flash storage,
  whichever the builder prefers.
- The README gets a short how-to: where `client.pfx` comes from, and the one
  `openssl` command to convert it to PEM (cert + key).
- Without a certificate, the Kobra screen says "Upload Kobra certificate on the
  web page" instead of trying to connect.

### Screen changes per type

| Area | U1 | K2 | Kobra |
|---|---|---|---|
| Sidebar "T" tab | Toolheads (as today) | **Filament: CFS** | **Filament: ACE Pro** |
| Home: active-tool card | Active tool pop-up | Shows nozzle temp, no pop-up | Shows nozzle temp, no pop-up |
| Home: temperature presets | ✓ | ✓ (single nozzle) | Hidden (watch-only) |
| Home: speed/fan pop-up | ✓ | ✓ | Shows values, no pop-up |
| Home: pause/cancel | ✓ | ✓ | Hidden |
| Move tab | ✓ | ✓ | Hidden from the sidebar |
| Print tab | ✓ | Hidden | Hidden |
| Web dashboard temp/speed controls | ✓ | ✓ | Read-only |
| Web filament form | ✓ | Hidden (CFS is read-only) | Hidden |

Filament tab layout: reuses the four tool cards (color circle, material text,
number), adds remaining amount where known, and shows a header line with unit
temperature/humidity (K2) or drying state (Kobra). With two CFS units, slots
are labelled 1A–1D / 2A–2D. On the 4.3"/7" both units show at once (two rows of
4); on the 2.8"/4.0" a "CFS 1 / CFS 2" toggle in the header switches between
them (Claude's call; the builder may adjust within this). An empty slot shows "—" and a
grey circle. A missing unit shows "No CFS / ACE detected".

## First step: confirm the unknowns (spike)

Before the Kobra and CFS parsing is finalized:
1. **K2:** dump one real `box` object (via `/printer/objects/query?box`) to learn
   the slot color field and how empty slots look.
2. **Kobra:** a throwaway capture script on the PC (Python, using the existing
   `client.pfx` and the pairing steps above) that connects, subscribes, and logs
   every message for a few minutes while idle and while printing. It answers:
   does the printer push status unprompted, which topic and field names carry
   temps/state/progress/ACE slots, and what a status request looks like if one is
   needed. It should also confirm the TLS handshake works without the server
   certificate being verified, since the ESP32 will do the same.

The real field names from these captures go into the parser. If the Kobra
capture shows the stock MQTT path is unworkable, stop and revisit approach D
(Rinkhals) with Nate before building the Kobra backend.

## Constraints

- **Classic ESP32 (2.8"/4.0" CYD):** no PSRAM; MQTT/TLS needs roughly 40–50 KB of
  heap during the handshake. JSON parsing must use ArduinoJson filters, not whole
  documents. Firmware is about 1.48 MB of a 3 MB app partition, so there's room.
- **Library choice is the builder's**, e.g. PubSubClient or ESP-IDF `esp-mqtt`
  over `WiFiClientSecure`. It must be non-blocking from the UI's point of view
  (runs in the poll task).
- The poll task stack (12 KB) will likely need to grow for TLS.
- Keep the single-file structure unless splitting the backends into separate files
  is clearly simpler. Builder's call.

## Answers from Nate (review round)

- **Nothing else stays connected to the Kobras;** the screen is for watching
  only. So the screen logs in with the paired `deviceId` as its client id and
  doesn't need to share the connection with KobraCommandCenter or the slicer. If
  Nate opens one of those while the screen is on, a brief drop and reconnect is
  acceptable.
- **One ACE Pro per Kobra; the Kobra X has 4 slots.**
- **Two K2-family printers (K2 and K2 Plus), each with dual CFS.** Both are the
  "Creality K2" type; the filament tab shows both CFS units (8 slots).

## Calls made during review (Claude's calls, correct if wrong)

- **Phasing:** K2 (plus the shared printer-type setting, setup/web dropdown and
  filament tab) ships as its own release. Kobra follows once its spike passes. A
  stalled Kobra never blocks K2.
- **Spike runs from Nate's PC:** Claude writes and runs the capture scripts there,
  with Nate turning printers on and starting a print when asked.
- **Settings stay single-printer:** one `printerType` key, with Kobra
  credentials/cert stored globally. No per-printer indexed settings for a future
  "watch several printers" mode. If that mode comes, it migrates settings then.
- **Memory on classic ESP32:** when type is Kobra on a no-PSRAM board, the radar
  tab is disabled, so no second TLS session sits next to the MQTT one. The budget
  is measured (free heap with MQTT connected) during the first Kobra build on the
  2.8". If it doesn't fit, Kobra is supported on the 4.3"/7" only and the 2.8"/4.0"
  setup hides the Kobra option (Nate didn't object when asked).
- **Where `client.pfx` comes from:** Claude traces it while writing the README
  (likely extracted from the Anycubic Slicer install) and confirms with Nate then.

## Left to the builder

- How the backend dispatch is structured in code (switch vs function pointers vs
  small classes).
- WiFiManager dropdown mechanics (custom HTML parameter).
- Where the PEM is stored (NVS vs LittleFS) and the MQTT library.
- Exact visual layout of the filament tab within the existing card style.
- Whether Voron/Giga happen to work as "K2 without CFS". A nice side effect, not
  a requirement.

## Testing

- **Hardware available:** Nate's 2.8" single-USB CYD and Sunton 7". Nate's K2 and
  K2 Plus, Kobra S1 and Kobra X are on the same LAN as the screens. The CFS spike
  dumps `box` from both K2s to see how the second unit appears.
- Each type is tested on both screens: setup portal selection, web selection,
  Home data, filament tab, offline behavior (printer off), reboot persistence.
- **U1 regression:** a v2.0.6 screen updated to this firmware must come up as U1
  with all settings and calibration intact.
- Kobra-specific: first-time pairing, certificate missing, certificate wrong, and
  printer rebooting while the screen is connected (reconnect).
