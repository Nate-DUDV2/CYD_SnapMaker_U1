#include <unity.h>
#include <string.h>
#include <stdio.h>
#include <ArduinoJson.h>
#include <PrinterParse.h>
#include "k2_box_fixtures.h"
#include "kobra_fixtures.h"

void setUp() {}
void tearDown() {}

static int parse(const char *json, FilUnit out[MAX_FIL_UNITS]) {
  static JsonDocument doc;
  doc.clear();
  TEST_ASSERT_FALSE(deserializeJson(doc, json));
  return parseK2Box(doc.as<JsonVariantConst>(), out);
}

void test_sanitize_type() {
  TEST_ASSERT_EQUAL(PT_U1, sanitizePrinterType(0));
  TEST_ASSERT_EQUAL(PT_K2, sanitizePrinterType(1));
  TEST_ASSERT_EQUAL(PT_U1, sanitizePrinterType(255));
  TEST_ASSERT_EQUAL(PT_U1, sanitizePrinterType(-1));
}

void test_label() {
  TEST_ASSERT_EQUAL_STRING("CREALITY K2", printerTypeLabel(PT_K2));
  TEST_ASSERT_EQUAL_STRING("SNAPMAKER U1", printerTypeLabel(PT_U1));
}

void test_decode_material() {
  char b[12];
  decodeCrealityMaterial("001001", b, sizeof(b)); TEST_ASSERT_EQUAL_STRING("PLA", b);
  decodeCrealityMaterial("006002", b, sizeof(b)); TEST_ASSERT_EQUAL_STRING("PETG", b);
  decodeCrealityMaterial("-1", b, sizeof(b));     TEST_ASSERT_EQUAL_STRING("Empty", b);
  decodeCrealityMaterial("", b, sizeof(b));       TEST_ASSERT_EQUAL_STRING("Empty", b);
  decodeCrealityMaterial(nullptr, b, sizeof(b));  TEST_ASSERT_EQUAL_STRING("Empty", b);
  decodeCrealityMaterial("XYZ-PETG-9", b, sizeof(b)); TEST_ASSERT_EQUAL_STRING("PETG", b);
  decodeCrealityMaterial("999999", b, sizeof(b)); TEST_ASSERT_EQUAL_STRING("Other", b);
  decodeCrealityMaterial("ABCDEFGHIJKLMNOP", b, sizeof(b)); TEST_ASSERT_EQUAL(11, (int)strlen(b));
}

void test_rgb565() {
  TEST_ASSERT_EQUAL_HEX16(0xF800, rgb888To565(0xFF0000));
  TEST_ASSERT_EQUAL_HEX16(0x07E0, rgb888To565(0x00FF00));
  TEST_ASSERT_EQUAL_HEX16(0x001F, rgb888To565(0x0000FF));
}

void test_box_dual() {
  FilUnit u[MAX_FIL_UNITS];
  TEST_ASSERT_EQUAL(2, parse(K2_BOX_DUAL, u));
  TEST_ASSERT_EQUAL_STRING("T1", u[0].key);
  TEST_ASSERT_EQUAL_STRING("T3", u[1].key);
  TEST_ASSERT_TRUE(u[0].hasEnv);
  TEST_ASSERT_EQUAL_FLOAT(27, u[0].temp);
  TEST_ASSERT_EQUAL_FLOAT(41, u[0].humidity);
  TEST_ASSERT_EQUAL_STRING("PLA", u[0].slot[0].material);
  TEST_ASSERT_EQUAL(80, u[0].slot[0].remainPct);
  TEST_ASSERT_EQUAL_STRING("PETG", u[0].slot[1].material);
  TEST_ASSERT_FALSE(u[0].slot[2].present);
  TEST_ASSERT_EQUAL_STRING("TPU", u[0].slot[3].material);
  TEST_ASSERT_EQUAL_STRING("ABS", u[1].slot[0].material);
  TEST_ASSERT_EQUAL_STRING("PLA-Silk", u[1].slot[1].material);
  TEST_ASSERT_EQUAL_STRING("PC", u[1].slot[2].material);
  TEST_ASSERT_EQUAL(95, u[1].slot[2].remainPct);
  TEST_ASSERT_FALSE(u[1].slot[3].present);
}

