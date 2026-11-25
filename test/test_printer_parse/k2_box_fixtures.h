// K2 CFS ("box") fixtures: Moonraker result.status objects.
//
// PROVISIONAL (2026-11-23): the printers were unreachable from the dev PC, so
// these are built from what the Creality app (Creality/MainWindow.xaml.cs) reads:
//   - units keyed T1, then the second CFS on T3 (Nate's K2) or T2
//   - state "None" = no unit on that key
//   - material_type[4]: Creality material codes ("001001" = PLA, "-1" = empty)
//   - remain_len[4]: percent
//   - temperature, dry_and_humidity: numbers
//   - color_value[4]: ASSUMED "#RRGGBB"-style strings; confirm with a real capture:
//       curl "http://<K2>:7125/printer/objects/query?box"
//     and replace these with the real output.
#pragma once

static const char K2_BOX_DUAL[] = R"JSON({
  "box": {
    "T1": { "state": "connect", "temperature": 27, "dry_and_humidity": 41,
            "material_type": ["001001", "006001", "-1", "011001"],
            "color_value":   ["#FF0000", "#00FF00", "", "#0000FF"],
            "remain_len":    [80, 35, 0, 100] },
    "T2": { "state": "None" },
    "T3": { "state": "connect", "temperature": 26, "dry_and_humidity": 38,
            "material_type": ["002001", "001002", "013001", "-1"],
            "color_value":   ["#FFFFFF", "#000000", "#808080", ""],
            "remain_len":    [60, 10, 95, 0] }
  },
  "extruder": { "temperature": 24.5, "target": 0 }
})JSON";

static const char K2_BOX_SINGLE[] = R"JSON({
  "box": {
    "T1": { "state": "connect", "temperature": 27, "dry_and_humidity": 41,
            "material_type": ["001001", "006001", "-1", "011001"],
            "color_value":   ["#FF0000", "#00FF00", "", "#0000FF"],
            "remain_len":    [80, 35, 0, 100] },
    "T2": { "state": "None" },
    "T3": { "state": "None" }
  }
})JSON";

static const char K2_BOX_NONE[] = R"JSON({
  "extruder": { "temperature": 24.5, "target": 0 },
  "heater_bed": { "temperature": 23.0, "target": 0 }
})JSON";

// ---- REAL captures (2026-11-24): K2 at .57 (CFS on T1 + T3) and K2 Plus at .241 (T1 + T2). ----
// Numbers arrive as strings; remain_len is "0" when not measured; colors are "0RRGGBB".
static const char K2_REAL_57[] = R"JSON({"box": {"state": "connect", "T1": {"state": "connect", "temperature": "24", "dry_and_humidity": "42", "material_type": ["-1", "-1", "-1", "005001"], "color_value": ["-1", "-1", "-1", "09cff4f"], "remain_len": ["0", "0", "0", "0"], "type": "CFS"}, "T2": {"state": "None", "temperature": "None", "dry_and_humidity": "None", "material_type": ["-1", "-1", "-1", "-1"], "color_value": ["-1", "-1", "-1", "-1"], "remain_len": ["-1", "-1", "-1", "-1"], "type": "None"}, "T3": {"state": "connect", "temperature": "25", "dry_and_humidity": "37", "material_type": ["006002", "001001", "001004", "006002"], "color_value": ["08a43ff", "0ba552a", "07a92ac", "0ff1e1e"], "remain_len": ["0", "0", "0", "0"], "type": "CFS"}, "T4": {"state": "None", "temperature": "None", "dry_and_humidity": "None", "material_type": ["-1", "-1", "-1", "-1"], "color_value": ["-1", "-1", "-1", "-1"], "remain_len": ["-1", "-1", "-1", "-1"], "type": "None"}}})JSON";
static const char K2_REAL_241[] = R"JSON({"box": {"state": "connect", "T1": {"state": "connect", "temperature": "25", "dry_and_humidity": "31", "material_type": ["001001", "000001", "005001", "001004"], "color_value": ["06c84ff", "000a3ff", "0fc9da9", "0ff1e1e"], "remain_len": ["0", "0", "0", "0"], "type": "CFS"}, "T2": {"state": "connect", "temperature": "27", "dry_and_humidity": "31", "material_type": ["-1", "-1", "-1", "-1"], "color_value": ["-1", "-1", "-1", "-1"], "remain_len": ["0", "0", "0", "0"], "type": "CFS"}, "T3": {"state": "None", "temperature": "None", "dry_and_humidity": "None", "material_type": ["-1", "-1", "-1", "-1"], "color_value": ["-1", "-1", "-1", "-1"], "remain_len": ["-1", "-1", "-1", "-1"], "type": "None"}, "T4": {"state": "None", "temperature": "None", "dry_and_humidity": "None", "material_type": ["-1", "-1", "-1", "-1"], "color_value": ["-1", "-1", "-1", "-1"], "remain_len": ["-1", "-1", "-1", "-1"], "type": "None"}}})JSON";
