#include "PrinterParse.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

static void copyStr(char *dst, const char *src, size_t n) {
  if (n == 0) return;
  size_t i = 0;
  if (src) for (; src[i] && i + 1 < n; i++) dst[i] = src[i];
  dst[i] = '\0';
}

PrinterType sanitizePrinterType(int raw) {
  return raw == PT_K2 ? PT_K2 : raw == PT_KOBRA ? PT_KOBRA : PT_U1;
}

const char *printerTypeLabel(PrinterType t) {
  switch (t) {
    case PT_K2: return "CREALITY K2";
    case PT_KOBRA: return "ANYCUBIC KOBRA";
    default: return "SNAPMAKER U1";
  }
}

// Mirrors DecodeMaterial() in the Creality app (Creality/MainWindow.xaml.cs).
void decodeCrealityMaterial(const char *code, char *out, size_t n) {
  if (!code || !code[0] || !strcmp(code, "-1") || !strcmp(code, "0")) { copyStr(out, "Empty", n); return; }
  static const char *const table[][2] = {
    {"001001", "PLA"}, {"001002", "PLA-Silk"}, {"001003", "PLA-CF"}, {"001004", "CR-PLA"},
    {"001005", "Hyper PLA"}, {"002001", "ABS"}, {"005001", "PLA"}, {"006001", "PETG"},
    {"006002", "PETG"}, {"007001", "ABS"}, {"011001", "TPU"}, {"012001", "PA-CF"}, {"013001", "PC"},
    // Generic materials ("0" + HelixScreen's 5-digit id), as the screen writes them.
    {"000001", "PLA"}, {"000003", "PETG"}, {"000004", "ABS"}, {"000005", "TPU"}, {"000006", "PLA-CF"},
    {"000007", "ASA"}, {"000008", "NYLON"}, {"000014", "PETG-CF"}, {"000021", "PC"},
  };
  for (auto &e : table) if (!strcmp(code, e[0])) { copyStr(out, e[1], n); return; }

  char up[32];
  size_t i = 0;
  for (; code[i] && i + 1 < sizeof(up); i++) up[i] = (char)toupper((unsigned char)code[i]);
  up[i] = '\0';
  if (strstr(up, "PLA")) copyStr(out, "PLA", n);
  else if (strstr(up, "PETG")) copyStr(out, "PETG", n);
  else if (strstr(up, "ABS")) copyStr(out, "ABS", n);
  else if (strstr(up, "TPU")) copyStr(out, "TPU", n);
  else if (strstr(up, "PA") || strstr(up, "NYLON")) copyStr(out, "Nylon", n);
  else if (strstr(up, "PC")) copyStr(out, "PC", n);
  else {
    // A Creality code we don't know (e.g. "000001" on a real K2 Plus): say so instead of the number.
    bool digits = true;
    for (const char *p = code; *p; p++) if (*p < '0' || *p > '9') digits = false;
    copyStr(out, digits ? "Other" : code, n);
  }
}

static bool sameNoCase(const char *a, const char *b) {
  for (; *a && *b; a++, b++) if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return false;
  return *a == *b;
}

const char *crealityMaterialCode(const char *type) {
  if (!type || !type[0]) return nullptr;
  static const char *const table[][2] = {
    {"PLA", "000001"}, {"PETG", "000003"}, {"ABS", "000004"}, {"TPU", "000005"}, {"PLA-CF", "000006"},
    {"ASA", "000007"}, {"NYLON", "000008"}, {"PA", "000008"}, {"PETG-CF", "000014"}, {"PC", "000021"},
  };
  for (auto &e : table) if (sameNoCase(type, e[0])) return e[1];
  // A typed name: its family, most specific first ("PETG-CF" before "PETG").
  char up[24];
  size_t i = 0;
  for (; type[i] && i + 1 < sizeof(up); i++) up[i] = (char)toupper((unsigned char)type[i]);
  up[i] = '\0';
  bool cf = strstr(up, "CF") != nullptr;
  if (strstr(up, "PETG")) return cf ? "000014" : "000003";
  if (strstr(up, "PLA")) return cf ? "000006" : "000001";
  if (strstr(up, "ASA")) return "000007";
  if (strstr(up, "ABS")) return "000004";
  if (strstr(up, "TPU")) return "000005";
  if (strstr(up, "NYLON")) return "000008";
  return nullptr;
}