void test_box_single() {
  FilUnit u[MAX_FIL_UNITS];
  TEST_ASSERT_EQUAL(1, parse(K2_BOX_SINGLE, u));
  TEST_ASSERT_EQUAL_STRING("T1", u[0].key);
}

void test_box_none() {
  FilUnit u[MAX_FIL_UNITS];
  TEST_ASSERT_EQUAL(0, parse(K2_BOX_NONE, u));
}

void test_box_t3_only() {
  FilUnit u[MAX_FIL_UNITS];
  const char *j = R"({"box":{"T1":{"state":"None"},"T3":{"state":"connect","material_type":["001001","-1","",null],"remain_len":[50,-1,150,null]}}})";
  TEST_ASSERT_EQUAL(1, parse(j, u));
  TEST_ASSERT_EQUAL_STRING("T3", u[0].key);
  TEST_ASSERT_FALSE(u[0].hasEnv);
  TEST_ASSERT_TRUE(u[0].slot[0].present);
  TEST_ASSERT_EQUAL_STRING("PLA", u[0].slot[0].material);
  TEST_ASSERT_EQUAL(50, u[0].slot[0].remainPct);
  TEST_ASSERT_FALSE(u[0].slot[1].present);
  TEST_ASSERT_EQUAL(-1, u[0].slot[1].remainPct);
  TEST_ASSERT_FALSE(u[0].slot[2].present);
  TEST_ASSERT_EQUAL(100, u[0].slot[2].remainPct);
  TEST_ASSERT_FALSE(u[0].slot[3].present);
  TEST_ASSERT_EQUAL(-1, u[0].slot[3].remainPct);
}

void test_box_short_arrays() {
  FilUnit u[MAX_FIL_UNITS];
  const char *j = R"({"box":{"T1":{"state":"connect","material_type":["001001","006001"],"remain_len":[10]}}})";
  TEST_ASSERT_EQUAL(1, parse(j, u));
  TEST_ASSERT_TRUE(u[0].slot[1].present);
  TEST_ASSERT_EQUAL(-1, u[0].slot[1].remainPct);
  TEST_ASSERT_FALSE(u[0].slot[2].present);
  TEST_ASSERT_FALSE(u[0].slot[3].present);
}

void test_color() {
  FilUnit u[MAX_FIL_UNITS];
  parse(K2_BOX_DUAL, u);
  TEST_ASSERT_TRUE(u[0].slot[0].hasColor);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000, u[0].slot[0].rgb);
  TEST_ASSERT_FALSE(u[0].slot[2].hasColor);
  // Other plausible encodings: bare hex and Creality's 7-digit "0RRGGBB".
  const char *j = R"({"box":{"T1":{"state":"connect","material_type":["001001","001001","001001"],"color_value":["00FF00","0123456","zz"]}}})";
  parse(j, u);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00, u[0].slot[0].rgb);
  TEST_ASSERT_EQUAL_HEX32(0x123456, u[0].slot[1].rgb);
  TEST_ASSERT_FALSE(u[0].slot[2].hasColor);
}

void test_slot_same() {
  FilUnit a[MAX_FIL_UNITS], b[MAX_FIL_UNITS];
  parse(K2_BOX_DUAL, a);
  parse(K2_BOX_DUAL, b);
  TEST_ASSERT_TRUE(filSlotSame(a[0].slot[0], b[0].slot[0]));
  b[0].slot[0].remainPct = 79;
  TEST_ASSERT_FALSE(filSlotSame(a[0].slot[0], b[0].slot[0]));
  parse(K2_BOX_DUAL, b);
  b[0].slot[1].rgb = 0x123456;
  TEST_ASSERT_FALSE(filSlotSame(a[0].slot[1], b[0].slot[1]));
  parse(K2_BOX_DUAL, b);
  strcpy(b[1].slot[0].material, "PETG");
  TEST_ASSERT_FALSE(filSlotSame(a[1].slot[0], b[1].slot[0]));
}

