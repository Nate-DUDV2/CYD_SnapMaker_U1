// Pure-C++ printer data helpers (no Arduino types), unit-tested natively:
//   pio test -e native
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <ArduinoJson.h>

enum PrinterType : uint8_t { PT_U1 = 0, PT_K2 = 1, PT_KOBRA = 2 };

// 0 = U1, 1 = K2, 2 = Kobra; anything else falls back to U1.
PrinterType sanitizePrinterType(int raw);
const char *printerTypeLabel(PrinterType t);

const int MAX_FIL_UNITS = 2;

struct FilSlot {
  bool present;        // material loaded (decoded material != "Empty")
  char material[12];
  bool hasColor;
  uint32_t rgb;        // 0xRRGGBB
  int remainPct;       // 0..100, -1 = unknown
};

struct FilUnit {
  bool present;
  char key[4];         // "T1", "T2", "T3"
  bool hasEnv;
  float temp;
  float humidity;
  int drying;          // Kobra ACE: -1 unknown, 0 idle, 1 drying
  int dryRemainMin;
  FilSlot slot[4];
};

// status = Moonraker result.status. Returns the number of CFS units found (0..2).
int parseK2Box(JsonVariantConst status, FilUnit out[MAX_FIL_UNITS]);
void decodeCrealityMaterial(const char *code, char *out, size_t n);
uint16_t rgb888To565(uint32_t rgb);
// True when two slots would draw identically (used to skip redundant redraws).
bool filSlotSame(const FilSlot &a, const FilSlot &b);

// Slot editing. Creality CFS generic material code for a screen material name
// (FIL_TYPES), or nullptr when the box has no code for it (then only the colour
// is written). Case-insensitive.
const char *crealityMaterialCode(const char *type);
// Nozzle temperature (C) to load/unload a material: matched by family name
// ("Hyper PLA" -> PLA), 220 when unknown.
int filamentLoadTemp(const char *type);

// K2 slot pick. Tool n in K2 G-code loads CFS slot n (T0 = CFS 1 A, T4 = CFS 2 A).
// When the print's T0 should come from tool `to`, a bare "T0" line becomes "T<to>"
// and "T<to>" becomes "T0" (so a multi-colour file keeps its colours apart).
// Returns true and writes the new line to `out` only when the line changes.
bool k2RemapToolLine(const char *line, int to, char *out, size_t n);

// ---------------- Anycubic Kobra (MQTT) ----------------
// The Kobra pushes partial reports ({type, action, state, data}); each one is
// merged into a running KobraState. Field names follow hass-anycubic_cloud
// (real Anycubic traffic) plus the Kobra S1 names from KobraCommandCenter.
struct KobraState {
  bool online;             // false after a lastWill "offline"
  bool haveTemps;
  float nozzle, nozzleTarget, bed, bedTarget;
  char state[12];          // Moonraker words: standby/printing/paused/complete/cancelled/error
  char file[64];           // "" when idle
  float progress;          // 0..100
  int remainMin;           // -1 unknown
  int elapsedMin;          // -1 unknown
  int fan;                 // %, -1 unknown
  int speed;               // %, -1 unknown
  bool haveAce;
  FilUnit ace;
};
void kobraInit(KobraState &s);

// Kobra file list read a byte at a time (boards without PSRAM can't hold the
// ~28 KB listLocal reply). Keeps the top-level action/state/msg and the names of
// records that aren't folders; feed one whole message, read, then reset.
const int KFS_MAX = 40;
struct KobraFileScan {
  char action[16], state[12], msg[48];
  int count;
  char names[KFS_MAX][64];
  // parser state
  int depth;
  bool inStr, esc, strDone, expectValue;
  char tok[64]; int tokLen;
  char lit[8]; int litLen;
  char key[16];
  char pendName[64]; bool pendHave, pendDir;
};
void kfsReset(KobraFileScan &s);
void kfsFeed(KobraFileScan &s, const char *data, size_t len);
// Returns true when the message was recognised and applied.
bool kobraApplyMessage(JsonVariantConst msg, KobraState &s);