int filamentLoadTemp(const char *type) {
  if (!type) return 220;
  char up[24];
  size_t i = 0;
  for (; type[i] && i + 1 < sizeof(up); i++) up[i] = (char)toupper((unsigned char)type[i]);
  up[i] = '\0';
  // Most specific first: "PETG" before "PET", "ASA"/"ABS" before nothing else.
  if (strstr(up, "PETG")) return 240;
  if (strstr(up, "PLA")) return 220;
  if (strstr(up, "TPU")) return 230;
  if (strstr(up, "ABS") || strstr(up, "ASA")) return 250;
  if (strstr(up, "NYLON") || strstr(up, "PA")) return 260;
  if (strstr(up, "PC")) return 270;
  return 220;
}

uint16_t rgb888To565(uint32_t rgb) {
  return (uint16_t)(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
}

// Accepts "#RRGGBB", "RRGGBB" and Creality's 7-digit "0RRGGBB".
static bool parseColor(const char *s, uint32_t &rgb) {
  if (!s) return false;
  if (*s == '#') s++;
  size_t len = strlen(s);
  if (len == 7) { s++; len = 6; }
  if (len != 6) return false;
  uint32_t v = 0;
  for (size_t i = 0; i < 6; i++) {
    char c = s[i];
    int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
    if (d < 0) return false;
    v = (v << 4) | (uint32_t)d;
  }
  rgb = v;
  return true;
}

static bool unitConnected(JsonVariantConst u) {
  if (u.isNull()) return false;
  JsonVariantConst st = u["state"];
  if (st.isNull()) return false;
  const char *s = st.as<const char *>();
  return !(s && !strcmp(s, "None"));
}

static void fillUnit(JsonVariantConst u, const char *key, FilUnit &f) {
  memset(&f, 0, sizeof(f));
  f.present = true;
  copyStr(f.key, key, sizeof(f.key));
  JsonVariantConst t = u["temperature"], h = u["dry_and_humidity"];
  f.hasEnv = !t.isNull() || !h.isNull();
  f.temp = t.as<float>();
  f.humidity = h.as<float>();

  JsonArrayConst mats = u["material_type"], colors = u["color_value"], remains = u["remain_len"];
  for (int i = 0; i < 4; i++) {
    FilSlot &s = f.slot[i];
    const char *code = (i < (int)mats.size()) ? mats[i].as<const char *>() : nullptr;
    decodeCrealityMaterial(code, s.material, sizeof(s.material));
    s.present = strcmp(s.material, "Empty") != 0;

    s.hasColor = (i < (int)colors.size()) && parseColor(colors[i].as<const char *>(), s.rgb);
    if (!s.hasColor) s.rgb = 0;

    JsonVariantConst r = (i < (int)remains.size()) ? remains[i] : JsonVariantConst();
    if (r.isNull() || !(r.is<int>() || r.is<float>())) s.remainPct = -1;
    else {
      int v = r.as<int>();
      s.remainPct = v < 0 ? -1 : (v > 100 ? 100 : v);
    }
  }
}

int parseK2Box(JsonVariantConst status, FilUnit out[MAX_FIL_UNITS]) {
  for (int i = 0; i < MAX_FIL_UNITS; i++) memset(&out[i], 0, sizeof(FilUnit));
  JsonVariantConst box = status["box"];
  if (box.isNull()) return 0;

  // Unit 1 is T1; the second CFS is on T3 (K2) or T2 — same rule as the Creality app.
  const char *keys[3] = {"T1", "T3", "T2"};
  int n = 0;
  bool haveSecond = false;
  for (const char *k : keys) {
    if (n >= MAX_FIL_UNITS) break;
    bool isSecondKey = strcmp(k, "T1") != 0;
    if (isSecondKey && haveSecond) continue;
    JsonVariantConst u = box[k];
    if (!unitConnected(u)) continue;
    fillUnit(u, k, out[n++]);
    if (isSecondKey) haveSecond = true;
  }

  // The printer's own names for loaded slots: [code, colour, ["T1B", ...], "PLA"].
  for (JsonVariantConst g : box["same_material"].as<JsonArrayConst>()) {
    const char *name = g[3] | "";
    if (!name[0]) continue;
    for (JsonVariantConst sl : g[2].as<JsonArrayConst>()) {
      const char *id = sl | "";   // "T1B"
      if (strlen(id) != 3 || id[2] < 'A' || id[2] > 'D') continue;
      for (int i = 0; i < n; i++) {
        if (out[i].key[0] != id[0] || out[i].key[1] != id[1]) continue;
        FilSlot &s = out[i].slot[id[2] - 'A'];
        if (s.present) copyStr(s.material, name, sizeof(s.material));
      }
    }
  }
  return n;
}

bool filSlotSame(const FilSlot &a, const FilSlot &b) {
  return a.present == b.present && a.hasColor == b.hasColor && a.rgb == b.rgb &&
         a.remainPct == b.remainPct && strcmp(a.material, b.material) == 0;
}

bool k2RemapToolLine(const char *line, int to, char *out, size_t n) {
  if (!line || to <= 0 || line[0] != 'T' || !isdigit((unsigned char)line[1])) return false;
  const char *p = line + 1;
  int tool = 0;
  while (isdigit((unsigned char)*p)) { tool = tool * 10 + (*p - '0'); p++; }
  if (*p != '\0' && *p != '\r' && *p != '\n' && *p != ' ' && *p != ';' && *p != '\t') return false;
  int now;
  if (tool == 0) now = to;
  else if (tool == to) now = 0;
  else return false;
  int w = snprintf(out, n, "T%d%s", now, p);
  return w > 0 && (size_t)w < n;
}

// ---------------- Anycubic Kobra ----------------

void kobraInit(KobraState &s) {
  memset(&s, 0, sizeof(s));
  s.online = true;
  copyStr(s.state, "standby", sizeof(s.state));
  s.remainMin = s.elapsedMin = s.fan = s.speed = -1;
  s.ace.drying = -1;
}

static bool isNum(JsonVariantConst v) { return v.is<int>() || v.is<float>() || v.is<double>(); }

// First numeric value among the keys, looked up in `obj`.
static bool pickNum(JsonVariantConst obj, const char *const *keys, int n, float &out) {
  if (!obj.is<JsonObjectConst>()) return false;
  for (int i = 0; i < n; i++) {
    JsonVariantConst v = obj[keys[i]];
    if (isNum(v)) { out = v.as<float>(); return true; }
  }
  return false;
}

static void readTemps(JsonVariantConst d, KobraState &s) {
  static const char *const noz[] = {"curr_nozzle_temp", "curr_nozzle", "cur_nozzle_temp", "nozzle_temp"};
  static const char *const nozT[] = {"target_nozzle_temp", "target_nozzle"};
  static const char *const bed[] = {"curr_hotbed_temp", "curr_bed", "curr_hotbed", "cur_hotbed_temp", "bed_temp"};
  static const char *const bedT[] = {"target_hotbed_temp", "target_bed", "target_hotbed"};
  float v;
  if (pickNum(d, noz, 4, v)) { s.nozzle = v; s.haveTemps = true; }
  if (pickNum(d, nozT, 2, v)) s.nozzleTarget = v;
  if (pickNum(d, bed, 5, v)) { s.bed = v; s.haveTemps = true; }
  if (pickNum(d, bedT, 3, v)) s.bedTarget = v;
}

static void readSettings(JsonVariantConst d, KobraState &s) {
  static const char *const fan[] = {"fan_speed_pct"};
  static const char *const spd[] = {"print_speed_pct"};
  float v;
  if (pickNum(d, fan, 1, v)) s.fan = (int)v;
  if (pickNum(d, spd, 1, v)) s.speed = (int)v;
}

static const char *mapPrintState(const char *st) {
  if (!st) return nullptr;
  static const char *const printing[] = {"printing", "preheating", "checking", "downloading",
                                          "resuming", "resumed", "updated", "busy", "auto_leveling"};
  for (auto p : printing) if (!strcmp(st, p)) return "printing";
  if (!strcmp(st, "pausing") || !strcmp(st, "paused")) return "paused";
  if (!strcmp(st, "finished") || !strcmp(st, "complete")) return "complete";
  if (!strcmp(st, "stoped") || !strcmp(st, "stopped") || !strcmp(st, "stopping")) return "cancelled";
  if (!strcmp(st, "failed")) return "error";
  return nullptr;
}

static void readAceSlots(JsonArrayConst slots, FilUnit &u) {
  for (JsonVariantConst sl : slots) {
    int idx = sl["index"].is<int>() ? sl["index"].as<int>() : -1;
    if (idx < 0 || idx > 3) continue;
    FilSlot &f = u.slot[idx];
    // Partial updates only overwrite the fields they carry.
    if (sl["type"].is<const char *>()) copyStr(f.material, sl["type"].as<const char *>(), sizeof(f.material));
    f.present = f.material[0] != '\0';
    if (sl["status"].is<int>() && sl["status"].as<int>() == 0) f.present = false;
    JsonArrayConst c = sl["color"];
    if (c.size() >= 3) {
      uint32_t r = c[0].as<uint32_t>() & 0xFF, g = c[1].as<uint32_t>() & 0xFF, b = c[2].as<uint32_t>() & 0xFF;
      f.rgb = (r << 16) | (g << 8) | b; f.hasColor = true;
    } else if (sl["color"].is<const char *>()) {
      f.hasColor = parseColor(sl["color"].as<const char *>(), f.rgb);
    }
    f.remainPct = -1;
  }
}

static bool applyAce(JsonVariantConst data, KobraState &s) {
  JsonArrayConst boxes = data["multi_color_box"];
  if (boxes.isNull() || boxes.size() == 0) return false;
  JsonVariantConst b = boxes[0];               // one ACE per Kobra
  if (!b.is<JsonObjectConst>()) return false;
  FilUnit &u = s.ace;
  if (!s.haveAce) {
    memset(&u, 0, sizeof(u));
    u.drying = -1;
    for (auto &sl : u.slot) sl.remainPct = -1;
    copyStr(u.key, "ACE", sizeof(u.key));
  }
  s.haveAce = true;
  u.present = true;
  if (isNum(b["temp"])) { u.temp = b["temp"].as<float>(); u.hasEnv = true; }
  if (isNum(b["humidity"])) { u.humidity = b["humidity"].as<float>(); u.hasEnv = true; }
  JsonVariantConst dry = b["drying_status"];
  if (dry.is<JsonObjectConst>()) {
    if (dry["status"].is<int>()) u.drying = dry["status"].as<int>() == 1 ? 1 : 0;
    if (isNum(dry["remain_time"])) u.dryRemainMin = dry["remain_time"].as<int>();
  }
  if (b["slots"].is<JsonArrayConst>()) readAceSlots(b["slots"], u);
  return true;
}

// Job fields, shared by "print" reports and the "project" block of info reports.
static void applyJobData(JsonVariantConst d, KobraState &s) {
  const char *fn = d["filename"].as<const char *>();
  if (fn && fn[0]) copyStr(s.file, fn, sizeof(s.file));
  if (isNum(d["progress"])) s.progress = d["progress"].as<float>();
  if (isNum(d["remain_time"])) s.remainMin = d["remain_time"].as<int>();
  if (isNum(d["print_time"])) s.elapsedMin = d["print_time"].as<int>();
}

static void clearJob(KobraState &s) {
  copyStr(s.state, "standby", sizeof(s.state));
  s.file[0] = 0; s.progress = 0; s.remainMin = s.elapsedMin = -1;
}

bool kobraApplyMessage(JsonVariantConst msg, KobraState &s) {
  if (!msg.is<JsonObjectConst>()) return false;
  const char *type = msg["type"].as<const char *>();
  if (!type) return false;
  const char *state = msg["state"].as<const char *>();
  JsonVariantConst data = msg["data"];
  if (data["info"].is<JsonObjectConst>()) data = data["info"];   // nested variant seen in KobraCommandCenter

  if (!strcmp(type, "lastWill")) {
    if (state) s.online = strcmp(state, "offline") != 0;
    return true;
  }

  bool ok = false;
  if (!strcmp(type, "tempature") || !strcmp(type, "temperature")) {
    readTemps(data, s);
    ok = true;
  } else if (!strcmp(type, "fan")) {
    readSettings(data, s);
    ok = true;
  } else if (!strcmp(type, "status")) {
    // "free" means idle (also catches a missed "finished"), but a pause
    // may report free too, so don't blank a paused job.
    if (state && !strcmp(state, "free") && strcmp(s.state, "paused")) {
      copyStr(s.state, "standby", sizeof(s.state));
      s.file[0] = '\0'; s.progress = 0; s.remainMin = s.elapsedMin = -1;
    } else if (state && !strcmp(state, "busy") && !strcmp(s.state, "standby")) {
      copyStr(s.state, "printing", sizeof(s.state));
    }
    ok = true;
  } else if (!strcmp(type, "print")) {
    const char *m = mapPrintState(state);
    if (m) copyStr(s.state, m, sizeof(s.state));
    if (data.is<JsonObjectConst>()) {
      applyJobData(data, s);
      readTemps(data, s);
      readSettings(data, s);
      JsonVariantConst st = data["settings"];
      if (st.is<JsonObjectConst>()) { readTemps(st, s); readSettings(st, s); }
    }
    ok = true;
  } else if (!strcmp(type, "multiColorBox")) {
    ok = applyAce(data, s);
  } else if (!strcmp(type, "info")) {
    // Reply to info/query (real S1 capture): data.temp{}, data.state "free"/"busy",
    // data.project = current job or null; data.last_project is history only.
    if (data.is<JsonObjectConst>()) {
      readTemps(data["temp"], s);
      readTemps(data, s);
      readSettings(data, s);
      JsonVariantConst proj = data["project"];
      if (proj.is<JsonObjectConst>()) {
        const char *m = mapPrintState(proj["state"].as<const char *>());
        if (m) copyStr(s.state, m, sizeof(s.state));
        if (proj["pause"].is<int>() && proj["pause"].as<int>() != 0) copyStr(s.state, "paused", sizeof(s.state));
        applyJobData(proj, s);
      } else {
        // "project": null means the printer has no current job (unless it still says busy).
        const char *ps = data["state"].as<const char *>();
        if (!ps || strcmp(ps, "busy")) clearJob(s);
      }
      ok = true;
    }
  }
  if (ok) s.online = true;
  return ok;
}

// ---------------- Kobra file list, streamed ----------------
// A tiny JSON tokenizer: just enough to pull "action"/"state"/"msg" and each
// record's "filename"/"is_dir" out of a byte stream without holding it all.

void kfsReset(KobraFileScan &s) { memset(&s, 0, sizeof(s)); }

static void kfsValue(KobraFileScan &s, const char *v, bool isString) {
  if (!s.expectValue) return;
  s.expectValue = false;
  if (!strcmp(s.key, "filename") && isString) { copyStr(s.pendName, v, sizeof s.pendName); s.pendHave = true; }
  else if (!strcmp(s.key, "is_dir")) s.pendDir = !strcmp(v, "true");
  else if (!isString) return;
  else if (s.depth == 1 && !strcmp(s.key, "action")) copyStr(s.action, v, sizeof s.action);
  else if (s.depth == 1 && !strcmp(s.key, "state")) copyStr(s.state, v, sizeof s.state);
  else if (s.depth == 1 && !strcmp(s.key, "msg")) copyStr(s.msg, v, sizeof s.msg);
}

static void kfsEndLiteral(KobraFileScan &s) {
  if (!s.litLen) return;
  s.lit[s.litLen] = '\0';
  kfsValue(s, s.lit, false);
  s.litLen = 0;
}

void kfsFeed(KobraFileScan &s, const char *data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    char c = data[i];
    if (s.inStr) {
      if (s.esc) { s.esc = false; }
      else if (c == '\\') { s.esc = true; continue; }
      else if (c == '"') { s.inStr = false; s.tok[s.tokLen] = '\0'; s.strDone = true; continue; }
      if (s.tokLen < (int)sizeof(s.tok) - 1) s.tok[s.tokLen++] = c;
      continue;
    }
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t') { kfsEndLiteral(s); continue; }
    if (s.strDone) {   // a finished string is a key if a ':' follows, else a value
      s.strDone = false;
      if (c == ':') { copyStr(s.key, s.tok, sizeof s.key); s.expectValue = true; continue; }
      kfsValue(s, s.tok, true);
    }
    switch (c) {
      case '"': kfsEndLiteral(s); s.inStr = true; s.tokLen = 0; break;
      case '{':
        s.depth++; s.expectValue = false;
        s.pendHave = s.pendDir = false; s.pendName[0] = '\0';
        break;
      case '}':
        kfsEndLiteral(s);
        if (s.pendHave && !s.pendDir && s.count < KFS_MAX) copyStr(s.names[s.count++], s.pendName, sizeof s.names[0]);
        s.pendHave = s.pendDir = false;
        if (s.depth > 0) s.depth--;
        s.expectValue = false;
        break;
      case '[': s.expectValue = false; break;
      case ']': case ',': kfsEndLiteral(s); s.expectValue = false; break;
      default:
        if (s.expectValue && s.litLen < (int)sizeof(s.lit) - 1) s.lit[s.litLen++] = c;
    }
  }
}