// ---------------- Kobra ----------------
static bool kb(const char *json, KobraState &s) {
  static JsonDocument doc;
  doc.clear();
  if (deserializeJson(doc, json)) return kobraApplyMessage(JsonVariantConst(), s);
  return kobraApplyMessage(doc.as<JsonVariantConst>(), s);
}

void test_kobra_sanitize() {
  TEST_ASSERT_EQUAL(PT_KOBRA, sanitizePrinterType(2));
  TEST_ASSERT_EQUAL_STRING("ANYCUBIC KOBRA", printerTypeLabel(PT_KOBRA));
}

void test_kobra_init() {
  KobraState s; kobraInit(s);
  TEST_ASSERT_TRUE(s.online);
  TEST_ASSERT_FALSE(s.haveTemps);
  TEST_ASSERT_FALSE(s.haveAce);
  TEST_ASSERT_EQUAL_STRING("standby", s.state);
  TEST_ASSERT_EQUAL(-1, s.remainMin);
}

void test_kobra_temps_x() {
  KobraState s; kobraInit(s);
  TEST_ASSERT_TRUE(kb(KB_TEMP_X, s));
  TEST_ASSERT_TRUE(s.haveTemps);
  TEST_ASSERT_EQUAL_FLOAT(220.5, s.nozzle);
  TEST_ASSERT_EQUAL_FLOAT(220, s.nozzleTarget);
  TEST_ASSERT_EQUAL_FLOAT(64.8, s.bed);
  TEST_ASSERT_EQUAL_FLOAT(65, s.bedTarget);
}

void test_kobra_temps_s1() {
  KobraState s; kobraInit(s);
  TEST_ASSERT_TRUE(kb(KB_TEMP_S1, s));
  TEST_ASSERT_EQUAL_FLOAT(215.4, s.nozzle);
  TEST_ASSERT_EQUAL_FLOAT(215, s.nozzleTarget);
  TEST_ASSERT_EQUAL_FLOAT(60.1, s.bed);
  TEST_ASSERT_EQUAL_FLOAT(60, s.bedTarget);
}

void test_kobra_print_flow() {
  KobraState s; kobraInit(s);
  TEST_ASSERT_TRUE(kb(KB_PRINTING, s));
  TEST_ASSERT_EQUAL_STRING("printing", s.state);
  TEST_ASSERT_EQUAL_STRING("benchy.gcode", s.file);
  TEST_ASSERT_EQUAL_FLOAT(42, s.progress);
  TEST_ASSERT_EQUAL(35, s.remainMin);
  TEST_ASSERT_EQUAL(25, s.elapsedMin);
  kb(KB_PAUSED, s);
  TEST_ASSERT_EQUAL_STRING("paused", s.state);
  TEST_ASSERT_EQUAL_STRING("benchy.gcode", s.file);   // kept from earlier message
  TEST_ASSERT_EQUAL_FLOAT(43, s.progress);
  kb(KB_FINISHED, s);
  TEST_ASSERT_EQUAL_STRING("complete", s.state);
  TEST_ASSERT_EQUAL(0, s.remainMin);
  kb(KB_STOPPED, s);
  TEST_ASSERT_EQUAL_STRING("cancelled", s.state);
  kb(KB_FAILED, s);
  TEST_ASSERT_EQUAL_STRING("error", s.state);
}

void test_kobra_status_free() {
  KobraState s; kobraInit(s);
  kb(KB_PRINTING, s);
  TEST_ASSERT_TRUE(kb(KB_FREE, s));
  TEST_ASSERT_EQUAL_STRING("standby", s.state);
  TEST_ASSERT_EQUAL_STRING("", s.file);
  TEST_ASSERT_EQUAL(-1, s.remainMin);
}

