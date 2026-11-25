// Kobra MQTT message fixtures (payload JSON only; the topic is not parsed).
//
// PROVISIONAL (2026-11-24): the Kobras were unreachable from the dev PC. Shapes
// follow the open-source hass-anycubic_cloud integration (which parses real
// Anycubic traffic: type/action/state/data, "tempature" sic, "stoped" sic,
// multi_color_box[].slots[] with color [r,g,b]) plus KobraCommandCenter's Kobra
// S1 names (curr_nozzle / curr_bed). Replace with captures from the screen's
// /kobra page once it connects.
#pragma once

static const char KB_TEMP_X[] = R"JSON({"type":"tempature","action":"auto","state":"done",
  "data":{"curr_nozzle_temp":220.5,"target_nozzle_temp":220,"curr_hotbed_temp":64.8,"target_hotbed_temp":65}})JSON";

static const char KB_TEMP_S1[] = R"JSON({"type":"tempature","action":"auto","state":"done",
  "data":{"curr_nozzle":215.4,"target_nozzle":215,"curr_bed":60.1,"target_bed":60,"curr_box_temp":38.5}})JSON";

static const char KB_PRINTING[] = R"JSON({"type":"print","action":"start","state":"printing",
  "data":{"taskid":"123","filename":"benchy.gcode","progress":42,"remain_time":35,"print_time":25,
          "curr_layer":40,"total_layers":120}})JSON";

static const char KB_PAUSED[] = R"JSON({"type":"print","action":"pause","state":"paused","data":{"taskid":"123","progress":43}})JSON";
static const char KB_FINISHED[] = R"JSON({"type":"print","action":"start","state":"finished","data":{"taskid":"123","progress":100,"remain_time":0}})JSON";
static const char KB_STOPPED[] = R"JSON({"type":"print","action":"stop","state":"stoped","data":{"taskid":"123"}})JSON";
static const char KB_FAILED[] = R"JSON({"type":"print","action":"start","state":"failed","msg":"nozzle clog"})JSON";
static const char KB_FREE[] = R"JSON({"type":"status","action":"workReport","state":"free"})JSON";

static const char KB_UPDATED[] = R"JSON({"type":"print","action":"update","state":"updated",
  "data":{"curr_nozzle_temp":200,"curr_hotbed_temp":55,
          "settings":{"fan_speed_pct":80,"print_speed_pct":150,"target_nozzle_temp":205,"target_hotbed_temp":60}}})JSON";

static const char KB_FAN[] = R"JSON({"type":"fan","action":"auto","state":"done","data":{"fan_speed_pct":55}})JSON";

static const char KB_ACE_INFO[] = R"JSON({"type":"multiColorBox","action":"getInfo","state":"success",
  "data":{"multi_color_box":[{"id":0,"status":1,"temp":31,"humidity":28,"loaded_slot":1,
    "drying_status":{"status":0,"target_temp":0,"duration":0,"remain_time":0},
    "slots":[
      {"index":0,"type":"PLA","color":[255,0,0],"status":5},
      {"index":1,"type":"PETG","color":[0,255,0],"status":5},
      {"index":2,"type":"","color":[0,0,0],"status":0},
      {"index":3,"type":"PLA+","color":[0,0,255],"status":5}]}]}})JSON";

static const char KB_ACE_DRY[] = R"JSON({"type":"multiColorBox","action":"autoUpdateDryStatus","state":"success",
  "data":{"multi_color_box":[{"id":0,"temp":45,"drying_status":{"status":1,"target_temp":50,"duration":240,"remain_time":180}}]}})JSON";

static const char KB_LASTWILL_OFF[] = R"JSON({"type":"lastWill","action":"onlineReport","state":"offline"})JSON";

// ---- REAL captures, Kobra S1 fw 2.7.2.7 (2026-11-24, idle). Topic .../printer/public/20025/<dev>/<type>/report ----
static const char KB_S1_INFO_IDLE[] = R"JSON({"type":"info","action":"report","timestamp":1795549120302,"msgid":"b954899f","state":"done","code":200,"msg":"done","data":{"printerName":"Anycubic Kobra S1","urls":{"fileUploadurl":"x","rtspUrl":"x"},"project":null,"last_project":{"remain_time":0,"curr_layer":294,"total_layers":294,"supplies_usage":13432,"print_time":68,"progress":100,"state":"finished","print_status":2,"filename":"1121-2153-Main parts(01)_PLA_0.2_1h7m17s.gcode.3mf","pause":0,"project_type":2,"task_id":-1,"localtask":"","task_settings":{"camera_timelapse":0},"print_speed_mode":null},"model":"Anycubic Kobra S1","ip":"192.168.87.77","version":"2.7.2.7","state":"free","temp":{"curr_hotbed_temp":23,"curr_nozzle_temp":25,"target_hotbed_temp":0,"target_nozzle_temp":0},"print_speed_mode":2,"fan_speed_pct":0,"aux_fan_speed_pct":0,"box_fan_level":0}})JSON";

static const char KB_S1_ACE[] = R"JSON({"type":"multiColorBox","action":"getInfo","timestamp":1795549120494,"msgid":"9f63d9fb","state":"success","code":200,"msg":"done","data":{"head_tools_model":0,"multi_color_box":[{"id":0,"status":1,"model_id":40001,"auto_feed":1,"loaded_slot":-1,"feed_status":{"code":200,"type":3,"current_status":10,"slot_index":1},"temp":30,"humidity":0,"drying_status":{"status":0,"target_temp":0,"duration":0,"remain_time":0},"slots":[{"index":0,"sku":"","type":"PLA","color":[238,0,238],"edit_status":2,"status":4,"color_group":[[238,0,238,255]],"icon_type":0,"consumables_percent":0},{"index":1,"sku":"","type":"PLA","color":[255,106,20],"edit_status":1,"status":5,"color_group":[[255,106,20,255]],"icon_type":0,"consumables_percent":0},{"index":2,"sku":"","type":"PLA","color":[255,106,20],"edit_status":2,"status":4,"color_group":[[255,106,20,255]],"icon_type":0,"consumables_percent":0},{"index":3,"sku":"","type":"PLA","color":[238,0,238],"edit_status":2,"status":4,"color_group":[[238,0,238,255]],"icon_type":0,"consumables_percent":0}]}]}})JSON";

// Same info report while printing: "project" carries the job in the shape of "last_project"
// (constructed; not yet captured live).
static const char KB_S1_INFO_PRINTING[] = R"JSON({"type":"info","action":"report","state":"done","code":200,"data":{"project":{"remain_time":41,"curr_layer":120,"total_layers":294,"print_time":27,"progress":40,"state":"printing","filename":"cube.gcode.3mf","pause":0},"state":"busy","temp":{"curr_hotbed_temp":60,"curr_nozzle_temp":219,"target_hotbed_temp":60,"target_nozzle_temp":220},"print_speed_mode":2,"fan_speed_pct":100}})JSON";