void test_kobra_updated_and_fan() {
  KobraState s; kobraInit(s);
  TEST_ASSERT_TRUE(kb(KB_UPDATED, s));
  TEST_ASSERT_EQUAL_FLOAT(200, s.nozzle);
  TEST_ASSERT_EQUAL_FLOAT(205, s.nozzleTarget);
  TEST_ASSERT_EQUAL_FLOAT(60, s.bedTarget);
  TEST_ASSERT_EQUAL(80, s.fan);
  TEST_ASSERT_EQUAL(150, s.speed);
  TEST_ASSERT_TRUE(kb(KB_FAN, s));
  TEST_ASSERT_EQUAL(55, s.fan);
}

void test_kobra_ace_info() {
  KobraState s; kobraInit(s);
  TEST_ASSERT_TRUE(kb(KB_ACE_INFO, s));
  TEST_ASSERT_TRUE(s.haveAce);
  TEST_ASSERT_TRUE(s.ace.present);
  TEST_ASSERT_TRUE(s.ace.hasEnv);
  TEST_ASSERT_EQUAL_FLOAT(31, s.ace.temp);
  TEST_ASSERT_EQUAL_FLOAT(28, s.ace.humidity);
  TEST_ASSERT_EQUAL(0, s.ace.drying);
  TEST_ASSERT_EQUAL_STRING("PLA", s.ace.slot[0].material);
  TEST_ASSERT_TRUE(s.ace.slot[0].hasColor);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000, s.ace.slot[0].rgb);
  TEST_ASSERT_EQUAL_STRING("PETG", s.ace.slot[1].material);
  TEST_ASSERT_FALSE(s.ace.slot[2].present);
  TEST_ASSERT_TRUE(s.ace.slot[3].present);
  TEST_ASSERT_EQUAL_STRING("PLA+", s.ace.slot[3].material);
  TEST_ASSERT_EQUAL(-1, s.ace.slot[0].remainPct);
}

void test_kobra_ace_partial_dry() {
  KobraState s; kobraInit(s);
  kb(KB_ACE_INFO, s);
  TEST_ASSERT_TRUE(kb(KB_ACE_DRY, s));
  TEST_ASSERT_EQUAL(1, s.ace.drying);
  TEST_ASSERT_EQUAL(180, s.ace.dryRemainMin);
  TEST_ASSERT_EQUAL_FLOAT(45, s.ace.temp);
  TEST_ASSERT_EQUAL_FLOAT(28, s.ace.humidity);           // kept
  TEST_ASSERT_EQUAL_STRING("PLA", s.ace.slot[0].material); // slots kept
  TEST_ASSERT_TRUE(s.ace.slot[1].present);
}

// Slot editing: the screen's material names -> Creality CFS generic codes ("0" +
// HelixScreen's 5-digit id; "000001" is what a real K2 Plus reports as PLA).
void test_creality_material_codes() {
  TEST_ASSERT_EQUAL_STRING("000001", crealityMaterialCode("PLA"));
  TEST_ASSERT_EQUAL_STRING("000003", crealityMaterialCode("PETG"));
  TEST_ASSERT_EQUAL_STRING("000004", crealityMaterialCode("ABS"));
  TEST_ASSERT_EQUAL_STRING("000005", crealityMaterialCode("TPU"));
  TEST_ASSERT_EQUAL_STRING("000007", crealityMaterialCode("ASA"));
  TEST_ASSERT_EQUAL_STRING("000021", crealityMaterialCode("PC"));
  TEST_ASSERT_EQUAL_STRING("000008", crealityMaterialCode("NYLON"));
  TEST_ASSERT_EQUAL_STRING("000006", crealityMaterialCode("PLA-CF"));
  TEST_ASSERT_EQUAL_STRING("000014", crealityMaterialCode("PETG-CF"));
  TEST_ASSERT_EQUAL_STRING("000001", crealityMaterialCode("pla"));   // case-insensitive
  TEST_ASSERT_NULL(crealityMaterialCode("WOOD"));                   // no code: colour-only write
  TEST_ASSERT_NULL(crealityMaterialCode(""));
  // Typed (custom) names fall back to their family; the CFS stores codes, not text.
  TEST_ASSERT_EQUAL_STRING("000001", crealityMaterialCode("PLA+"));
  TEST_ASSERT_EQUAL_STRING("000001", crealityMaterialCode("SILK PLA"));
  TEST_ASSERT_EQUAL_STRING("000003", crealityMaterialCode("PETG HF"));
  TEST_ASSERT_EQUAL_STRING("000014", crealityMaterialCode("PETG-CF HS"));
  TEST_ASSERT_EQUAL_STRING("000006", crealityMaterialCode("PLA CF"));
  TEST_ASSERT_EQUAL_STRING("000007", crealityMaterialCode("ASA AERO"));
  TEST_ASSERT_EQUAL_STRING("000005", crealityMaterialCode("TPU 95A"));
  TEST_ASSERT_NULL(crealityMaterialCode("MYSTERY"));
  // ...and the codes read back as names.
  char b[16];
  decodeCrealityMaterial("000003", b, sizeof b); TEST_ASSERT_EQUAL_STRING("PETG", b);
  decodeCrealityMaterial("000007", b, sizeof b); TEST_ASSERT_EQUAL_STRING("ASA", b);
  decodeCrealityMaterial("000021", b, sizeof b); TEST_ASSERT_EQUAL_STRING("PC", b);
  decodeCrealityMaterial("000014", b, sizeof b); TEST_ASSERT_EQUAL_STRING("PETG-CF", b);
}

void test_filament_load_temp() {
  TEST_ASSERT_EQUAL(220, filamentLoadTemp("PLA"));
  TEST_ASSERT_EQUAL(220, filamentLoadTemp("PLA-CF"));
  TEST_ASSERT_EQUAL(240, filamentLoadTemp("PETG"));
  TEST_ASSERT_EQUAL(240, filamentLoadTemp("PETG-CF"));
  TEST_ASSERT_EQUAL(230, filamentLoadTemp("TPU"));
  TEST_ASSERT_EQUAL(250, filamentLoadTemp("ABS"));
  TEST_ASSERT_EQUAL(250, filamentLoadTemp("ASA"));
  TEST_ASSERT_EQUAL(270, filamentLoadTemp("PC"));
  TEST_ASSERT_EQUAL(260, filamentLoadTemp("NYLON"));
  TEST_ASSERT_EQUAL(220, filamentLoadTemp("Hyper PLA"));   // contains PLA
  TEST_ASSERT_EQUAL(220, filamentLoadTemp("Other"));       // unknown: safe PLA default
  TEST_ASSERT_EQUAL(220, filamentLoadTemp(nullptr));
}

// Shape of a real S1 file/listLocal reply (records: filename, is_dir, size, timestamp, plate_number).
static const char *KFS_LIST = R"({"type":"file","action":"listLocal","msgid":"a1","state":"done","code":200,"msg":"done","data":{"records":[
  {"filename":"cube.gcode","is_dir":false,"size":12345,"timestamp":1732400000,"plate_number":1},
  {"filename":"models","is_dir":true,"size":0,"timestamp":1732400001,"plate_number":0},
  {"filename":"say \"hi\" \\ part.gcode","is_dir":false,"size":7,"timestamp":1,"plate_number":1},
  {"is_dir":false,"filename":"order swapped.gcode","size":1,"timestamp":2,"plate_number":1}],"path":"/"},"timestamp":1732400099})";

static void feedInChunks(KobraFileScan &s, const char *json, size_t chunk) {
  size_t n = strlen(json);
  for (size_t i = 0; i < n; i += chunk) kfsFeed(s, json + i, (n - i) < chunk ? (n - i) : chunk);
}

void test_kobra_file_scan() {
  static KobraFileScan s;
  size_t chunks[] = {1, 7, 4096};
  for (size_t c : chunks) {
    kfsReset(s);
    feedInChunks(s, KFS_LIST, c);
    TEST_ASSERT_EQUAL_STRING("listLocal", s.action);
    TEST_ASSERT_EQUAL_STRING("done", s.state);
    TEST_ASSERT_EQUAL(3, s.count);
    TEST_ASSERT_EQUAL_STRING("cube.gcode", s.names[0]);
    TEST_ASSERT_EQUAL_STRING("say \"hi\" \\ part.gcode", s.names[1]);
    TEST_ASSERT_EQUAL_STRING("order swapped.gcode", s.names[2]);
  }
}

void test_kobra_file_scan_limits() {
  static KobraFileScan s;
  kfsReset(s);
  const char *head = "{\"action\":\"listUdisk\",\"state\":\"failed\",\"msg\":\"no udisk\",\"data\":{\"records\":[";
  kfsFeed(s, head, strlen(head));
  char rec[200];
  for (int i = 0; i < 50; i++) {
    int n = snprintf(rec, sizeof rec, "%s{\"filename\":\"%d_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.gcode\",\"is_dir\":false}", i ? "," : "", i);
    kfsFeed(s, rec, n);
  }
  kfsFeed(s, "]}}", 3);
  TEST_ASSERT_EQUAL_STRING("listUdisk", s.action);
  TEST_ASSERT_EQUAL_STRING("failed", s.state);
  TEST_ASSERT_EQUAL_STRING("no udisk", s.msg);
  TEST_ASSERT_EQUAL(KFS_MAX, s.count);
  TEST_ASSERT_EQUAL(63, (int)strlen(s.names[0]));   // long names are cut, not overflowed
  TEST_ASSERT_EQUAL_STRING_LEN("0_aaa", s.names[0], 5);
  // A new message after reset starts clean.
  kfsReset(s);
  kfsFeed(s, "{\"action\":\"query\"}", strlen("{\"action\":\"query\"}"));
  TEST_ASSERT_EQUAL(0, s.count);
  TEST_ASSERT_EQUAL_STRING("query", s.action);
}

void test_kobra_lastwill_and_garbage() {
  KobraState s; kobraInit(s);
  TEST_ASSERT_TRUE(kb(KB_LASTWILL_OFF, s));
  TEST_ASSERT_FALSE(s.online);
  kb(KB_TEMP_X, s);
  TEST_ASSERT_TRUE(s.online);                            // any report means it's back
  TEST_ASSERT_FALSE(kb("not json", s));
  TEST_ASSERT_FALSE(kb("[1,2,3]", s));
  TEST_ASSERT_FALSE(kb(R"({"type":"camera","action":"x"})", s));
  TEST_ASSERT_FALSE(kb(R"({"type":"multiColorBox","data":{"multi_color_box":"oops"}})", s));
  TEST_ASSERT_EQUAL_FLOAT(220.5, s.nozzle);              // untouched by the junk
}

void test_kobra_free_while_paused_keeps_job() {
  KobraState s; kobraInit(s);
  kb(KB_PRINTING, s);
  kb(KB_PAUSED, s);
  kb(KB_FREE, s);
  TEST_ASSERT_EQUAL_STRING("paused", s.state);
  TEST_ASSERT_EQUAL_STRING("benchy.gcode", s.file);
}

void test_kobra_partial_slot_keeps_material() {
  KobraState s; kobraInit(s);
  kb(KB_ACE_INFO, s);
  kb(R"({"type":"multiColorBox","action":"refresh","state":"success","data":{"multi_color_box":[{"id":0,"slots":[{"index":0,"color":[0,0,255]},{"index":1,"status":5}]}]}})", s);
  TEST_ASSERT_EQUAL_STRING("PLA", s.ace.slot[0].material);
  TEST_ASSERT_TRUE(s.ace.slot[0].present);
  TEST_ASSERT_EQUAL_HEX32(0x0000FF, s.ace.slot[0].rgb);
  TEST_ASSERT_EQUAL_STRING("PETG", s.ace.slot[1].material);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00, s.ace.slot[1].rgb);
}

void test_kobra_s1_real_info_idle() {
  KobraState s; kobraInit(s);
  kb(KB_S1_INFO_PRINTING, s);                 // leftover job must be cleared by the idle report
  TEST_ASSERT_TRUE(kb(KB_S1_INFO_IDLE, s));
  TEST_ASSERT_TRUE(s.haveTemps);
  TEST_ASSERT_EQUAL_FLOAT(25, s.nozzle);
  TEST_ASSERT_EQUAL_FLOAT(23, s.bed);
  TEST_ASSERT_EQUAL_FLOAT(0, s.nozzleTarget);
  TEST_ASSERT_EQUAL_STRING("standby", s.state);
  TEST_ASSERT_EQUAL_STRING("", s.file);       // last_project is history, not the current job
  TEST_ASSERT_EQUAL(-1, s.remainMin);
  TEST_ASSERT_EQUAL(0, s.fan);
}

void test_kobra_s1_info_printing() {
  KobraState s; kobraInit(s);
  TEST_ASSERT_TRUE(kb(KB_S1_INFO_PRINTING, s));
  TEST_ASSERT_EQUAL_STRING("printing", s.state);
  TEST_ASSERT_EQUAL_STRING("cube.gcode.3mf", s.file);
  TEST_ASSERT_EQUAL_FLOAT(40, s.progress);
  TEST_ASSERT_EQUAL(41, s.remainMin);
  TEST_ASSERT_EQUAL(27, s.elapsedMin);
  TEST_ASSERT_EQUAL_FLOAT(219, s.nozzle);
  TEST_ASSERT_EQUAL_FLOAT(220, s.nozzleTarget);
  TEST_ASSERT_EQUAL(100, s.fan);
}

void test_kobra_s1_real_ace() {
  KobraState s; kobraInit(s);
  TEST_ASSERT_TRUE(kb(KB_S1_ACE, s));
  TEST_ASSERT_TRUE(s.ace.present);
  TEST_ASSERT_EQUAL_FLOAT(30, s.ace.temp);
  for (int i = 0; i < 4; i++) TEST_ASSERT_TRUE(s.ace.slot[i].present);
  TEST_ASSERT_EQUAL_STRING("PLA", s.ace.slot[1].material);
  TEST_ASSERT_EQUAL_HEX32(0xFF6A14, s.ace.slot[1].rgb);
  TEST_ASSERT_EQUAL_HEX32(0xEE00EE, s.ace.slot[0].rgb);
  TEST_ASSERT_EQUAL(0, s.ace.drying);
}

void test_k2_real_57() {
  FilUnit u[MAX_FIL_UNITS];
  TEST_ASSERT_EQUAL(2, parse(K2_REAL_57, u));
  TEST_ASSERT_EQUAL_STRING("T1", u[0].key);
  TEST_ASSERT_EQUAL_STRING("T3", u[1].key);
  TEST_ASSERT_TRUE(u[0].hasEnv);
  TEST_ASSERT_EQUAL_FLOAT(24, u[0].temp);        // "24" (string)
  TEST_ASSERT_EQUAL_FLOAT(42, u[0].humidity);
  TEST_ASSERT_FALSE(u[0].slot[0].present);
  TEST_ASSERT_TRUE(u[0].slot[3].present);
  TEST_ASSERT_EQUAL_STRING("PLA", u[0].slot[3].material);
  TEST_ASSERT_EQUAL_HEX32(0x9CFF4F, u[0].slot[3].rgb);
  TEST_ASSERT_EQUAL(-1, u[0].slot[3].remainPct);  // "0" = not measured
  TEST_ASSERT_EQUAL_STRING("PETG", u[1].slot[0].material);
  TEST_ASSERT_EQUAL_HEX32(0x8A43FF, u[1].slot[0].rgb);
  TEST_ASSERT_EQUAL_STRING("CR-PLA", u[1].slot[2].material);
}

void test_k2_real_241() {
  FilUnit u[MAX_FIL_UNITS];
  TEST_ASSERT_EQUAL(2, parse(K2_REAL_241, u));
  TEST_ASSERT_EQUAL_STRING("T2", u[1].key);
  TEST_ASSERT_EQUAL_FLOAT(27, u[1].temp);
  for (int i = 0; i < 4; i++) TEST_ASSERT_FALSE(u[1].slot[i].present);
  TEST_ASSERT_EQUAL_STRING("PLA", u[0].slot[1].material);     // 000001 = Generic PLA (the printer names it PLA too)
  TEST_ASSERT_TRUE(u[0].slot[1].present);
}

// The printer names each loaded slot's material in box.same_material
// ([code, colour, [slots], name]); that name wins over the code table.
void test_k2_same_material_names() {
  const char *json = R"({"box":{"same_material":[["001001","06c84ff",["T1A"],"PLA"],["000001","000a3ff",["T1B"],"PLA"],["005001","0fc9da9",["T1C"],"PLA"],["001004","0ff1e1e",["T1D"],"PLA Matte"]],
    "T1":{"state":"connect","temperature":"24","dry_and_humidity":"30","remain_len":["0","0","0","0"],
      "color_value":["06c84ff","000a3ff","0fc9da9","0ff1e1e"],"material_type":["001001","000001","005001","001004"],"type":"CFS"}}})";
  FilUnit u[MAX_FIL_UNITS];
  TEST_ASSERT_EQUAL(1, parse(json, u));
  TEST_ASSERT_EQUAL_STRING("PLA", u[0].slot[1].material);
  TEST_ASSERT_EQUAL_STRING("PLA Matte", u[0].slot[3].material);
  TEST_ASSERT_EQUAL_STRING("PLA", u[0].slot[0].material);
}


// K2 slot pick: the print's T0 goes to the chosen tool and that tool's lines go to T0.
static void expectRemap(const char *in, int to, const char *want) {
  char out[64];
  bool changed = k2RemapToolLine(in, to, out, sizeof out);
  if (!want) { TEST_ASSERT_FALSE_MESSAGE(changed, in); return; }
  TEST_ASSERT_TRUE_MESSAGE(changed, in);
  TEST_ASSERT_EQUAL_STRING(want, out);
}

void test_k2_remap_tool_line() {
  expectRemap("T0", 5, "T5");
  expectRemap("T0\r", 5, "T5\r");
  expectRemap("T0 ; first colour", 5, "T5 ; first colour");
  expectRemap("T5", 5, "T0");
  expectRemap("T12", 12, "T0");
  expectRemap("T1", 5, nullptr);
  expectRemap("T0", 0, nullptr);
  expectRemap("M104 T0 S200", 5, nullptr);
  expectRemap("TIMELAPSE_TAKE_FRAME", 5, nullptr);
  expectRemap("T", 5, nullptr);
  expectRemap("T0X", 5, nullptr);
  expectRemap("", 5, nullptr);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_sanitize_type);
  RUN_TEST(test_label);
  RUN_TEST(test_decode_material);
  RUN_TEST(test_rgb565);
  RUN_TEST(test_box_dual);
  RUN_TEST(test_box_single);
  RUN_TEST(test_box_none);
  RUN_TEST(test_box_t3_only);
  RUN_TEST(test_box_short_arrays);
  RUN_TEST(test_color);
  RUN_TEST(test_slot_same);
  RUN_TEST(test_kobra_sanitize);
  RUN_TEST(test_kobra_init);
  RUN_TEST(test_kobra_temps_x);
  RUN_TEST(test_kobra_temps_s1);
  RUN_TEST(test_kobra_print_flow);
  RUN_TEST(test_kobra_status_free);
  RUN_TEST(test_kobra_updated_and_fan);
  RUN_TEST(test_kobra_ace_info);
  RUN_TEST(test_kobra_ace_partial_dry);
  RUN_TEST(test_kobra_lastwill_and_garbage);
  RUN_TEST(test_kobra_free_while_paused_keeps_job);
  RUN_TEST(test_kobra_partial_slot_keeps_material);
  RUN_TEST(test_kobra_s1_real_info_idle);
  RUN_TEST(test_kobra_s1_info_printing);
  RUN_TEST(test_kobra_s1_real_ace);
  RUN_TEST(test_k2_real_57);
  RUN_TEST(test_k2_real_241);
  RUN_TEST(test_k2_remap_tool_line);
  RUN_TEST(test_k2_same_material_names);
  RUN_TEST(test_creality_material_codes);
  RUN_TEST(test_filament_load_temp);
  RUN_TEST(test_kobra_file_scan);
  RUN_TEST(test_kobra_file_scan_limits);
  return UNITY_END();
}
