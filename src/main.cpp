#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Preferences.h>
#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <SD.h>

#include <FS.h>
#include <esp_adc_cal.h>
#include "esp_idf_version.h"
#include <Audio.h>

#define FW_VERSION "v2.0.6"

// Build with -DPERF_DEBUG to log how long the slow operations take.
#ifdef PERF_DEBUG
  #define PERF_T0(n)        unsigned long _perf_##n = millis();
  #define PERF_LOG(n, lbl)  Serial.printf("PERF %-14s %4lu ms\n", lbl, millis() - _perf_##n);
#else
  #define PERF_T0(n)
  #define PERF_LOG(n, lbl)
#endif

int currentVolume = 50; 
bool enableAudio = false; 
int tcMode = 0;

// ==========================================
// --- BOARD SELECTION & GRAPHICS ---
// Handled dynamically by platformio.ini!
// ==========================================

#ifdef BOARD_WAVESHARE_43

  #ifndef CONFIG_IDF_TARGET_ESP32S3
    #define CONFIG_IDF_TARGET_ESP32S3 1 
  #endif
  
  #define LGFX_USE_V1
  #include <LovyanGFX.hpp>
  #include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
  #include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
  #include <lgfx/v1/platforms/esp32/Light_CH422G.hpp>
  #include <Wire.h>

  #define SCREEN_W 800
  #define SCREEN_H 480

  #define SD_CS   15          
  #define SD_SCK  12
  #define SD_MISO 13
  #define SD_MOSI 11
  SPIClass sdSPI(FSPI);

  class LGFX : public lgfx::LGFX_Device {
  public:
    lgfx::Bus_RGB        _bus_instance;
    lgfx::Panel_RGB      _panel_instance;
    lgfx::Light_CH422G   _light_instance;
    lgfx::Touch_GT911    _touch_instance;

    LGFX(void) {
      {
        auto cfg = _panel_instance.config();
        cfg.memory_width  = 800;
        cfg.memory_height = 480;
        cfg.panel_width   = 800;
        cfg.panel_height  = 480;
        cfg.offset_x      = 0;
        cfg.offset_y      = 0;
        _panel_instance.config(cfg);
      }
      {
        auto cfg = _panel_instance.config_detail();
        cfg.use_psram = 1;
        _panel_instance.config_detail(cfg);
      }
      {
        auto cfg = _bus_instance.config();
        cfg.panel = &_panel_instance;
        cfg.pin_d0  = 14; cfg.pin_d1  = 38; cfg.pin_d2  = 18; cfg.pin_d3  = 17; cfg.pin_d4  = 10;
        cfg.pin_d5  = 39; cfg.pin_d6  = 0;  cfg.pin_d7  = 45; cfg.pin_d8  = 48; cfg.pin_d9  = 47; cfg.pin_d10 = 21;
        cfg.pin_d11 = 1;  cfg.pin_d12 = 2;  cfg.pin_d13 = 42; cfg.pin_d14 = 41; cfg.pin_d15 = 40;
        cfg.pin_henable = 5; cfg.pin_vsync   = 3; cfg.pin_hsync   = 46; cfg.pin_pclk    = 7;
        cfg.freq_write  = 16000000;
        cfg.hsync_polarity    = 0; cfg.hsync_front_porch = 8; cfg.hsync_pulse_width = 4; cfg.hsync_back_porch  = 8;
        cfg.vsync_polarity    = 0; cfg.vsync_front_porch = 8; cfg.vsync_pulse_width = 4; cfg.vsync_back_porch  = 8;
        cfg.pclk_idle_high    = 1; cfg.de_idle_high      = 0;
        _bus_instance.config(cfg);
      }
      _panel_instance.setBus(&_bus_instance);
      {
        auto cfg = _light_instance.config();
        cfg.pin_sda     = 8; cfg.pin_scl     = 9; cfg.i2c_port    = 0; cfg.freq        = 400000;
        cfg.pin_bl      = 2; cfg.shadow_init = (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4); cfg.invert      = false;
        _light_instance.config(cfg); _panel_instance.setLight(&_light_instance);
      }
      {
        auto cfg = _touch_instance.config();
        cfg.x_min           = 0; cfg.x_max           = 799; cfg.y_min           = 0; cfg.y_max           = 479;
        cfg.pin_int         = 4; cfg.bus_shared      = false; cfg.offset_rotation = 0;
        cfg.i2c_port        = 0; cfg.i2c_addr        = 0x5D; cfg.pin_sda         = 8; cfg.pin_scl         = 9;
        cfg.freq            = 400000;
        _touch_instance.config(cfg); _panel_instance.setTouch(&_touch_instance);
      }
      setPanel(&_panel_instance);
    }
  };

  LGFX tft;
  #define TEXT_SCALE 2.2f 

#elif defined(BOARD_SUNTON_70)

  #ifndef CONFIG_IDF_TARGET_ESP32S3
    #define CONFIG_IDF_TARGET_ESP32S3 1 
  #endif
  
  #define LGFX_USE_V1
  #include <LovyanGFX.hpp>
  #include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
  #include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
  #include <Wire.h>

  #define SCREEN_W 800
  #define SCREEN_H 480

  #define SD_CS   10
  #define SD_SCK  12
  #define SD_MISO 13
  #define SD_MOSI 11
  SPIClass sdSPI(FSPI);

  class LGFX : public lgfx::LGFX_Device {
  public:
    lgfx::Bus_RGB      _bus_instance;
    lgfx::Panel_RGB    _panel_instance;
    lgfx::Light_PWM    _light_instance;
    lgfx::Touch_GT911  _touch_instance;

    LGFX(void) {
      {
        auto cfg = _panel_instance.config();
        cfg.memory_width  = 800;
        cfg.memory_height = 480;
        cfg.panel_width   = 800;
        cfg.panel_height  = 480;
        cfg.offset_x      = 0;
        cfg.offset_y      = 0;
        
        cfg.rgb_order     = false;
        cfg.invert        = true; 
        
        _panel_instance.config(cfg);
      }
      {
        auto cfg = _panel_instance.config_detail();
        cfg.use_psram = 1;
        _panel_instance.config_detail(cfg);
      }
      {
        auto cfg = _bus_instance.config();
        cfg.panel = &_panel_instance;
        
        cfg.pin_d0  = 15; cfg.pin_d1  = 7;  cfg.pin_d2  = 6;  cfg.pin_d3  = 5;  cfg.pin_d4  = 4;
        cfg.pin_d5  = 9;  cfg.pin_d6  = 46; cfg.pin_d7  = 3;  cfg.pin_d8  = 8;  cfg.pin_d9  = 16; cfg.pin_d10 = 1;
        cfg.pin_d11 = 14; cfg.pin_d12 = 21; cfg.pin_d13 = 47; cfg.pin_d14 = 48; cfg.pin_d15 = 45;
        
        cfg.pin_henable = 41;
        cfg.pin_vsync   = 40;
        cfg.pin_hsync   = 39;
        cfg.pin_pclk    = 42;
        
        // --- OFFICIAL 7.0" EK9716 TIMINGS (Fixes the Cut-Off) ---
        cfg.freq_write  = 12000000;
        
        cfg.hsync_polarity    = 0; 
        cfg.hsync_front_porch = 40; 
        cfg.hsync_pulse_width = 48; 
        cfg.hsync_back_porch  = 40;
        
        cfg.vsync_polarity    = 0; 
        cfg.vsync_front_porch = 13;  
        cfg.vsync_pulse_width = 3; 
        cfg.vsync_back_porch  = 31;
        
        cfg.pclk_idle_high    = 1;
        cfg.de_idle_high      = 0;
        cfg.pclk_active_neg   = 0;
        
        _bus_instance.config(cfg);
      }
      _panel_instance.setBus(&_bus_instance);
      
      {
        auto cfg = _light_instance.config();
        cfg.pin_bl      = 2;
        cfg.invert      = false;
        cfg.freq        = 44100;
        cfg.pwm_channel = 7;
        _light_instance.config(cfg);
        _panel_instance.setLight(&_light_instance);
      }
      {
        auto cfg = _touch_instance.config();
        cfg.x_min           = 0;
        cfg.x_max           = 799;
        cfg.y_min           = 0;
        cfg.y_max           = 479;
        cfg.pin_int         = -1;
        cfg.bus_shared      = false;
        cfg.offset_rotation = 0;
        cfg.i2c_port        = 0;
        cfg.i2c_addr        = 0x5D; 
        cfg.pin_sda         = 19;
        cfg.pin_scl         = 20;
        cfg.freq            = 400000;
        _touch_instance.config(cfg);
        _panel_instance.setTouch(&_touch_instance);
      }
      setPanel(&_panel_instance);
    }
  };

  LGFX tft;
  #define TEXT_SCALE 2.2f 

#else
  #include <SD.h> 
  #include <TFT_eSPI.h> 
  TFT_eSPI tft = TFT_eSPI();
  #define TEXT_SCALE 1.0f 

  #if defined(BOARD_CYD_28) || defined(BOARD_28_DUAL) || defined(BOARD_CYD_28_DUAL)
    #define TFT_BL 21
    #define SD_CS 5 
    #define RTP_DOUT 39
    #define RTP_DIN  32
    #define RTP_SCK  25
    #define RTP_CS   33
    #define SCREEN_W 320
    #define SCREEN_H 240

    // --- XPT2046 resistive touch driver (bit-banged) for the 2.8" CYD ---
    // Replaces the TFT_Touch library, which (a) had no pressure detection,
    // (b) clocked the chip with no delays so reads were noisy and many real
    // presses were thrown away by its stability check, and (c) mapped the
    // calibrated range onto 30..(res-30) px, pulling every tap up to ~25 px
    // towards the centre (half the sidebar was unhittable).
    #define TOUCH_Z_THRESHOLD 150   // min Z1 reading to count as a press (idle reads ~0, a finger 400-1300)
    #define CAL_MARGIN        20    // calibration crosshairs sit this far in from each edge

    struct CydTouch {
      // Raw readings at the crosshair positions. Direction is preserved
      // (no min/max sorting), so mirrored panels calibrate correctly.
      int32_t x0 = 0, x1 = 4095, y0 = 0, y1 = 4095;
      bool swapXY = false;
      // Two wirings exist for "2.8in ESP32" boards:
      //  - classic CYD (ESP32-2432S028R): XPT2046 on its own pins
      //    CLK 25 / DIN 32 / DOUT 39 / CS 33 (bit-banged)
      //  - ESP32-32E style (e.g. Hosyond): XPT2046 shares the display's HSPI
      //    bus (CLK 14 / MOSI 13 / MISO 12) with its own CS 33
      // begin() probes both and uses whichever has a real chip answering.
      bool shared = false;
      bool detected = false;

      // select()/deselect() only claim the shared SPI bus. Chip-select is
      // toggled around EVERY conversion inside xfer(): the XPT2046 on these
      // boards only answers the first command after CS goes low.
      void select() {
        if (shared) tft.getSPIinstance().beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
      }
      void deselect() {
        if (shared) tft.getSPIinstance().endTransaction();
      }

      // A real XPT2046 gives z1~0 / z2~4095 when idle, or sane mid-range values
      // when pressed. Missing chips read as constant junk (all 0, all 4095, ...).
      bool probe() {
        int ok = 0;
        for (int i = 0; i < 8; i++) {
          select();
          int t = readTemp();
          xfer(0x90);
          deselect();
          if (t > 200 && t < 3900) ok++;
          delay(5);
        }
        return ok >= 6;
      }

      void begin() {
        pinMode(RTP_CS, OUTPUT); digitalWrite(RTP_CS, HIGH);
        shared = true;                       // try the shared display bus first
        if (probe()) { detected = true; Serial.println("Touch: XPT2046 on shared display SPI bus (ESP32-32E wiring)"); return; }
        shared = false;
        pinMode(RTP_SCK, OUTPUT);  digitalWrite(RTP_SCK, LOW);
        pinMode(RTP_DIN, OUTPUT);  digitalWrite(RTP_DIN, LOW);
        pinMode(RTP_DOUT, INPUT);
        if (probe()) { detected = true; Serial.println("Touch: XPT2046 on dedicated pins (classic CYD wiring)"); return; }
        // Nothing passed the probe: fall back to the classic wiring (what the
        // firmware always assumed) rather than disabling touch entirely.
        detected = true;
        Serial.println("Touch: probe inconclusive, using classic CYD wiring");
      }

      uint16_t xfer(uint8_t cmd) {
        if (shared) {
          SPIClass &bus = tft.getSPIinstance();
          digitalWrite(RTP_CS, LOW);
          bus.transfer(cmd);
          uint16_t v = (bus.transfer16(0) >> 3) & 0x0FFF;
          digitalWrite(RTP_CS, HIGH);
          return v;
        }
        digitalWrite(RTP_CS, LOW);
        for (int i = 7; i >= 0; i--) {
          digitalWrite(RTP_DIN, (cmd >> i) & 1);
          digitalWrite(RTP_SCK, HIGH); delayMicroseconds(1);
          digitalWrite(RTP_SCK, LOW);  delayMicroseconds(1);
        }
        uint16_t r = 0;
        for (int i = 11; i >= 0; i--) {
          digitalWrite(RTP_SCK, HIGH); delayMicroseconds(1);
          digitalWrite(RTP_SCK, LOW);  delayMicroseconds(1);
          r |= (digitalRead(RTP_DOUT) << i);
        }
        // Clock out the 3 trailing zero bits + 1 spare so each conversion is the
        // full 24 clocks the XPT2046 expects. With only 20 clocks every command
        // after the first one in a CS cycle was misaligned and read garbage.
        for (int i = 0; i < 4; i++) {
          digitalWrite(RTP_SCK, HIGH); delayMicroseconds(1);
          digitalWrite(RTP_SCK, LOW);  delayMicroseconds(1);
        }
        digitalWrite(RTP_CS, HIGH);
        digitalWrite(RTP_DIN, LOW);
        return r;
      }

      // Z1 is ~0 with nothing touching and rises with finger pressure. (The
      // common "z1 + 4095 - z2" formula reads ~4095 at idle on some panels.)
      int pressure() {
        return xfer(0xB1);
      }

      // Internal temperature sensor: a live chip returns a mid-range value,
      // an empty bus returns 0 or 4095.
      int readTemp() {
        xfer(0x87);              // first read powers up the reference
        return xfer(0x87);
      }

      // Filtered raw readings (a = X channel, b = Y channel) while pressed.
      bool readRaw(int &a, int &b) {
        if (!detected) return false;
        select();
        if (pressure() < TOUCH_Z_THRESHOLD) {
          xfer(0x90); deselect();
          return false;
        }
        xfer(0xD1);                          // discard: lets the ADC settle
        const int N = 7;
        int xs[N], ys[N];
        for (int i = 0; i < N; i++) { xs[i] = xfer(0xD1); ys[i] = xfer(0x91); }
        bool stillPressed = pressure() >= TOUCH_Z_THRESHOLD;  // reject lift-off samples
        xfer(0x90);                          // power down, re-enable PENIRQ
        deselect();
        if (!stillPressed) return false;

        // Median-ish filter: sort and average the middle three samples.
        for (int i = 1; i < N; i++) {
          int vx = xs[i], vy = ys[i], j = i - 1, k = i - 1;
          while (j >= 0 && xs[j] > vx) { xs[j + 1] = xs[j]; j--; } xs[j + 1] = vx;
          while (k >= 0 && ys[k] > vy) { ys[k + 1] = ys[k]; k--; } ys[k + 1] = vy;
        }
        a = (xs[2] + xs[3] + xs[4]) / 3;
        b = (ys[2] + ys[3] + ys[4]) / 3;
        return (a > 0 && a < 4095 && b > 0 && b < 4095);
      }

      bool getXY(int &x, int &y) {
        int a, b;
        if (!readRaw(a, b)) return false;
        if (x1 == x0 || y1 == y0) return false;
        int32_t sx = swapXY ? b : a;
        int32_t sy = swapXY ? a : b;
        x = CAL_MARGIN + (sx - x0) * (SCREEN_W - 1 - 2 * CAL_MARGIN) / (x1 - x0);
        y = CAL_MARGIN + (sy - y0) * (SCREEN_H - 1 - 2 * CAL_MARGIN) / (y1 - y0);
        x = constrain(x, 0, SCREEN_W - 1);
        y = constrain(y, 0, SCREEN_H - 1);
        return true;
      }
    };
    CydTouch touch;
  #elif defined(BOARD_40_INCH)
    #include <Wire.h>
    #define TFT_BL 27
    #define SD_CS   5
    #define SD_SCK  18
    #define SD_MISO 19
    #define SD_MOSI 23
    #define SCREEN_W 480
    #define SCREEN_H 320
    SPIClass sdSPI(VSPI);
  #endif
#endif

#if defined(BOARD_40_INCH) || defined(BOARD_WAVESHARE_43) || defined(BOARD_SUNTON_70)
  #include <Wire.h>
  #include <Adafruit_BME280.h>
  Adafruit_BME280 bme;
  bool bmeReady = false;
  int bmeSDA = 32; 
  int bmeSCL = 25; 

  bool pingBME() {
    if (bme.begin(0x76, &Wire) || bme.begin(0x77, &Wire)) {
      bme.setSampling(Adafruit_BME280::MODE_NORMAL, Adafruit_BME280::SAMPLING_X2, Adafruit_BME280::SAMPLING_X16, Adafruit_BME280::SAMPLING_X1, Adafruit_BME280::FILTER_X16, Adafruit_BME280::STANDBY_MS_500);
      return true;
    }
    return false;
  }
#endif

#if defined(BOARD_40_INCH) || defined(BOARD_WAVESHARE_43) || defined(BOARD_SUNTON_70)
  #include <Adafruit_MAX31855.h>

  // The MAX31855 shares the SD card's hardware SPI bus (SCK + MISO) and only
  // needs its own chip-select. The old bit-banged pins collided with other
  // hardware: on the Sunton 7" GPIO 6/16 are RGB data lines and GPIO 20 is the
  // GT911 touch SCL; on the Waveshare GPIO 20 is USB D+; on the 4.0" CYD the
  // bit-banged CLK/SO were the SD card's own SCK/MISO.
  //   Wiring: MAX31855 SCK -> SD_SCK, SO -> SD_MISO, CS -> MAX_CS
  #if defined(BOARD_SUNTON_70)
    #define MAX_CS   17   // Free GPIO on the Sunton expansion header
  #elif defined(BOARD_WAVESHARE_43)
    #define MAX_CS   6    // Sensor/AD header pin on the Waveshare 4.3"
  #else
    #define MAX_CS   21   // 4.0" CYD: same wiring as before (SCK 18, SO 19)
  #endif

  Adafruit_MAX31855 thermocouple(MAX_CS, &sdSPI);
  bool max31855Ready = false;
#endif

#define PX(x) ((x) * SCREEN_W / 320)
#define PY(y) ((y) * SCREEN_H / 240)

Preferences preferences;
WebServer server(80);

char printerIP[32] = "192.168.1.100"; 
char printerName[32] = "Snapmaker U1"; 
char mdnsName[32] = "u1-display"; 
bool shouldSaveConfig = false;
bool sdCardReady = false;
uint64_t sdCardTotal = 0;
uint64_t sdCardUsed = 0;
bool invertDisplay = false; 
unsigned long blockSyncUntil = 0; 
bool showBattery = false; 
int envMode = 0; 
bool useFahrenheit = false; 
float tempOffset = 0.0; 
float humOffset = 0.0;  

unsigned long lastUpdate = 0;
const int updateInterval = 2000;
const int offlineRetryInterval = 10000;  // poll less often while the printer is unreachable
bool printerOffline = false;
String currentState = "Offline";
String currentFile = "Idle";
float bedTemp = 0.0, bedTarget = 0.0;
float toolTemp[4] = {0,0,0,0};
float toolTarget[4] = {0,0,0,0};
int printSpeed = 100, fanSpeed = 0;
float printProgress = 0.0;
int printDuration = 0; 
int slicerEstimate = 0;          // seconds, from the gcode file's slicer metadata (0 = unknown)
String estimateForFile = "";     // file the cached slicer estimate belongs to
int activeTool = -1;
float moveDist = 10.0; 

String filType[4] = {"PLA", "PLA", "PLA-CF", "WOOD"}; 
uint16_t filColor[4] = {0x2104, 0x07FF, 0x3186, 0xA285}; 
bool toolAttached[4] = {true, false, false, false}; 
bool toolLoaded[4] = {true, true, true, true};     

const String FIL_TYPES[] = {"PLA", "PETG", "ABS", "TPU", "ASA", "PC", "NYLON", "PLA-CF", "PETG-CF", "WOOD"};
const uint16_t FIL_COLORS[] = {TFT_WHITE, TFT_RED, TFT_BLUE, TFT_GREEN, TFT_YELLOW, TFT_ORANGE, TFT_CYAN, TFT_MAGENTA, 0x2104, 0xA285};

unsigned long lastTouchTime = 0; 
int currentTab = 0;

String fileList[20];
int fileCount = 0;
String klipperFileList[20];
int klipperFileCount = 0;
int fileScrollIndex = 0;
String selectedFile = "";
int printTabSource = 0;

bool enableRadar = false;
int radarAuthMode = 0;
float homeLat = 41.9742; 
float homeLon = -87.9073; 
float radarRadiusDeg = 0.5; 
float scanAngle = 0.0;
float prevScanAngle = -1.0;
unsigned long lastSweepAnim = 0;

struct Airplane {
  String callsign; float lat; float lon; float track; float altitude;
};

Airplane planes[30]; 
int currentPlaneCount = 0;
unsigned long lastRadarApiCall = 0;
String radarStatus = "Waiting for data...";
String radarClientId = "";
String radarClientSecret = "";
String radarToken = "";
unsigned long radarTokenExpiry = 0;

int activeModal = 0; 
int kbMode = 0;
String kbInput = ""; 
bool optBedLevel = true;
bool optTimelapse = false;
bool optAdvMap = false;
int optT0 = 0;

String toastTitle = "";
String toastMsg = "";
bool toastError = false;
bool toastDrawn = false; 
unsigned long toastExpireTime = 0;
bool forceRedrawForToast = false; 

const char kbRows[4][11] = {
  "1234567890",
  "QWERTYUIOP",
  "ASDFGHJKL-",
  "ZXCVBNM_.<" 
};

const int presetBed[] = {0, 50, 60, 70, 80, 100};
const int presetTool[] = {0, 190, 210, 230, 250, 270};
bool isCalibrated = false;
File fsUploadFile;

uint16_t BG_COLOR, SIDEBAR_COLOR, CARD_COLOR, TEXT_GRAY, ACCENT_CYAN;
int currentTheme = 0;
String customBG = "#0F1115";
String customSidebar = "#161B22";
String customCard = "#1A1D24";
String customText = "#888888";
String customAccent = "#00E5FF";

#define BTN_BLUE tft.color565(30, 80, 150)
#define BTN_GREEN tft.color565(40, 120, 60)
#define BTN_RED tft.color565(150, 50, 50)        

void loadFilesFromSD();
void fetchKlipperFiles();
void syncFilamentToKlipper(int tIdx);
bool uploadFileToKlipper(String localPath, String remoteName, int yOffset);
void launchPatchedPrint(String filename, int source, bool bBed, bool bTime, bool bAdv, int t0_map);
void drawSidebar();
void drawHomeTab();
void drawToolheadTab();
void drawMoveTab();
void drawPrintTab();
void drawSettingsTab();
void drawTempModal(int type);
void drawSpeedFanModal();
void drawActiveToolModal();
void drawCancelModal();
void drawPauseModal();
void drawToolModal(int idx);
void drawFilamentTypeModal(int idx); 
void drawKeyboardModal(int idx);
void updateKeyboardInputBox();
void drawColorPickerModal(int idx);
void drawPrintOptionsModal();
void updateDynamicUI();
void sendGcode(String command);
void startPrinterPolling();
bool applyPrinterSnapshot();
void handleRoot();
void handleAudio();
void handleTech();
void handleSave();
void handleSaveFilament();
void handleControl();
void handleCalibrateWeb();
void handleFileUpload();
void touchCalibrate();
void showToast(String title, String msg, bool isError = false);
void drawToast();
void drawBootLogo();
void configModeCallback(WiFiManager *myWiFiManager);
String urlEncode(String str);
void applyTheme();
float getBatteryVoltage();
int getBatteryPercentage(float volts);

#if defined(BOARD_WAVESHARE_43) || defined(BOARD_SUNTON_70)
  Audio audio; 
#else
  Audio audio(true, I2S_DAC_CHANNEL_LEFT_EN); 
  #define AUDIO_EN_PIN 4
#endif

enum : uint8_t { CMD_SET_VOLUME, CMD_PLAY_FILE, CMD_STOP_AUDIO };
struct audioMessage {
  uint8_t cmd;
  char txt[128];
  uint32_t value;
};
QueueHandle_t audioQueue = NULL;

void audioTask(void *parameter) {
  audioQueue = xQueueCreate(10, sizeof(struct audioMessage));
  audioMessage msg;

#if !defined(BOARD_WAVESHARE_43) && !defined(BOARD_SUNTON_70)
  pinMode(AUDIO_EN_PIN, OUTPUT);
  digitalWrite(AUDIO_EN_PIN, LOW);
#endif

  int mappedVol = (currentVolume * 21) / 100;
  audio.setVolume(mappedVol);

  while(true) {
     if (xQueueReceive(audioQueue, &msg, 1) == pdPASS) {
        if (msg.cmd == CMD_SET_VOLUME) {
           int libVol = (msg.value * 21) / 100;
           audio.setVolume(libVol);
        } else if (msg.cmd == CMD_PLAY_FILE) {
           audio.stopSong();
           audio.connecttoFS(SD, msg.txt);
        } else if (msg.cmd == CMD_STOP_AUDIO) {
           audio.stopSong();
        }
     }
     audio.loop();
  }
}

#define PIN_BAT_ADC 34
float getBatteryVoltage() {
  esp_adc_cal_characteristics_t adc_chars;
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 1100, &adc_chars);
  uint32_t raw_sum = 0;
  for(int i = 0; i < 20; i++) {
    raw_sum += analogRead(PIN_BAT_ADC);
  }
  uint32_t raw = raw_sum / 20;
  uint32_t mv = esp_adc_cal_raw_to_voltage(raw, &adc_chars) * 2; 
  return mv / 1000.0f;
}

int getBatteryPercentage(float volts) {
  if (volts <= 3.3f) return 0;
  if (volts >= 4.15f) return 100;
  int pct = (int)((volts - 3.3f) / (4.15f - 3.3f) * 100.0f);
  return constrain(pct, 0, 100);
}

uint16_t hexToRGB565(String hex) {
  if (hex.startsWith("#")) hex.remove(0, 1);
  if (hex.length() >= 6) hex = hex.substring(0, 6);
  long rgb = strtol(hex.c_str(), NULL, 16);
  uint8_t r = (rgb >> 16) & 0xFF;
  uint8_t g = (rgb >> 8) & 0xFF;
  uint8_t b = rgb & 0xFF;
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

String rgb565ToHex(uint16_t color) {
  uint8_t r = ((color >> 11) & 0x1F) * 255 / 31;
  uint8_t g = ((color >> 5) & 0x3F) * 255 / 63;
  uint8_t b = (color & 0x1F) * 255 / 31;
  char hex[8];
  sprintf(hex, "#%02X%02X%02X", r, g, b);
  return String(hex);
}

void applyTheme() {
    if (currentTheme == 0) {
        BG_COLOR = tft.color565(15, 17, 21);
        SIDEBAR_COLOR = tft.color565(22, 27, 34);
        CARD_COLOR = tft.color565(26, 29, 36);
        TEXT_GRAY = tft.color565(136, 136, 136);
        ACCENT_CYAN = tft.color565(0, 229, 255);
    } else if (currentTheme == 1) {
        BG_COLOR = tft.color565(230, 235, 240);
        SIDEBAR_COLOR = tft.color565(200, 205, 215);
        CARD_COLOR = tft.color565(255, 255, 255);
        TEXT_GRAY = tft.color565(80, 80, 80);
        ACCENT_CYAN = tft.color565(0, 150, 255);
    } else if (currentTheme == 2) {
        BG_COLOR = tft.color565(11, 10, 16);
        SIDEBAR_COLOR = tft.color565(21, 10, 33);
        CARD_COLOR = tft.color565(34, 15, 50);
        TEXT_GRAY = tft.color565(255, 0, 153); 
        ACCENT_CYAN = tft.color565(0, 255, 255); 
    } else if (currentTheme == 3) {
        BG_COLOR = tft.color565(15, 15, 15);
        SIDEBAR_COLOR = tft.color565(25, 25, 25);
        CARD_COLOR = tft.color565(30, 30, 30);
        TEXT_GRAY = tft.color565(255, 176, 0); 
        ACCENT_CYAN = tft.color565(50, 255, 50); 
    } else if (currentTheme == 4) {
        BG_COLOR = hexToRGB565(customBG);
        SIDEBAR_COLOR = hexToRGB565(customSidebar);
        CARD_COLOR = hexToRGB565(customCard);
        TEXT_GRAY = hexToRGB565(customText);
        ACCENT_CYAN = hexToRGB565(customAccent);
    }
}

String urlEncode(String str) {
  String encoded = "";
  for (size_t i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else {
      char buf[4];
      sprintf(buf, "%%%02X", (unsigned char)c);
      encoded += buf;
    }
  }
  return encoded;
}

void saveConfigCallback() { shouldSaveConfig = true; }

void showToast(String title, String msg, bool isError) {
  toastTitle = title;
  toastMsg = msg;
  toastError = isError;
  toastExpireTime = millis() + 3000;
  toastDrawn = false; 
}

void drawToast() {
  if (millis() > toastExpireTime) return;
  tft.fillRoundRect(PX(50), PY(10), PX(260), PY(45), 6, CARD_COLOR);
  tft.drawRoundRect(PX(50), PY(10), PX(260), PY(45), 6, toastError ? BTN_RED : BTN_GREEN);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(toastTitle, PX(60), PY(22), 2);
  tft.setTextColor(TEXT_GRAY);
  
  String shortMsg = toastMsg;
  if(shortMsg.length() > 35) shortMsg = shortMsg.substring(0, 32) + "...";
  tft.drawString(shortMsg, PX(60), PY(38), 1);
  tft.setTextDatum(TL_DATUM);
}

void drawBootLogo() {
  tft.fillScreen(BG_COLOR);
  tft.drawRect(10, 10, SCREEN_W - 20, SCREEN_H - 20, ACCENT_CYAN);
  tft.drawRect(12, 12, SCREEN_W - 24, SCREEN_H - 24, tft.color565(50, 55, 65));
  tft.drawLine(10, 40, 30, 40, ACCENT_CYAN);
  tft.drawLine(30, 40, 40, 30, ACCENT_CYAN);
  tft.drawLine(40, 30, 100, 30, ACCENT_CYAN);
  tft.drawLine(SCREEN_W - 10, SCREEN_H - 40, SCREEN_W - 30, SCREEN_H - 40, ACCENT_CYAN);
  tft.drawLine(SCREEN_W - 30, SCREEN_H - 40, SCREEN_W - 40, SCREEN_H - 30, ACCENT_CYAN);
  tft.drawLine(SCREEN_W - 40, SCREEN_H - 30, SCREEN_W - 100, SCREEN_H - 30, ACCENT_CYAN);
  
  tft.setTextColor(ACCENT_CYAN);
  tft.setTextDatum(MC_DATUM);
  
#if defined(BOARD_WAVESHARE_43) || defined(BOARD_SUNTON_70)
  tft.drawString("ADVANCED", SCREEN_W/2, PY(90), 4); 
#else
  tft.drawString("CYD", SCREEN_W/2, PY(90), 4); 
#endif

  tft.setTextColor(TFT_WHITE);
  tft.drawString("SNAPMAKER U1 SCREEN", SCREEN_W/2, PY(130), 2);
  tft.setTextColor(ACCENT_CYAN);
  tft.drawString(String(FW_VERSION) + " - Made by Nates Print Shop", SCREEN_W/2, PY(155), 1);
  tft.setTextColor(TEXT_GRAY);
  tft.drawString("Initializing Core Systems...", SCREEN_W/2, PY(190), 1);
  tft.setTextDatum(TL_DATUM);
}

void configModeCallback(WiFiManager *myWiFiManager) {
  tft.fillScreen(BG_COLOR);
  tft.setTextColor(ACCENT_CYAN);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Wi-Fi Setup Required", SCREEN_W/2, PY(60), 4);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("Connect phone/PC to network:", SCREEN_W/2, PY(120), 2);
  tft.setTextColor(TFT_YELLOW);
  tft.drawString(myWiFiManager->getConfigPortalSSID(), SCREEN_W/2, PY(150), 2);
  tft.setTextColor(TEXT_GRAY);
  tft.drawString("Then go to http://192.168.4.1", SCREEN_W/2, PY(190), 1);
  tft.setTextDatum(TL_DATUM);
}

bool getTouchCoord(int &x, int &y) {
#if defined(BOARD_CYD_28) || defined(BOARD_28_DUAL) || defined(BOARD_CYD_28_DUAL)
  return touch.getXY(x, y);
#elif defined(BOARD_40_INCH) || defined(BOARD_WAVESHARE_43) || defined(BOARD_SUNTON_70)
  uint16_t ux = 0, uy = 0;
  if (tft.getTouch(&ux, &uy)) {
    x = ux;
    y = uy;
    return true;
  }
  return false;
#endif
}

void loadFilesFromSD() {
  fileCount = 0;
  if (!sdCardReady) return;
  File root = SD.open("/");
  if (!root) return;
  File file = root.openNextFile();
  while (file && fileCount < 20) {
    if (!file.isDirectory()) {
      String fname = String(file.name());
      if (fname.endsWith(".gcode") || fname.endsWith(".GCODE") || fname.endsWith(".3mf")) {
          if (fname.startsWith("/")) fname.remove(0, 1);
          fileList[fileCount++] = fname;
      }
    }
    file = root.openNextFile();
  }
}

void fetchKlipperFiles() {
  if (!sdCardReady) return;
  HTTPClient http;
  int httpCode = -1;
  if (!printerOffline) {
    http.setConnectTimeout(800);
    http.setTimeout(3000);
    http.begin("http://" + String(printerIP) + ":7125/server/files/list?root=gcodes");
    httpCode = http.GET();
  }
  if (httpCode == 200) {
    File tmpFile = SD.open("/k_files.tmp", FILE_WRITE);
    if (tmpFile) {
       http.writeToStream(&tmpFile);
       tmpFile.close();
    }
  }
  http.end();

  klipperFileCount = 0;
  File f = SD.open("/k_files.tmp", FILE_READ);
  if (f) {
    String line = "";
    while (f.available() && klipperFileCount < 20) {
        char c = f.read();
        line += c;
        if (c == '}' || line.length() > 512) {
            int p = line.indexOf("\"path\":");
            if (p != -1) {
                int q1 = line.indexOf("\"", p + 7);
                if (q1 != -1) {
                    int q2 = line.indexOf("\"", q1 + 1);
                    if (q2 != -1) {
                        String fn = line.substring(q1 + 1, q2);
                        if ((fn.endsWith(".gcode") || fn.endsWith(".3mf") || fn.endsWith(".GCODE")) && !fn.startsWith(".")) {
                            klipperFileList[klipperFileCount++] = fn;
                        }
                    }
                }
            }
            line = "";
        }
    }
    f.close();
  }
}

void syncFilamentToKlipper(int tIdx) {
  blockSyncUntil = millis() + 5000;
  if (printerOffline) return;
  HTTPClient http;
  
  http.setConnectTimeout(800);
  http.setTimeout(1500); 
  http.begin("http://" + String(printerIP) + ":7125/printer/filament_detect/set");
  http.addHeader("Content-Type", "application/json");

  uint32_t r = (filColor[tIdx] >> 11) & 0x1F; r = (r * 255) / 31;
  uint32_t g = (filColor[tIdx] >> 5) & 0x3F;  g = (g * 255) / 63;
  uint32_t b = filColor[tIdx] & 0x1F;         b = (b * 255) / 31;
  uint32_t colorDecimal = (r << 16) | (g << 8) | b;

  String payload = "{\"channel\":" + String(tIdx) + ",\"info\":{\"VENDOR\":\"Generic\",\"MAIN_TYPE\":\"" + filType[tIdx] + "\",\"RGB_1\":" + String(colorDecimal) + ",\"ALPHA\":255}}";
  http.POST(payload);
  http.end();
}

bool uploadFileToKlipper(String localPath, String remoteName, int yOffset) {
  File f = SD.open("/" + localPath, FILE_READ);
  if (!f) return false;
  
  WiFiClient client;
  if (!client.connect(printerIP, 7125)) {
      f.close();
      return false;
  }
  
  String boundary = "----ESP32BoundaryUpload";
  String head = "--" + boundary + "\r\n"
                "Content-Disposition: form-data; name=\"file\"; filename=\"" + remoteName + "\"\r\n"
                "Content-Type: application/octet-stream\r\n\r\n";
  String tail = "\r\n--" + boundary + "--\r\n";
  
  uint32_t fileLen = f.size();
  uint32_t totalLen = head.length() + fileLen + tail.length();
  
  client.println("POST /server/files/upload HTTP/1.1");
  client.println("Host: " + String(printerIP) + ":7125");
  client.println("Content-Type: multipart/form-data; boundary=" + boundary);
  client.println("Content-Length: " + String(totalLen));
  client.println();
  client.print(head);
  
  uint8_t *buf = (uint8_t*)malloc(4096); 
  if (!buf) { f.close(); client.stop(); return false; }

  uint32_t uploaded = 0;
  int lastP = -1;
  
  tft.drawRect(PX(30), PY(yOffset), PX(260), PY(20), tft.color565(50, 55, 65));

  while (f.available()) {
      size_t c = f.read(buf, 4096);
      client.write(buf, c);
      uploaded += c;
      
      if (fileLen > 0) {
          int p = (int)(((uint64_t)uploaded * 100) / fileLen);
          if (p > lastP && p <= 100) {
              tft.fillRect(PX(32), PY(yOffset + 2), (int)((p / 100.0) * PX(256)), PY(16), ACCENT_CYAN);
              tft.setTextColor(BG_COLOR);
              tft.setTextDatum(MC_DATUM);
              tft.drawString(String(p) + "% Complete", SCREEN_W/2, PY(yOffset + 10), 1);
              lastP = p;
          }
      }
      server.handleClient(); 
      yield(); 
  }
  
  free(buf);
  client.print(tail);
  f.close();

  // Moonraker may need a while to process a large upload before replying.
  int statusCode = 0;
  unsigned long deadline = millis() + 30000;
  while (client.connected() && millis() < deadline) {
      if (client.available()) {
          String statusLine = client.readStringUntil('\n');   // "HTTP/1.1 201 Created"
          int sp = statusLine.indexOf(' ');
          if (sp > 0) statusCode = statusLine.substring(sp + 1, sp + 4).toInt();
          break;
      }
      server.handleClient();
      delay(10);
  }
  client.stop();

  if (uploaded != fileLen) return false;
  return (statusCode >= 200 && statusCode < 300);
}

void launchPatchedPrint(String filename, int source, bool bBed, bool bTime, bool bAdv, int t0_map) {
  tft.fillScreen(BG_COLOR);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Preparing Job...", SCREEN_W/2, PY(40), 2);
  
  String targetFilename = filename;
  bool needsPatching = (bBed || bTime || bAdv || t0_map != 0);

  if (needsPatching) {
      tft.drawString("Patching File & Variables...", SCREEN_W/2, PY(70), 1);
      if (!SD.exists("/dump_zone")) SD.mkdir("/dump_zone");
      
      // Files can live in printer subfolders ("sub/part.gcode"). The patched
      // copy goes flat into /dump_zone and the printer's gcode root, so drop
      // the directory part (SD.open can't create missing folders).
      int slashIdx = targetFilename.lastIndexOf('/');
      if (slashIdx >= 0) targetFilename = targetFilename.substring(slashIdx + 1);

      int extIdx = targetFilename.lastIndexOf('.');
      if (extIdx > 0) targetFilename = targetFilename.substring(0, extIdx) + "_screen" + targetFilename.substring(extIdx);
      else targetFilename += "_screen.gcode";

      File outFile = SD.open("/dump_zone/" + targetFilename, FILE_WRITE);
      if (!outFile) { 
          showToast("Error", "SD Card Error!", true); 
          activeModal = 0; currentTab = 0; forceRedrawForToast = true; return; 
      }

      String injection = "";
      if (bBed) injection += " BED_LEVEL=1";
      if (bTime) injection += " TIMELAPSE=1";
      if (bAdv) injection += " ADV_TOOL_MAPPING=1 T0_MAP=" + String(t0_map);

      bool foundMacro = false;
      int linesRead = 0;
      uint32_t totalBytes = 0;
      uint32_t processedBytes = 0;
      int lastP = -1;
      bool sourceOk = false;

      tft.drawRect(PX(30), PY(90), PX(260), PY(20), tft.color565(50, 55, 65));

      if (source == 0) { 
          HTTPClient http;
          http.begin("http://" + String(printerIP) + ":7125/server/files/gcodes/" + urlEncode(filename));
          int httpCode = http.GET();
          if (httpCode == 200) {
              sourceOk = true;
              int reportedSize = http.getSize();   // -1 when chunked / unknown
              totalBytes = (reportedSize > 0) ? (uint32_t)reportedSize : 0;
              
              WiFiClient *stream = http.getStreamPtr();
              String line; line.reserve(256);
              uint8_t *fastBuf = (uint8_t*)malloc(4096);
              
              while(http.connected() && (totalBytes == 0 || processedBytes < totalBytes)) {
                  if (stream->available()) {
                      if (t0_map != 0 || (!foundMacro && linesRead < 500)) {
                          line = stream->readStringUntil('\n');
                          processedBytes += line.length() + 1;
                          
                          bool hasCr = line.endsWith("\r");
                          if (hasCr) line.remove(line.length() - 1);
                          
                          if (t0_map != 0) {
                              if (line.indexOf("T0") != -1) {
                                  line.replace("T0 ", "T" + String(t0_map) + " ");
                                  line.replace("T0\n", "T" + String(t0_map) + "\n");
                                  line.replace("T0\r", "T" + String(t0_map) + "\r");
                                  if(line.endsWith("T0")) line.replace("T0", "T" + String(t0_map));
                              }
                          }

                          if (!foundMacro) {
                              if (line.startsWith("START_PRINT") || line.startsWith("PRINT_START")) {
                                  outFile.print(line + injection + (hasCr ? "\r\n" : "\n"));
                                  foundMacro = true;
                              } else {
                                  outFile.print(line + (hasCr ? "\r\n" : "\n"));
                              }
                              linesRead++;
                          } else {
                              outFile.print(line + (hasCr ? "\r\n" : "\n"));
                          }
                      } else {
                          if (fastBuf) {
                              int c = stream->readBytes(fastBuf, 4096);
                              if(c > 0) {
                                  outFile.write(fastBuf, c);
                                  processedBytes += c;
                              }
                          }
                      }
                      
                      if (totalBytes > 0) {
                          int p = (int)(((uint64_t)processedBytes * 100) / totalBytes);
                          if (p > lastP && p <= 100) {
                              tft.fillRect(PX(32), PY(92), (int)((p / 100.0) * PX(256)), PY(16), ACCENT_CYAN);
                              tft.setTextColor(BG_COLOR);
                              tft.drawString(String(p) + "%", SCREEN_W/2, PY(100), 1);
                              lastP = p;
                          }
                      }
                  }
                  server.handleClient(); yield();
              }
              if(fastBuf) free(fastBuf);
          }
          http.end();
      } else { 
          File inFile = SD.open("/" + filename, FILE_READ);
          if (inFile) {
              sourceOk = true;
              totalBytes = inFile.size();
              String line; line.reserve(256);
              uint8_t *fastBuf = (uint8_t*)malloc(4096);
              
              while (inFile.available()) {
                  if (t0_map != 0 || (!foundMacro && linesRead < 500)) {
                      line = inFile.readStringUntil('\n');
                      processedBytes += line.length() + 1;
                      
                      bool hasCr = line.endsWith("\r");
                      if (hasCr) line.remove(line.length() - 1);
                      
                      if (t0_map != 0) {
                          if (line.indexOf("T0") != -1) {
                              line.replace("T0 ", "T" + String(t0_map) + " ");
                              line.replace("T0\n", "T" + String(t0_map) + "\n");
                              line.replace("T0\r", "T" + String(t0_map) + "\r");
                              if(line.endsWith("T0")) line.replace("T0", "T" + String(t0_map));
                          }
                      }

                      if (!foundMacro) {
                          if (line.startsWith("START_PRINT") || line.startsWith("PRINT_START")) {
                              outFile.print(line + injection + (hasCr ? "\r\n" : "\n"));
                              foundMacro = true;
                          } else {
                              outFile.print(line + (hasCr ? "\r\n" : "\n"));
                          }
                          linesRead++;
                      } else {
                          outFile.print(line + (hasCr ? "\r\n" : "\n"));
                      }
                  } else {
                      if (fastBuf) {
                          int c = inFile.read(fastBuf, 4096);
                          if (c > 0) {
                              outFile.write(fastBuf, c);
                              processedBytes += c;
                          }
                      }
                  }
                  
                  if (totalBytes > 0) {
                      int p = (int)(((uint64_t)processedBytes * 100) / totalBytes);
                      if (p > lastP && p <= 100) {
                          tft.fillRect(PX(32), PY(92), (int)((p / 100.0) * PX(256)), PY(16), ACCENT_CYAN);
                          tft.setTextColor(BG_COLOR);
                          tft.drawString(String(p) + "%", SCREEN_W/2, PY(100), 1);
                          lastP = p;
                      }
                  }
                  server.handleClient(); yield();
              }
              if(fastBuf) free(fastBuf);
              inFile.close();
          }
      }
      outFile.close();

      // Never upload/print a missing or truncated copy.
      if (!sourceOk || (totalBytes > 0 && processedBytes < totalBytes)) {
          SD.remove("/dump_zone/" + targetFilename);
          showToast("Error", sourceOk ? "Download incomplete!" : "Could not read source file!", true);
          activeModal = 0; currentTab = 0; forceRedrawForToast = true; return;
      }

      tft.setTextColor(TFT_WHITE, BG_COLOR);
      tft.drawString("Uploading Patched File...", SCREEN_W/2, PY(130), 1);
      
      if(!uploadFileToKlipper("dump_zone/" + targetFilename, targetFilename, 150)) {
          showToast("Error", "Upload to printer failed!", true);
          activeModal = 0; currentTab = 0; forceRedrawForToast = true; return;
      }
  } else {
      if (source == 1) {
          tft.drawString("Uploading to Printer...", SCREEN_W/2, PY(110), 1);
          if(!uploadFileToKlipper(filename, targetFilename, 130)) {
              showToast("Error", "Upload failed", true);
              activeModal = 0; currentTab = 0; forceRedrawForToast = true; return;
          }
      }
  }

  tft.setTextColor(TFT_WHITE, BG_COLOR);
  tft.drawString("Configuring & Launching...", SCREEN_W/2, PY(200), 1);

  sendGcode("SET_GCODE_VARIABLE MACRO=print_task_config VARIABLE=auto_bed_leveling VALUE=" + String(bBed ? 1 : 0));
  sendGcode("SET_GCODE_VARIABLE MACRO=print_task_config VARIABLE=time_lapse_camera VALUE=" + String(bTime ? 1 : 0));

  delay(300); 

  HTTPClient http;
  http.begin("http://" + String(printerIP) + ":7125/printer/print/start?filename=" + urlEncode(targetFilename));
  http.POST("");
  http.end();
  
  showToast("Print Started", "File mapped & launched!");
  
  activeModal = 0;
  currentTab = 0;
  forceRedrawForToast = true;
}

#if defined(BOARD_WAVESHARE_43) || defined(BOARD_40_INCH) || defined(BOARD_CYD_28) || defined(BOARD_28_INCH) || defined(BOARD_28_DUAL) || defined(BOARD_CYD_28_DUAL) || defined(BOARD_SUNTON_70)

void drawAirplaneIcon(int x, int y, float angle, uint16_t color) {
    float rad = (angle - 90) * PI / 180.0;
    int scale = (SCREEN_H < 280) ? 7 : 11; 
    int x1 = x + cos(rad) * scale; int y1 = y + sin(rad) * scale;
    int x2 = x + cos(rad + 2.5) * (scale * 0.66); int y2 = y + sin(rad + 2.5) * (scale * 0.66);
    int x3 = x + cos(rad - 2.5) * (scale * 0.66); int y3 = y + sin(rad - 2.5) * (scale * 0.66);
    tft.fillTriangle(x1, y1, x2, y2, x3, y3, color); 
    tft.drawTriangle(x1, y1, x2, y2, x3, y3, TFT_BLACK); 
}

void drawRadarTab() {
  tft.setTextSize(1);
  
  uint16_t bgDark = tft.color565(15, 15, 15);
  uint16_t radarGreen = tft.color565(0, 150, 50);
  uint16_t textGreen = tft.color565(0, 255, 120);
  
  int leftBarW = PX(42);
  
  int rightBarW = (SCREEN_W - leftBarW) * 0.35;
  if (rightBarW < 95) rightBarW = 95;
  if (rightBarW > 170) rightBarW = 170;
  
  int rightBarX = SCREEN_W - rightBarW;

  tft.fillRect(leftBarW, 0, rightBarX - leftBarW, SCREEN_H, bgDark); 
  uint16_t tableBg = tft.color565(10, 10, 10); 
  tft.fillRect(rightBarX, 0, rightBarW, SCREEN_H, tableBg);
  tft.drawFastVLine(rightBarX, 0, SCREEN_H, radarGreen);
  
  int cx = leftBarW + ((rightBarX - leftBarW) / 2);
  int cy = SCREEN_H / 2;
  
  int maxR = (cy - 18 < (cx - leftBarW - 18)) ? (cy - 18) : (cx - leftBarW - 18);
  if (maxR < 30) maxR = 30;
  int step = maxR / 4;
  
  tft.drawCircle(cx, cy, step, radarGreen);
  tft.drawCircle(cx, cy, step*2, radarGreen);
  tft.drawCircle(cx, cy, step*3, radarGreen);
  tft.drawCircle(cx, cy, maxR, radarGreen);
  
  tft.drawLine(cx, cy - maxR, cx, cy + maxR, radarGreen);
  tft.drawLine(cx - maxR, cy, cx + maxR, cy, radarGreen);
  
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, bgDark);
  tft.setTextFont(1);
  tft.drawString("N", cx, cy - maxR - 8);
  tft.drawString("S", cx, cy + maxR + 8);
  tft.drawString("W", cx - maxR - 8, cy);
  tft.drawString("E", cx + maxR + 8, cy);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(textGreen, tableBg);
  tft.drawString((rightBarW < 110) ? "DATA" : "FLIGHT DATA", rightBarX + 6, 6);
  tft.drawFastHLine(rightBarX, 20, rightBarW, radarGreen);
  
  int tableY = 25;
  int itemSpacing = (SCREEN_H < 280) ? 30 : 38; 
  
  for (int i = 0; i < currentPlaneCount; i++) {
      float dLat = planes[i].lat - homeLat;
      float dLon = planes[i].lon - homeLon;
      int px = cx + (int)((dLon / radarRadiusDeg) * maxR);
      int py = cy - (int)((dLat / radarRadiusDeg) * maxR);

      if (px > leftBarW + 5 && px < rightBarX - 5 && py > 5 && py < SCREEN_H - 15) {
          drawAirplaneIcon(px, py, planes[i].track, TFT_YELLOW);
          
          String cs = planes[i].callsign; cs.trim(); 
          
          if (cs.length() > 0 && tableY < SCREEN_H - itemSpacing) {
              if (rightBarW >= 130) tft.setTextFont(2); else tft.setTextFont(1);
              tft.setTextColor(TFT_YELLOW, tableBg);
              tft.drawString(cs.c_str(), rightBarX + 6, tableY);
              
              tft.setTextFont(1);
              tft.setTextColor(textGreen, tableBg);
              
              int altFt = (int)(planes[i].altitude * 3.28084);
              String altStr = (altFt >= 1000) ? String(altFt / 1000) + "k" : String(altFt);
              
              String details = (rightBarW < 130) 
                  ? String((int)planes[i].track) + "deg | " + altStr 
                  : "HDG " + String((int)planes[i].track) + "deg | " + altStr + " FT";
              
              int subY = (rightBarW >= 130) ? tableY + 16 : tableY + 12;
              tft.drawString(details.c_str(), rightBarX + 6, subY); 
              
              tft.drawFastHLine(rightBarX + 2, subY + 12, rightBarW - 4, tft.color565(20, 45, 20));
              tableY += itemSpacing; 
          }
      }
  }
  
  tft.fillRect(leftBarW, SCREEN_H - 18, rightBarX - leftBarW - 1, 18, bgDark);
  tft.setTextColor(textGreen, bgDark);
  tft.setTextDatum(MC_DATUM); 
  tft.setTextFont(1); 
  tft.drawString(radarStatus.c_str(), cx, SCREEN_H - 9);
  tft.setTextDatum(TL_DATUM);
  
  prevScanAngle = -1.0; 
  tft.setTextSize(TEXT_SCALE); 
}

void fetchOpenSkyData() {
  if (radarAuthMode == 1 && radarClientId.length() > 0 && radarClientSecret.length() > 0) {
      if (radarToken == "" || millis() > radarTokenExpiry) {
          WiFiClientSecure authClient;
          authClient.setInsecure(); 
          HTTPClient httpAuth;
          
          httpAuth.useHTTP10(true); 
          httpAuth.begin(authClient, "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token");
          httpAuth.addHeader("Content-Type", "application/x-www-form-urlencoded");
          String payload = "grant_type=client_credentials&client_id=" + radarClientId + "&client_secret=" + radarClientSecret;
          
          int authCode = httpAuth.POST(payload);
          if (authCode == 200) {
              JsonDocument authDoc;
              deserializeJson(authDoc, httpAuth.getStream());
              radarToken = authDoc["access_token"].as<String>();
              int expires = authDoc["expires_in"].as<int>();
              radarTokenExpiry = millis() + ((expires - 60) * 1000UL); 
          } else {
              radarStatus = "Auth Fail: " + String(authCode);
              httpAuth.end();
              return;
          }
          httpAuth.end();
      }
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  
  String lamin = String(homeLat - radarRadiusDeg, 4);
  String lomin = String(homeLon - radarRadiusDeg, 4);
  String lamax = String(homeLat + radarRadiusDeg, 4);
  String lomax = String(homeLon + radarRadiusDeg, 4);
  
  String url = "https://opensky-network.org/api/states/all?lamin=" + lamin + "&lomin=" + lomin + "&lamax=" + lamax + "&lomax=" + lomax;
  
  http.useHTTP10(true);
  http.begin(client, url);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("User-Agent", "ESP32-AirspaceSlicer/1.0");
  http.setTimeout(10000); 
  
  if (radarAuthMode == 1 && radarToken != "") {
      http.addHeader("Authorization", "Bearer " + radarToken);
  }
  
  int httpCode = http.GET();
  if (httpCode == 200) {
      JsonDocument doc; 
      DeserializationError error = deserializeJson(doc, http.getStream());
      
      if (!error) {
          JsonArray states = doc["states"];
          currentPlaneCount = 0;
          if (!states.isNull()) {
              for (JsonVariant state : states) {
                  if (currentPlaneCount >= 20) break; 
                  JsonArray planeData = state.as<JsonArray>();
                  if (!planeData[1].isNull()) planes[currentPlaneCount].callsign = planeData[1].as<String>();
                  if (!planeData[5].isNull()) planes[currentPlaneCount].lon = planeData[5].as<float>();
                  if (!planeData[6].isNull()) planes[currentPlaneCount].lat = planeData[6].as<float>();
                  if (!planeData[7].isNull()) planes[currentPlaneCount].altitude = planeData[7].as<float>();
                  if (!planeData[10].isNull()) planes[currentPlaneCount].track = planeData[10].as<float>();
                  
                  if (planes[currentPlaneCount].lat != 0.0) {
                      currentPlaneCount++;
                  }
              }
              radarStatus = "Tracking " + String(currentPlaneCount) + " planes";
          } else {
              radarStatus = "Airspace Clear";
          }
      } else {
          radarStatus = String("JSON Error: ") + error.c_str(); 
      }
  } else {
      radarStatus = "API Error: " + String(httpCode);
  }
  http.end();
}

void updateRadar() {
  if (currentTab != 5 || !enableRadar) return;
  
  if (millis() - lastRadarApiCall > 30000 || lastRadarApiCall == 0) {
      lastRadarApiCall = millis();
      radarStatus = "Updating...";
      drawRadarTab(); 
      fetchOpenSkyData(); 
      drawRadarTab(); 
      return;
  }

  if (millis() - lastSweepAnim > 33) { 
      lastSweepAnim = millis();
      
      int leftBarW = PX(42);
      int rightBarW = (SCREEN_W - leftBarW) * 0.35;
      if (rightBarW < 95) rightBarW = 95;
      if (rightBarW > 170) rightBarW = 170;
      
      int rightBarX = SCREEN_W - rightBarW;
      int cx = leftBarW + ((rightBarX - leftBarW) / 2);
      int cy = SCREEN_H / 2;
      
      int maxR = (cy - 18 < (cx - leftBarW - 18)) ? (cy - 18) : (cx - leftBarW - 18);
      if (maxR < 30) maxR = 30;
      int step = maxR / 4;
      
      uint16_t bgDark = tft.color565(15, 15, 15);
      uint16_t radarGreen = tft.color565(0, 150, 50);
      uint16_t textGreen = tft.color565(0, 255, 120);

      if (prevScanAngle >= 0.0) {
          float oldRad = prevScanAngle * DEG_TO_RAD;
          int oldBx = cx + (int)(cos(oldRad) * maxR);
          int oldBy = cy + (int)(sin(oldRad) * maxR);
          tft.drawLine(cx, cy, oldBx, oldBy, bgDark); 
          
          tft.drawCircle(cx, cy, step, radarGreen);
          tft.drawCircle(cx, cy, step*2, radarGreen);
          tft.drawCircle(cx, cy, step*3, radarGreen);
          tft.drawCircle(cx, cy, maxR, radarGreen);
          tft.drawLine(cx, cy - maxR, cx, cy + maxR, radarGreen);
          tft.drawLine(cx - maxR, cy, cx + maxR, cy, radarGreen);
      }

      scanAngle += 4.0; 
      if (scanAngle >= 360.0) scanAngle -= 360.0;

      float newRad = scanAngle * DEG_TO_RAD;
      int newBx = cx + (int)(cos(newRad) * maxR);
      int newBy = cy + (int)(sin(newRad) * maxR);
      tft.drawLine(cx, cy, newBx, newBy, textGreen);

      for (int i = 0; i < currentPlaneCount; i++) {
          float dLat = planes[i].lat - homeLat;
          float dLon = planes[i].lon - homeLon;
          int px = cx + (int)((dLon / radarRadiusDeg) * maxR);
          int py = cy - (int)((dLat / radarRadiusDeg) * maxR);

          if (px > leftBarW + 5 && px < rightBarX - 5 && py > 5 && py < SCREEN_H - 15) {
              drawAirplaneIcon(px, py, planes[i].track, TFT_YELLOW);
          }
      }

      prevScanAngle = scanAngle;
  }
}

#endif

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.printf("\n--- BOOT DIAGNOSTIC ---\nPSRAM Total Size: %d bytes\n-----------------------\n", ESP.getPsramSize());

#if !defined(BOARD_WAVESHARE_43) && !defined(BOARD_SUNTON_70)
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
#endif

  tft.init();
  tft.setSwapBytes(true); // <--- ADDED FIX FOR THE GREEN TINT

#if defined(BOARD_SUNTON_70)
  pinMode(2, OUTPUT);
  digitalWrite(2, HIGH); 
  tft.setBrightness(150);
#endif

  preferences.begin("printer", false);
  
  int screenRotation = preferences.getInt("rotation", 3);
  
#if defined(BOARD_WAVESHARE_43) || defined(BOARD_SUNTON_70)
  if (screenRotation == 3) screenRotation = 0; 
#endif

  tft.setRotation(screenRotation);

#if !defined(BOARD_WAVESHARE_43) && !defined(BOARD_SUNTON_70)
  invertDisplay = preferences.getBool("invertDisplay", false);
  tft.invertDisplay(invertDisplay);
#endif

  currentTheme = preferences.getInt("theme", 0);
  customBG = preferences.getString("customBG", customBG);
  customSidebar = preferences.getString("customSidebar", customSidebar);
  customCard = preferences.getString("customCard", customCard);
  customText = preferences.getString("customText", customText);
  customAccent = preferences.getString("customAccent", customAccent);
  
  showBattery = preferences.getBool("showBattery", false); 
  envMode = preferences.getInt("envMode", 0); 
  useFahrenheit = preferences.getBool("useFahrenheit", false); 
  tempOffset = preferences.getFloat("tempOffset", 0.0); 
  humOffset = preferences.getFloat("humOffset", 0.0);   
  enableAudio = preferences.getBool("enableAudio", false);
  
#if defined(BOARD_WAVESHARE_43) || defined(BOARD_40_INCH) || defined(BOARD_CYD_28) || defined(BOARD_28_INCH) || defined(BOARD_28_DUAL) || defined(BOARD_CYD_28_DUAL) || defined(BOARD_SUNTON_70)
  enableRadar = preferences.getBool("enableRadar", false);
  homeLat = preferences.getFloat("radarLat", 41.9742f);
  homeLon = preferences.getFloat("radarLon", -87.9073f);
  radarRadiusDeg = preferences.getFloat("radarRad", 0.80f);
  radarAuthMode = preferences.getInt("radarAuthMode", 0);
  radarClientId = preferences.getString("radarClientId", "");
  radarClientSecret = preferences.getString("radarClientSecret", "");
#endif

#if defined(BOARD_40_INCH) || defined(BOARD_WAVESHARE_43) || defined(BOARD_SUNTON_70)
  #ifdef BOARD_WAVESHARE_43
    Wire.begin(8, 9);
  #elif defined(BOARD_SUNTON_70)
    // Disabled to prevent I2C collision with GT911 Touch Controller (GPIO 19/20)
  #else
    bmeSDA = 32;
    bmeSCL = 25;
    Wire.begin(bmeSDA, bmeSCL); 
  #endif
  
  #if defined(BOARD_SUNTON_70)
    // No free I2C bus on the Sunton: GPIO 19/20 belong to the GT911 touch
    // controller (I2C port 0), and Wire's default S3 pins (8/9) are RGB data
    // lines. Probing the BME280 here would disturb the touch/display.
    bmeReady = false;
  #else
    // Only probe when the env sensor is enabled (saves ~2s of boot time).
    if (preferences.getInt("envMode", 0) > 0) {
      for (int i = 0; i < 4; i++) {
        delay(500);
        if (pingBME()) {
          bmeReady = true;
          Serial.println("BME280 connected successfully at boot!");
          break;
        }
      }
    }
  #endif
  // Thermocouple is initialised later (after the SD SPI bus is up), and only
  // when tcMode is enabled.
#endif

  applyTheme();
  tft.setTextSize(TEXT_SCALE); 
  drawBootLogo();

#if defined(BOARD_CYD_28) || defined(BOARD_28_DUAL) || defined(BOARD_CYD_28_DUAL)
  touch.begin();
#ifdef TOUCH_DEBUG
  // Diagnostic: show raw XPT2046 readings from BOTH wirings, on screen + serial, for 60s.
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(1);
  for (unsigned long t0 = millis(); millis() - t0 < 60000; ) {
    int v[2][5];
    for (int m = 0; m < 2; m++) {
      touch.shared = (m == 1);
      touch.select();
      v[m][0] = touch.xfer(0xB1); v[m][1] = touch.xfer(0xC1);
      v[m][2] = touch.xfer(0xD1); v[m][3] = touch.xfer(0x91);
      v[m][4] = touch.readTemp();
      touch.xfer(0x90);
      touch.deselect();
    }
    touch.shared = false;
    int irq = digitalRead(36);
    Serial.printf("TDBG CLASSIC z1=%4d z2=%4d x=%4d y=%4d | SHARED z1=%4d z2=%4d x=%4d y=%4d | irq36=%d tempC=%d tempS=%d\n",
                  v[0][0], v[0][1], v[0][2], v[0][3], v[1][0], v[1][1], v[1][2], v[1][3], irq, v[0][4], v[1][4]);
    char buf[64];
    tft.setTextColor(TFT_YELLOW, TFT_BLACK); tft.setTextDatum(TL_DATUM);
    tft.drawString("TOUCH TEST - press & hold, then release", 5, 5, 2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    snprintf(buf, sizeof(buf), "CLASSIC z1=%4d z2=%4d   ", v[0][0], v[0][1]); tft.drawString(buf, 5, 40, 2);
    snprintf(buf, sizeof(buf), "        x=%4d  y=%4d   ", v[0][2], v[0][3]);  tft.drawString(buf, 5, 60, 2);
    snprintf(buf, sizeof(buf), "SHARED  z1=%4d z2=%4d   ", v[1][0], v[1][1]); tft.drawString(buf, 5, 100, 2);
    snprintf(buf, sizeof(buf), "        x=%4d  y=%4d   ", v[1][2], v[1][3]);  tft.drawString(buf, 5, 120, 2);
    snprintf(buf, sizeof(buf), "T=%d/%d IRQ=%d %2lus left  ", v[0][4], v[1][4], irq, (60000 - (millis() - t0)) / 1000); tft.drawString(buf, 5, 160, 2);
    delay(150);
  }
  tft.setTextSize(TEXT_SCALE);
#endif
  if (SD.begin(SD_CS)) {
    sdCardReady = true;
    sdCardTotal = SD.totalBytes() / (1024 * 1024); 
    sdCardUsed = SD.usedBytes() / (1024 * 1024);
    loadFilesFromSD();
  }
#elif defined(BOARD_40_INCH)
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (SD.begin(SD_CS, sdSPI, 4000000)) {
    sdCardReady = true;
    sdCardTotal = SD.totalBytes() / (1024 * 1024); 
    sdCardUsed = SD.usedBytes() / (1024 * 1024);
    loadFilesFromSD();
  }
#elif defined(BOARD_WAVESHARE_43) || defined(BOARD_SUNTON_70)
  isCalibrated = true; 
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  #ifdef BOARD_WAVESHARE_43
  tft._light_instance.write_pin(4, false);
  #endif
  delay(20);

  if (SD.begin(SD_CS, sdSPI, 25000000)) {
    sdCardReady = true;
    sdCardTotal = SD.totalBytes() / (1024 * 1024);
    sdCardUsed  = SD.usedBytes()  / (1024 * 1024);
    loadFilesFromSD();
    Serial.println("SD Card OK");
  } else {
    Serial.println("SD Card Failed");
    sdCardReady = false;
  }
#endif

tcMode = preferences.getInt("tcMode", 0);

#if defined(BOARD_40_INCH) || defined(BOARD_WAVESHARE_43) || defined(BOARD_SUNTON_70)
  if (tcMode > 0) {
    if (thermocouple.begin()) {
      max31855Ready = true;
      Serial.println("MAX31855 Thermocouple initialized!");
    } else {
      Serial.println("MAX31855 Thermocouple NOT detected.");
    }
  }
#endif

  if (enableAudio && sdCardReady) {
      xTaskCreatePinnedToCore(audioTask, "AudioTask", 8192, NULL, 1, NULL, 0);
  }

  String macSuffix = WiFi.macAddress();
  macSuffix.replace(":", "");
  macSuffix = macSuffix.substring(macSuffix.length() - 4);

  String defaultMDNS = "u1-display-" + macSuffix;
  String defaultAP = "Snapmaker-U1-" + macSuffix;

  String savedIP = preferences.getString("printer_ip", "192.168.1.100");
  String savedName = preferences.getString("printer_name", "Snapmaker U1");
  String savedMDNS = preferences.getString("mdns_name", defaultMDNS);
  
  savedIP.toCharArray(printerIP, sizeof(printerIP));
  savedName.toCharArray(printerName, sizeof(printerName));
  savedMDNS.toCharArray(mdnsName, sizeof(mdnsName));

  for (int i=0; i<4; i++) {
     String tKey = "filType" + String(i);
     String cKey = "filColor" + String(i);
     filType[i] = preferences.getString(tKey.c_str(), filType[i]);
     String hex = preferences.getString(cKey.c_str(), rgb565ToHex(filColor[i]));
     filColor[i] = hexToRGB565(hex);
  }
  
  bool loadedFromSD = false;
#if defined(BOARD_CYD_28) || defined(BOARD_28_DUAL) || defined(BOARD_CYD_28_DUAL)
  // Calibration format v2 (new touch driver). Old v1 data used a different
  // mapping, so it is ignored and the user is asked to calibrate once.
  // Calibration is only valid for the rotation it was taken in.
  isCalibrated = false;
  if (sdCardReady && SD.exists("/calibration.json")) {
    File file = SD.open("/calibration.json", FILE_READ);
    if (file) {
      JsonDocument doc;
      if (!deserializeJson(doc, file)) {
        if (doc["v"] == 2 && doc["rot"] == screenRotation) {
            touch.x0 = doc["x0"]; touch.x1 = doc["x1"];
            touch.y0 = doc["y0"]; touch.y1 = doc["y1"];
            touch.swapXY = doc["swap"];
            loadedFromSD = true;
            isCalibrated = true;
        }
      }
      file.close();
    }
  }
  if (!loadedFromSD && preferences.getBool("tcV2", false) && preferences.getInt("tcRot", -1) == screenRotation) {
    touch.x0 = preferences.getInt("tcX0", 0);
    touch.x1 = preferences.getInt("tcX1", 4095);
    touch.y0 = preferences.getInt("tcY0", 0);
    touch.y1 = preferences.getInt("tcY1", 4095);
    touch.swapXY = preferences.getBool("tcSwap", false);
    isCalibrated = true;
  }
#elif defined(BOARD_40_INCH)
  if (sdCardReady && SD.exists("/calibration.json")) {
    File file = SD.open("/calibration.json", FILE_READ);
    if (file) {
      JsonDocument doc;
      if (!deserializeJson(doc, file)) {
          if(!doc["is_40"].isNull()) {
              uint16_t calData[5];
              for(int i=0; i<5; i++) calData[i] = doc["cal"][i];
              tft.setTouch(calData);
              loadedFromSD = true;
              isCalibrated = true;
          }
      }
      file.close();
    }
  }
  if (!loadedFromSD) {
     isCalibrated = preferences.getBool("isCalibrated", false);
     if (isCalibrated) {
        uint16_t calData[5];
        calData[0] = preferences.getInt("cal0", 0);
        calData[1] = preferences.getInt("cal1", 0);
        calData[2] = preferences.getInt("cal2", 0);
        calData[3] = preferences.getInt("cal3", 0);
        calData[4] = preferences.getInt("cal4", 0);
        tft.setTouch(calData);
     }
  }
#endif

  WiFiManager wm;
  wm.setSaveConfigCallback(saveConfigCallback);
  wm.setAPCallback(configModeCallback);
  
  WiFiManagerParameter custom_ip("printer_ip", "U1 IP Address", printerIP, 32);
  WiFiManagerParameter custom_name("printer_name", "Printer Name", printerName, 32);

  wm.addParameter(&custom_ip);
  wm.addParameter(&custom_name);

  if (!wm.autoConnect(defaultAP.c_str())) ESP.restart();
  
  if (shouldSaveConfig) {
    strcpy(printerIP, custom_ip.getValue());
    strcpy(printerName, custom_name.getValue());
    preferences.putString("printer_ip", printerIP);
    preferences.putString("printer_name", printerName);
  }

  // NOTE: Preferences stays open for the lifetime of the firmware. Calling
  // preferences.end() here made every later put*() (on-screen IP/name edits,
  // filament changes, touch calibration) silently fail.

  if (MDNS.begin(mdnsName)) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS started");
  }
  
  server.on("/", HTTP_GET, handleRoot);
  server.on("/audio", HTTP_GET, handleAudio);
  server.on("/tech", HTTP_GET, handleTech);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/save_fil", HTTP_POST, handleSaveFilament);
  server.on("/control", HTTP_POST, handleControl);
  server.on("/calibrate", HTTP_GET, handleCalibrateWeb);
  
  server.on("/setup_sd", HTTP_GET, []() {
      if (sdCardReady) {
          if (!SD.exists("/dump_zone")) SD.mkdir("/dump_zone");
          showToast("SD Setup", "Required folders created!");
      } else {
          showToast("Error", "SD Card not found!", true);
      }
      server.sendHeader("Location", "/tech", true);
      server.send(302, "text/plain", "");
  });

  server.on("/vol_up", HTTP_GET, []() {
      currentVolume += 10;
      if (currentVolume > 100) currentVolume = 100;
      if (enableAudio && audioQueue != NULL) {
          audioMessage msg;
          msg.cmd = CMD_SET_VOLUME;
          msg.value = currentVolume;
          xQueueSend(audioQueue, &msg, portMAX_DELAY);
      }
      server.sendHeader("Location", "/audio", true);
      server.send(302, "text/plain", "");
  });
  
  server.on("/vol_down", HTTP_GET, []() {
      currentVolume -= 10;
      if (currentVolume < 0) currentVolume = 0;
      if (enableAudio && audioQueue != NULL) {
          audioMessage msg;
          msg.cmd = CMD_SET_VOLUME;
          msg.value = currentVolume;
          xQueueSend(audioQueue, &msg, portMAX_DELAY);
      }
      server.sendHeader("Location", "/audio", true);
      server.send(302, "text/plain", "");
  });
  
  server.on("/play_sound", HTTP_GET, []() {
      if(server.hasArg("file") && enableAudio && audioQueue != NULL) {
          audioMessage msg;
          msg.cmd = CMD_PLAY_FILE;
          strlcpy(msg.txt, server.arg("file").c_str(), sizeof(msg.txt));
          xQueueSend(audioQueue, &msg, portMAX_DELAY);
      }
      server.sendHeader("Location", "/audio", true);
      server.send(302, "text/plain", "");
  });

  server.on("/stop_sound", HTTP_GET, []() {
      if (enableAudio && audioQueue != NULL) {
          audioMessage msg;
          msg.cmd = CMD_STOP_AUDIO;
          xQueueSend(audioQueue, &msg, portMAX_DELAY);
      }
      server.sendHeader("Location", "/audio", true);
      server.send(302, "text/plain", "");
  });

  server.on("/upload", HTTP_POST, []() {
    server.sendHeader("Location", "/tech", true);
    server.send(302, "text/plain", "");
  }, handleFileUpload);

  server.on("/upload_audio", HTTP_POST, []() {
    server.sendHeader("Location", "/audio", true);
    server.send(302, "text/plain", "");
  }, handleFileUpload);

  server.begin();
  startPrinterPolling();
  fetchKlipperFiles();

  if (!isCalibrated) {
    touchCalibrate();
  } else {
    tft.fillScreen(BG_COLOR);
    drawSidebar();
    drawHomeTab();
  }
}

void loop() {
#ifdef PERF_DEBUG
  static unsigned long perfLoopStart = 0, perfLoopMax = 0, perfLoops = 0, perfWindow = 0;
  if (perfLoopStart) { unsigned long d = millis() - perfLoopStart; if (d > perfLoopMax) perfLoopMax = d; perfLoops++; }
  if (millis() - perfWindow > 5000) {
    Serial.printf("PERF loop: %lu iterations/5s, slowest %lu ms\n", perfLoops, perfLoopMax);
    perfWindow = millis(); perfLoopMax = 0; perfLoops = 0;
  }
  perfLoopStart = millis();
#endif
  server.handleClient(); 

#if defined(BOARD_WAVESHARE_43) || defined(BOARD_40_INCH) || defined(BOARD_CYD_28) || defined(BOARD_28_INCH) || defined(BOARD_28_DUAL) || defined(BOARD_CYD_28_DUAL) || defined(BOARD_SUNTON_70)
  updateRadar();
#endif

  int touchX = 0, touchY = 0;
  PERF_T0(touch)
  bool perfTouched = getTouchCoord(touchX, touchY);
#ifdef PERF_DEBUG
  { static unsigned long cnt = 0, sum = 0, mx = 0; unsigned long d = millis() - _perf_touch; sum += d; if (d > mx) mx = d;
    if (++cnt >= 200) { Serial.printf("PERF touch read avg %lu ms, max %lu ms\n", sum / cnt, mx); cnt = 0; sum = 0; mx = 0; } }
  if (perfTouched) Serial.printf("PERF touch at %d,%d\n", touchX, touchY);
#endif
  if (perfTouched) {

    // Touches are no longer ignored while a toast is showing (that locked the
    // screen for 3s after every button press).
    if (millis() - lastTouchTime > 250) { 
      bool modalForceClosed = false;

      if (activeModal > 0 && touchX < PX(42)) {
          delay(150); lastTouchTime = millis();
          activeModal = 0;
          modalForceClosed = true;
      }
      else if (activeModal > 0) {
        bool closedModal = false;
        bool returnToTool = false;
        int targetTool = 0;
        bool handledTouch = false;
        
        if (activeModal == 1 || activeModal == 2) {
          if (touchY > PY(30)) {
              int r = 0;
              if (touchY > PY(95) && touchY < PY(155)) r = 1;
              else if (touchY >= PY(155)) r = 2;
              
              int c = (touchX > PX(160)) ? 1 : 0;
              int i = r * 2 + c;
              
              int val = (activeModal == 1) ? presetBed[i] : presetTool[i];
              if (activeModal == 1) { sendGcode("M140 S" + String(val)); showToast("Heating Bed", String(val) + "C"); }
              else { 
                  if (activeTool >= 0) {
                      sendGcode("M104 T" + String(activeTool) + " S" + String(val)); 
                      showToast("Heating Tool " + String(activeTool+1), String(val) + "C"); 
                  } else {
                      showToast("Error", "No Tool Active", true);
                  }
              }
              int dummyX, dummyY;
              while(getTouchCoord(dummyX, dummyY)) { delay(10); server.handleClient(); yield(); }
              closedModal = true; handledTouch = true;
          } else { closedModal = true; }
        } 
        else if (activeModal == 7) {
          if (touchY > PY(35) && touchY < PY(115)) {
              int c = 0;
              if (touchX > PX(110) && touchX < PX(190)) c = 1;
              else if (touchX >= PX(190)) c = 2;
              int speeds[] = {50, 100, 150};
              sendGcode("M220 S" + String(speeds[c]));
              showToast("Print Speed", String(speeds[c]) + "%");
              int dummyX, dummyY; while(getTouchCoord(dummyX, dummyY)) { delay(10); server.handleClient(); yield(); }
              closedModal = true; handledTouch = true; 
          } else if (touchY >= PY(115) && touchY < PY(190)) {
              int c = 0;
              if (touchX > PX(110) && touchX < PX(190)) c = 1;
              else if (touchX >= PX(190)) c = 2;
              int fans[] = {0, 127, 255};
              sendGcode("M106 S" + String(fans[c]));
              showToast("Part Fan", String((int)(fans[c]*100/255)) + "%");
              int dummyX, dummyY; while(getTouchCoord(dummyX, dummyY)) { delay(10); server.handleClient(); yield(); }
              closedModal = true; handledTouch = true; 
          } else { closedModal = true; }
        }
        else if (activeModal == 8) {
          if (touchY > PY(35) && touchY < PY(160)) {
              int r = (touchY > PY(100)) ? 1 : 0;
              int c = (touchX > PX(160)) ? 1 : 0;
              int i = r * 2 + c;
              sendGcode("T" + String(i));
              showToast("Tool Change", "Swapped to T" + String(i+1));
              int dummyX, dummyY; while(getTouchCoord(dummyX, dummyY)) { delay(10); server.handleClient(); yield(); }
              closedModal = true; handledTouch = true; 
          } else if (touchY >= PY(160) && touchY < PY(230)) {
              String parkCmd = (activeTool <= 0) ? "park_extruder" : "park_extruder" + String(activeTool);
              sendGcode(parkCmd); 
              showToast("Tool Change", "Parking Tool");
              int dummyX, dummyY; while(getTouchCoord(dummyX, dummyY)) { delay(10); server.handleClient(); yield(); }
              closedModal = true; handledTouch = true;
          } else { closedModal = true; }
        }
        else if (activeModal == 9) { 
          if (touchY > PY(140) && touchY < PY(180)) {
              if (touchX > PX(60) && touchX < PX(160)) { 
                  closedModal = true; handledTouch = true; 
              }
              else if (touchX > PX(180) && touchX < PX(280)) { 
                  sendGcode("CANCEL_PRINT"); 
                  showToast("Command", "Cancelling...", true);
                  closedModal = true; handledTouch = true; 
              }
          } else { closedModal = true; }
        }
        else if (activeModal == 14) { 
          if (touchY > PY(140) && touchY < PY(180)) {
              if (touchX > PX(60) && touchX < PX(160)) { 
                  closedModal = true; handledTouch = true; 
              }
              else if (touchX > PX(180) && touchX < PX(280)) { 
                  if (currentState == "paused") { 
                    sendGcode("RESUME"); 
                    showToast("Command", "Resuming print..."); 
                  } else { 
                    sendGcode("PAUSE"); 
                    showToast("Command", "Pausing print..."); 
                  }
                  closedModal = true; handledTouch = true; 
              }
          } else { closedModal = true; }
        }
        else if (activeModal >= 10 && activeModal <= 13) {
          int tIdx = activeModal - 10;
          handledTouch = true; 
          
          if (touchY < PY(82)) {
            int c = 0;
            if (touchX > PX(90) && touchX <= PX(150)) c = 1;
            else if (touchX > PX(150) && touchX <= PX(215)) c = 2;
            else if (touchX > PX(215)) c = 3;
            
            if (c == 0) { sendGcode("M104 T" + String(tIdx) + " S0"); showToast("Heater Off", "Tool " + String(tIdx+1)); }
            else if (c == 1) { sendGcode("M104 T" + String(tIdx) + " S200"); showToast("Heating", "Tool " + String(tIdx+1) + " to 200C"); }
            else if (c == 2) { sendGcode("M104 T" + String(tIdx) + " S220"); showToast("Heating", "Tool " + String(tIdx+1) + " to 220C"); }
            else { sendGcode("M104 T" + String(tIdx) + " S240"); showToast("Heating", "Tool " + String(tIdx+1) + " to 240C"); }
          }
          else if (touchY >= PY(82) && touchY < PY(132)) {
            if (touchX < PX(160)) { 
              int dummyX, dummyY;
              while(getTouchCoord(dummyX, dummyY)) { delay(10); server.handleClient(); yield(); }
              activeModal = 60 + tIdx; 
              drawFilamentTypeModal(tIdx); 
              lastTouchTime = millis();
              return; 
            } 
            else { 
              int dummyX, dummyY;
              while(getTouchCoord(dummyX, dummyY)) { delay(10); server.handleClient(); yield(); }
              activeModal = 30 + tIdx; 
              drawColorPickerModal(tIdx); 
              lastTouchTime = millis();
              return; 
            }
          }
          else if (touchY >= PY(132) && touchY < PY(180)) {
            String toolName = (tIdx == 0) ? "extruder" : "extruder" + String(tIdx);
            String pickCmd = (tIdx == 0) ? "pick_extruder" : "pick_extruder" + String(tIdx);
            String parkCmd = (tIdx == 0) ? "park_extruder" : "park_extruder" + String(tIdx);

            if (touchX < PX(160)) { 
              if (toolAttached[tIdx]) { sendGcode(parkCmd); showToast("Tool", "Parking..."); }
              else { sendGcode(pickCmd); showToast("Tool", "Attaching T" + String(tIdx+1)); }
              toolAttached[tIdx] = !toolAttached[tIdx];
              drawToolModal(tIdx);
            } else {
              if (toolLoaded[tIdx]) { 
                sendGcode(pickCmd + "\nACTIVATE_EXTRUDER EXTRUDER=" + toolName + "\nINNER_FILAMENT_UNLOAD"); 
                showToast("Filament", "Unloading T" + String(tIdx+1)); 
              }
              else { 
                sendGcode(pickCmd + "\nACTIVATE_EXTRUDER EXTRUDER=" + toolName + "\nAUTO_FEEDING EXTRUDER=" + String(tIdx) + " LOAD=1"); 
                showToast("Filament", "Loading T" + String(tIdx+1)); 
              }
              toolLoaded[tIdx] = !toolLoaded[tIdx];
              drawToolModal(tIdx);
            }
          }
          else if (touchY >= PY(180)) {
            int dummyX, dummyY;
            while(getTouchCoord(dummyX, dummyY)) { delay(10); server.handleClient(); yield(); }
            closedModal = true;
          }
        }
        else if ((activeModal >= 20 && activeModal <= 23) || activeModal == 51) {
          int tIdx = (activeModal == 51) ? 0 : activeModal - 20;
          handledTouch = true; 
          
          if (touchY < PY(70)) { }
          else if (touchY >= PY(70) && touchY < PY(195)) {
              int r = (touchY - PY(70)) / PY(31); 
              if (r < 0) r = 0; if (r > 3) r = 3;
              
              int c = (touchX - PX(45)) / PX(27); 
              if (touchX > PX(275)) c = 9; 
              if (c < 0) c = 0; if (c > 9) c = 9;
              
              char key = kbRows[r][c];
              if (key == '<') {
                  if (kbInput.length() > 0) kbInput.remove(kbInput.length()-1);
              } else {
                  int maxLen = (kbMode == 0) ? 10 : ((kbMode == 3) ? 20 : 6);
                  if (kbInput.length() < maxLen) kbInput += key;
              }
              
              int dummyX, dummyY;
              while(getTouchCoord(dummyX, dummyY)) { delay(10); server.handleClient(); yield(); }
              
              updateKeyboardInputBox(); 
              lastTouchTime = millis();
          }
          else if (touchY >= PY(195)) {
             int dummyX, dummyY;
             while(getTouchCoord(dummyX, dummyY)) { delay(10); server.handleClient(); yield(); }
             
             if (touchX < PX(125)) { 
                closedModal = true; 
                if(activeModal != 51) { returnToTool = true; targetTool = tIdx; }
             }
             else if (touchX >= PX(125) && touchX < PX(230)) { 
                if ((kbMode == 0 || kbMode == 3) && kbInput.length() < ((kbMode == 3) ? 20 : 10)) { 
                    kbInput += " "; 
                    updateKeyboardInputBox(); 
                    lastTouchTime = millis();
                }
             }
             else if (touchX >= PX(230)) { 
                if (kbMode == 3) {
                   kbInput.toCharArray(printerName, sizeof(printerName));
                   preferences.putString("printer_name", printerName);
                   showToast("Saved", "Printer Name Updated!");
                }
                else if (kbMode == 0) {
                   filType[tIdx] = kbInput;
                   preferences.putString(("filType" + String(tIdx)).c_str(), kbInput);
                   syncFilamentToKlipper(tIdx);
                   showToast("Saved", "Tool " + String(tIdx+1) + " is now " + kbInput);
                } else {
                   if (kbInput.length() > 0) {
                     String hexStr = "#" + kbInput;
                     filColor[tIdx] = hexToRGB565(hexStr);
                     preferences.putString(("filColor" + String(tIdx)).c_str(), rgb565ToHex(filColor[tIdx]));
                     syncFilamentToKlipper(tIdx);
                     showToast("Saved", "Tool " + String(tIdx+1) + " color updated");
                   }
                }
                closedModal = true; 
                if(activeModal != 51) { returnToTool = true; targetTool = tIdx; }
             }
          }
        }
        else if (activeModal == 50) { 
          handledTouch = true; 
          
          if (touchY < PY(70)) { }
          else if (touchY >= PY(70) && touchY < PY(200)) {
              int r = 0;
              if (touchY > PY(105) && touchY <= PY(140)) r = 1;
              else if (touchY > PY(140) && touchY <= PY(170)) r = 2;
              else if (touchY > PY(170)) r = 3;

              int c = 0;
              if (touchX > PX(120) && touchX <= PX(180)) c = 1;
              else if (touchX > PX(180)) c = 2;
              
              int idx = r * 3 + c;
              if (idx == 11) { 
                  if (kbInput.length() > 0) kbInput.remove(kbInput.length()-1);
              } else if (idx == 9) { 
                  if (kbInput.length() < 15) kbInput += ".";
              } else if (idx == 10) { 
                  if (kbInput.length() < 15) kbInput += "0";
              } else {
                  if (kbInput.length() < 15) kbInput += String(idx + 1);
              }
              
              int dummyX, dummyY;
              while(getTouchCoord(dummyX, dummyY)) { delay(10); server.handleClient(); yield(); }
              
              updateKeyboardInputBox();
              lastTouchTime = millis();
          }
          else if (touchY >= PY(200)) {
              int dummyX, dummyY;
              while(getTouchCoord(dummyX, dummyY)) { delay(10); server.handleClient(); yield(); }
              
              if (touchX < PX(160)) { 
                  closedModal = true; 
              } else { 
                  kbInput.toCharArray(printerIP, sizeof(printerIP));
                  preferences.putString("printer_ip", printerIP);
                  showToast("Saved", "Printer IP Updated!");
                  closedModal = true; 
              }
          }
        }
        else if (activeModal >= 60 && activeModal <= 63) {
          int tIdx = activeModal - 60;
          handledTouch = true;
          
          if (touchY >= PY(30) && touchY < PY(230)) {
              int r = 0;
              if (touchY > PY(80) && touchY <= PY(125)) r = 1;
              else if (touchY > PY(125) && touchY <= PY(170)) r = 2;
              else if (touchY > PY(170)) r = 3;

              int c = 0;
              if (touchX > PX(120) && touchX <= PX(200)) c = 1;
              else if (touchX > PX(200)) c = 2;
              
              int i = r * 3 + c;
              
              int dummyX, dummyY;
              while(getTouchCoord(dummyX, dummyY)) { delay(10); server.handleClient(); yield(); }
              
              if (i < 10) { 
                  filType[tIdx] = FIL_TYPES[i];
                  preferences.putString(("filType" + String(tIdx)).c_str(), filType[tIdx]);
                  syncFilamentToKlipper(tIdx);
                  showToast("Saved", "Tool " + String(tIdx+1) + " is now " + filType[tIdx]);
                  closedModal = true; returnToTool = true; targetTool = tIdx; 
              } else if (i == 10) { 
                  kbInput = filType[tIdx];
                  kbMode = 0; 
                  activeModal = 20 + tIdx; 
                  drawKeyboardModal(tIdx);
                  lastTouchTime = millis();
                  return; 
              } else if (i == 11) { 
                  closedModal = true; returnToTool = true; targetTool = tIdx; 
              }
          }
        }
        else if (activeModal >= 30 && activeModal <= 33) {
          int tIdx = activeModal - 30;
          if (touchY >= PY(35) && touchY < PY(150)) {
              int r = (touchY > PY(90)) ? 1 : 0;
              int c = 0;
              if (touchX > PX(85) && touchX <= PX(135)) c = 1;
              else if (touchX > PX(135) && touchX <= PX(185)) c = 2;
              else if (touchX > PX(185) && touchX <= PX(235)) c = 3;
              else if (touchX > PX(235)) c = 4;
              
              int i = r * 5 + c;
              if (i >= 0 && i < 10) {
                  filColor[tIdx] = FIL_COLORS[i];
                  preferences.putString(("filColor" + String(tIdx)).c_str(), rgb565ToHex(filColor[tIdx]));
                  syncFilamentToKlipper(tIdx);
                  showToast("Saved", "Tool " + String(tIdx+1) + " color updated");
                  
                  int dummyX, dummyY;
                  while(getTouchCoord(dummyX, dummyY)) { delay(10); server.handleClient(); yield(); }
                  
                  closedModal = true; returnToTool = true; targetTool = tIdx; 
              }
          }
          else if (touchY >= PY(150) && touchY < PY(195)) { 
              int dummyX, dummyY;
              while(getTouchCoord(dummyX, dummyY)) { delay(10); server.handleClient(); yield(); }
              kbMode = 1; 
              kbInput = "";
              activeModal = 20 + tIdx; 
              drawKeyboardModal(tIdx);
              lastTouchTime = millis();
              return; 
          }
          else if (touchY >= PY(195)) { 
              int dummyX, dummyY;
              while(getTouchCoord(dummyX, dummyY)) { delay(10); server.handleClient(); yield(); }
              closedModal = true; returnToTool = true; targetTool = tIdx; 
          }
        }
        else if (activeModal == 40) { 
          if (touchY >= PY(40) && touchY < PY(70) && touchX < PX(200)) { optBedLevel = !optBedLevel; drawPrintOptionsModal(); handledTouch=true; }
          else if (touchY >= PY(70) && touchY < PY(100) && touchX < PX(200)) { optTimelapse = !optTimelapse; drawPrintOptionsModal(); handledTouch=true; }
          else if (touchY >= PY(100) && touchY < PY(130) && touchX < PX(200)) { optAdvMap = !optAdvMap; drawPrintOptionsModal(); handledTouch=true; }
          else if (touchY >= PY(140) && touchY < PY(195)) {
             int i = (touchX - PX(50)) / PX(55);
             if(i >= 0 && i < 4) { optT0 = i; drawPrintOptionsModal(); }
             handledTouch=true;
          }
          else if (touchY >= PY(195)) {
             int dummyX, dummyY;
             while(getTouchCoord(dummyX, dummyY)) { delay(10); server.handleClient(); yield(); }
             
             if (touchX < PX(145)) { 
                 closedModal = true; handledTouch=true; 
             } else {
                 launchPatchedPrint(selectedFile, printTabSource, optBedLevel, optTimelapse, optAdvMap, optT0);
                 return; 
             }
          }
          if (!handledTouch) closedModal = true;
        }
        
        if (closedModal) {
          if (returnToTool) {
             activeModal = 10 + targetTool;
             drawToolModal(targetTool);
          } else {
             activeModal = 0;
             tft.fillScreen(BG_COLOR);
             drawSidebar();
             if (currentTab == 0) drawHomeTab();
             else if (currentTab == 1) drawToolheadTab();
             else if (currentTab == 2) drawMoveTab();
             else if (currentTab == 3) drawPrintTab();
             else if (currentTab == 4) drawSettingsTab();
#if defined(BOARD_WAVESHARE_43) || defined(BOARD_40_INCH) || defined(BOARD_CYD_28) || defined(BOARD_28_INCH) || defined(BOARD_28_DUAL) || defined(BOARD_CYD_28_DUAL) || defined(BOARD_SUNTON_70)
             else if (currentTab == 5 && enableRadar) {
                 lastRadarApiCall = 0;
                 drawRadarTab();
             }
#endif
          }
        }
        if (!modalForceClosed) lastTouchTime = millis(); return; 
      }

      // --- SIDEBAR TOUCH CALCULATION FOR ALL BOARDS ---
      if (touchX < PX(42)) {
#if defined(BOARD_WAVESHARE_43) || defined(BOARD_40_INCH) || defined(BOARD_CYD_28) || defined(BOARD_28_INCH) || defined(BOARD_28_DUAL) || defined(BOARD_CYD_28_DUAL) || defined(BOARD_SUNTON_70)
        int numTabs = enableRadar ? 6 : 5;
#else
        int numTabs = 5;
#endif
        int tappedTab = touchY / (SCREEN_H / numTabs); 
        if (tappedTab < numTabs) {
          if (tappedTab != currentTab || modalForceClosed) {
            PERF_T0(tab)
            currentTab = tappedTab;
            
            if (currentTab == 3) {
               if (printTabSource == 0) fetchKlipperFiles();
               else loadFilesFromSD();
               fileScrollIndex = 0;
            }

            tft.fillScreen(BG_COLOR); 
            drawSidebar();
            
            if (currentTab == 0) drawHomeTab();
            else if (currentTab == 1) drawToolheadTab();
            else if (currentTab == 2) drawMoveTab();
            else if (currentTab == 3) drawPrintTab();
            else if (currentTab == 4) drawSettingsTab();
#if defined(BOARD_WAVESHARE_43) || defined(BOARD_40_INCH) || defined(BOARD_CYD_28) || defined(BOARD_28_INCH) || defined(BOARD_28_DUAL) || defined(BOARD_CYD_28_DUAL) || defined(BOARD_SUNTON_70)
            else if (currentTab == 5 && enableRadar) {
                lastRadarApiCall = 0; 
                drawRadarTab();
            }
#endif
            
            updateDynamicUI(); 
            PERF_LOG(tab, "tab switch")
          }
        }
        lastTouchTime = millis();
        return;
      } 
      
      // --- HOME TAB TOUCH ZONES ---
      if (currentTab == 0) {
        if (touchX > PX(48) && touchX < PX(178) && touchY > PY(22) && touchY < PY(76)) { activeModal = 1; drawTempModal(1); }
        else if (touchX > PX(184) && touchX < PX(314) && touchY > PY(22) && touchY < PY(76)) { activeModal = 8; drawActiveToolModal(); }
        else if (touchX > PX(48) && touchX < PX(178) && touchY > PY(82) && touchY < PY(136)) { activeModal = 2; drawTempModal(2); }
        else if (touchX > PX(184) && touchX < PX(314) && touchY > PY(82) && touchY < PY(136)) { activeModal = 7; drawSpeedFanModal(); }
        
        // PAUSE SAFETY POP-UP (Matches X: PX(48) to PX(178))
        else if (touchX > PX(48) && touchX < PX(178) && touchY > PY(174) && touchY < PY(226)) {
          activeModal = 14;
          drawPauseModal();
        }
        // STOP / CANCEL SAFETY POP-UP (Matches X: PX(184) to PX(314))
        else if (touchX > PX(184) && touchX < PX(314) && touchY > PY(174) && touchY < PY(226)) { 
          activeModal = 9; 
          drawCancelModal();
        }
      }
      else if (currentTab == 1) {
        for (int i=0; i<4; i++) {
           int bx = PX(45) + (i * PX(69));
           if (touchX > bx && touchX < bx + PX(64) && touchY > PY(40) && touchY < PY(200)) {
               activeModal = 10 + i; 
               drawToolModal(i);
               break;
           }
        }
      }
      else if (currentTab == 2) {
        if (touchY > PY(20) && touchY < PY(70)) {
          if (touchX > PX(60) && touchX < PX(110)) moveDist = 0.1;
          if (touchX > PX(120) && touchX < PX(170)) moveDist = 1.0;
          if (touchX > PX(180) && touchX < PX(230)) moveDist = 10.0;
          if (touchX > PX(240) && touchX < PX(290)) moveDist = 50.0;
          drawMoveTab(); 
        } else {
          if (touchX > PX(120) && touchX < PX(160) && touchY > PY(80) && touchY < PY(120)) { sendGcode("G91\nG1 Y" + String(moveDist) + " F6000\nG90"); showToast("Jog", "Y+ " + String(moveDist)); }
          if (touchX > PX(120) && touchX < PX(160) && touchY > PY(180) && touchY < PY(220)) { sendGcode("G91\nG1 Y-" + String(moveDist) + " F6000\nG90"); showToast("Jog", "Y- " + String(moveDist)); }
          if (touchX > PX(70) && touchX < PX(110) && touchY > PY(130) && touchY < PY(170)) { sendGcode("G91\nG1 X-" + String(moveDist) + " F6000\nG90"); showToast("Jog", "X- " + String(moveDist)); }
          if (touchX > PX(170) && touchX < PX(210) && touchY > PY(130) && touchY < PY(170)) { sendGcode("G91\nG1 X" + String(moveDist) + " F6000\nG90"); showToast("Jog", "X+ " + String(moveDist)); }
          if (touchX > PX(120) && touchX < PX(160) && touchY > PY(130) && touchY < PY(170)) { sendGcode("G28"); showToast("Homing", "All Axes"); }
          
          if (touchX > PX(235) && touchX < PX(285) && touchY > PY(80) && touchY < PY(120)) { sendGcode("G91\nG1 Z" + String(moveDist) + " F600\nG90"); showToast("Jog", "Z+ " + String(moveDist)); }
          if (touchX > PX(235) && touchX < PX(285) && touchY > PY(130) && touchY < PY(170)) { sendGcode("G28 Z"); showToast("Homing", "Z axis"); }
          if (touchX > PX(235) && touchX < PX(285) && touchY > PY(180) && touchY < PY(220)) { sendGcode("G91\nG1 Z-" + String(moveDist) + " F600\nG90"); showToast("Jog", "Z- " + String(moveDist)); }
        }
      }
      else if (currentTab == 3) {
        if (touchY < PY(40)) {
            if (touchX > PX(45) && touchX < PX(140) && printTabSource != 0) { 
                printTabSource = 0; fileScrollIndex = 0; fetchKlipperFiles(); drawPrintTab(); 
                showToast("File Source", "Loaded Printer Klipper Files");
            }
            if (touchX >= PX(140) && touchX < PX(240) && printTabSource != 1) { 
                printTabSource = 1; fileScrollIndex = 0; loadFilesFromSD(); drawPrintTab(); 
                showToast("File Source", "Loaded Screen SD Files");
            }
            lastTouchTime = millis();
            return;
        }

        int count = printTabSource == 0 ? klipperFileCount : fileCount;
        String* list = printTabSource == 0 ? klipperFileList : fileList;

        if (touchX > PX(260)) {
           if (touchY < PY(130) && fileScrollIndex > 0) { fileScrollIndex--; drawPrintTab(); }
           if (touchY > PY(130) && fileScrollIndex + 4 < count) { fileScrollIndex++; drawPrintTab(); }
        } else if (touchX > PX(50) && touchX < PX(250)) {
           int i = (touchY - PY(45)) / PY(45);
           if (i >= 0 && i < 4) {
              int idx = fileScrollIndex + i;
              if (idx < count) {
                 selectedFile = list[idx];
                 activeModal = 40;
                 drawPrintOptionsModal();
              }
           }
        }
      }
      else if (currentTab == 4) {
        if (touchX > PX(48) && touchX < PX(178) && touchY > PY(130) && touchY < PY(170)) { 
            int dummyX, dummyY;
            while(getTouchCoord(dummyX, dummyY)) { delay(10); server.handleClient(); yield(); }
            kbInput = String(printerIP);
            kbMode = 2; // IP Mode
            activeModal = 50; 
            drawKeyboardModal(0);
            lastTouchTime = millis();
        }
        else if (touchX > PX(184) && touchX < PX(314) && touchY > PY(130) && touchY < PY(170)) {
            int dummyX, dummyY;
            while(getTouchCoord(dummyX, dummyY)) { delay(10); server.handleClient(); yield(); }
            kbInput = String(printerName);
            kbMode = 3; // Name Mode
            activeModal = 51; 
            drawKeyboardModal(0);
            lastTouchTime = millis();
        }
        else if (touchX > PX(48) && touchX < PX(178) && touchY > PY(175) && touchY < PY(215)) {
#if !defined(BOARD_WAVESHARE_43) && !defined(BOARD_SUNTON_70)
          touchCalibrate();
          tft.fillScreen(BG_COLOR);
          drawSidebar();
          drawSettingsTab();
#else
          showToast("Info", "Capacitive screens auto-calibrate!");
          forceRedrawForToast = true;
          lastTouchTime = millis();
#endif
        }
        else if (touchX > PX(184) && touchX < PX(314) && touchY > PY(175) && touchY < PY(215)) {
          tft.fillScreen(BG_COLOR);
          tft.setTextColor(TFT_YELLOW);
          tft.setTextDatum(MC_DATUM);
          tft.drawString("Resetting Wi-Fi...", SCREEN_W/2, PY(100), 2);
          delay(1500);
          WiFiManager wm;
          wm.resetSettings();
          ESP.restart();
        }
        else if (touchX > PX(48) && touchX < PX(200) && touchY > PY(220)) {
          showToast("Nates Print Shop", "Custom Firmware " + String(FW_VERSION) + " Active");
        }
      }
      lastTouchTime = millis();
    }
  }

  if (toastExpireTime > 0) {
      if (millis() > toastExpireTime) {
          toastExpireTime = 0;
          forceRedrawForToast = true;
      } else if (!toastDrawn) {
          drawToast();
          toastDrawn = true;
      }
  }

  if (forceRedrawForToast) {
      forceRedrawForToast = false;
      if (activeModal > 0) {
          if (activeModal == 1 || activeModal == 2) drawTempModal(activeModal);
          else if (activeModal == 7) drawSpeedFanModal();
          else if (activeModal == 8) drawActiveToolModal();
          else if (activeModal == 9) drawCancelModal();
          else if (activeModal == 14) drawPauseModal();
          else if (activeModal >= 10 && activeModal <= 13) drawToolModal(activeModal - 10);
          else if ((activeModal >= 20 && activeModal <= 23) || activeModal == 50 || activeModal == 51) drawKeyboardModal(activeModal >= 20 && activeModal <= 23 ? activeModal - 20 : 0);
          else if (activeModal >= 30 && activeModal <= 33) drawColorPickerModal(activeModal - 30);
          else if (activeModal == 40) drawPrintOptionsModal();
          else if (activeModal >= 60 && activeModal <= 63) drawFilamentTypeModal(activeModal - 60); 
      } else {
          tft.fillScreen(BG_COLOR);
          drawSidebar();
          if (currentTab == 0) drawHomeTab();
          else if (currentTab == 1) drawToolheadTab();
          else if (currentTab == 2) drawMoveTab();
          else if (currentTab == 3) drawPrintTab();
          else if (currentTab == 4) drawSettingsTab();
#if defined(BOARD_WAVESHARE_43) || defined(BOARD_40_INCH) || defined(BOARD_CYD_28) || defined(BOARD_28_INCH) || defined(BOARD_28_DUAL) || defined(BOARD_CYD_28_DUAL) || defined(BOARD_SUNTON_70)
          else if (currentTab == 5 && enableRadar) {
              lastRadarApiCall = 0; 
              drawRadarTab();        
          }
#endif
      }
  }

  // Printer data arrives from the background poll task; just apply + draw.
  // Also redraw every 2s regardless, so local sensors (BME280 / thermocouple)
  // keep updating while the printer is offline.
  if (applyPrinterSnapshot() || millis() - lastUpdate > (unsigned long)updateInterval) {
    lastUpdate = millis();
    PERF_T0(dyn)
    updateDynamicUI(); 
    PERF_LOG(dyn, "live UI update")
  }
}

void drawCornerMarker(int corner, uint16_t color) {
  int w = SCREEN_W;
  int h = SCREEN_H;
  if (corner == 0) { tft.drawFastHLine(0, 0, 15, color); tft.drawLine(0, 0, 15, 15, color); tft.drawFastVLine(0, 0, 15, color); } 
  else if (corner == 1) { tft.drawFastHLine(0, h - 1, 15, color); tft.drawLine(0, h - 1, 15, h - 16, color); tft.drawFastVLine(0, h - 16, 15, color); } 
  else if (corner == 2) { tft.drawFastHLine(w - 16, 0, 15, color); tft.drawLine(w - 16, 15, w - 1, 0, color); tft.drawFastVLine(w - 1, 0, 15, color); } 
  else if (corner == 3) { tft.drawFastHLine(w - 16, h - 1, 15, color); tft.drawLine(w - 16, h - 16, w - 1, h - 1, color); tft.drawFastVLine(w - 1, h - 16, 15, color); }
}

void touchCalibrate() {
#if defined(BOARD_CYD_28) || defined(BOARD_28_DUAL) || defined(BOARD_CYD_28_DUAL)
  const int M = CAL_MARGIN;
  const int px[4] = {M, M, SCREEN_W - 1 - M, SCREEN_W - 1 - M};   // TL, BL, TR, BR
  const int py[4] = {M, SCREEN_H - 1 - M, M, SCREEN_H - 1 - M};
  long ra[4], rb[4];
  int a, b;
  int rot = tft.getRotation();

  auto drawCross = [](int x, int y, uint16_t c) {
    tft.drawFastHLine(x - 10, y, 21, c);
    tft.drawFastVLine(x, y - 10, 21, c);
    tft.drawCircle(x, y, 4, c);
  };

  while (true) {
    tft.fillScreen(BG_COLOR);
    tft.setTextColor(TFT_WHITE, BG_COLOR);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Tap the centre of each +", SCREEN_W/2, SCREEN_H/2 - 10, 2);
    tft.setTextColor(TEXT_GRAY, BG_COLOR);
    tft.drawString("Use a stylus, hold briefly", SCREEN_W/2, SCREEN_H/2 + 12, 1);

    for (int i = 0; i < 4; i++) {
      drawCross(px[i], py[i], TFT_RED);
      int n = 0; long sa = 0, sb = 0;
      while (n < 6) {
        n = 0; sa = 0; sb = 0;
        while (!touch.readRaw(a, b)) { server.handleClient(); delay(5); }
        delay(40);                                  // let the stylus settle
        while (n < 16 && touch.readRaw(a, b)) { sa += a; sb += b; n++; delay(5); }
      }
      ra[i] = sa / n; rb[i] = sb / n;
      drawCross(px[i], py[i], TFT_GREEN);
      while (touch.readRaw(a, b)) { server.handleClient(); delay(10); }   // wait for release
      delay(300);
      drawCross(px[i], py[i], BG_COLOR);
    }

    // Which raw channel follows screen X? The one that changes most left->right.
    long dA = labs((ra[2] + ra[3]) - (ra[0] + ra[1]));
    long dB = labs((rb[2] + rb[3]) - (rb[0] + rb[1]));
    bool swap = dB > dA;
    long *sx = swap ? rb : ra;
    long *sy = swap ? ra : rb;
    touch.swapXY = swap;
    touch.x0 = (sx[0] + sx[1]) / 2;   // left
    touch.x1 = (sx[2] + sx[3]) / 2;   // right
    touch.y0 = (sy[0] + sy[2]) / 2;   // top
    touch.y1 = (sy[1] + sy[3]) / 2;   // bottom

    if (labs(touch.x1 - touch.x0) > 1000 && labs(touch.y1 - touch.y0) > 800) break;

    tft.fillScreen(BG_COLOR);
    tft.setTextColor(TFT_RED, BG_COLOR);
    tft.drawString("Calibration looked wrong - try again", SCREEN_W/2, SCREEN_H/2, 2);
    delay(1500);
  }

  preferences.putInt("tcX0", touch.x0); preferences.putInt("tcX1", touch.x1);
  preferences.putInt("tcY0", touch.y0); preferences.putInt("tcY1", touch.y1);
  preferences.putBool("tcSwap", touch.swapXY);
  preferences.putInt("tcRot", rot);
  preferences.putBool("tcV2", true);
  
  if (sdCardReady) {
    File file = SD.open("/calibration.json", FILE_WRITE);
    if (file) {
      JsonDocument doc;
      doc["v"] = 2; doc["rot"] = rot;
      doc["x0"] = touch.x0; doc["x1"] = touch.x1;
      doc["y0"] = touch.y0; doc["y1"] = touch.y1; doc["swap"] = touch.swapXY;
      serializeJson(doc, file); file.close();
    }
  }

  isCalibrated = true;
  tft.fillScreen(BG_COLOR);
  tft.setTextColor(TFT_GREEN, BG_COLOR); 
  tft.drawString("CALIBRATION SAVED!", SCREEN_W/2, SCREEN_H/2, 2);
  tft.setTextDatum(TL_DATUM);
  delay(1200);

#elif defined(BOARD_40_INCH)

  uint16_t calData[5];
  tft.fillScreen(BG_COLOR);
  tft.setTextColor(TFT_WHITE, BG_COLOR);
  tft.setTextDatum(MC_DATUM); 
  tft.drawString("Touch corners as indicated", SCREEN_W/2, SCREEN_H/2, 2);
  
  tft.calibrateTouch(calData, TFT_RED, BG_COLOR, 15);
  
  preferences.putInt("cal0", calData[0]);
  preferences.putInt("cal1", calData[1]);
  preferences.putInt("cal2", calData[2]);
  preferences.putInt("cal3", calData[3]);
  preferences.putInt("cal4", calData[4]);
  preferences.putBool("isCalibrated", true);
  
  if (sdCardReady) {
    File file = SD.open("/calibration.json", FILE_WRITE);
    if (file) {
      JsonDocument doc;
      JsonArray calArray = doc["cal"].to<JsonArray>();
      for(int i=0; i<5; i++) calArray.add(calData[i]);
      doc["is_40"] = true;
      serializeJson(doc, file); 
      file.close();
    }
  }

  tft.setTouch(calData);
  isCalibrated = true;
  tft.fillScreen(BG_COLOR);
  tft.setTextColor(TFT_GREEN, BG_COLOR); 
  tft.drawString("CALIBRATION SAVED!", SCREEN_W/2, SCREEN_H/2, 2);
  tft.setTextDatum(TL_DATUM);
  delay(1200);

#elif defined(BOARD_WAVESHARE_43) || defined(BOARD_SUNTON_70)
  isCalibrated = true;
  return;
#endif
}

void drawSidebar() {
  tft.fillRect(0, 0, PX(42), SCREEN_H, BG_COLOR);
  tft.drawFastVLine(PX(41), 0, SCREEN_H, tft.color565(50, 55, 65));
  
  const char* labels[] = {"H", "T", "M", "P", "S"};
#if defined(BOARD_WAVESHARE_43) || defined(BOARD_40_INCH) || defined(BOARD_CYD_28) || defined(BOARD_28_INCH) || defined(BOARD_28_DUAL) || defined(BOARD_CYD_28_DUAL) || defined(BOARD_SUNTON_70)
  int totalTabs = enableRadar ? 6 : 5;
#else
  int totalTabs = 5;
#endif
  int tabHeight = SCREEN_H / totalTabs;

  for (int i = 0; i < 5; i++) {
    int yPos = (i * tabHeight) + (tabHeight / 2);
    tft.setTextColor((currentTab == i) ? ACCENT_CYAN : TEXT_GRAY, BG_COLOR);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(labels[i], PX(21), yPos, 2);
  }

#if defined(BOARD_WAVESHARE_43) || defined(BOARD_40_INCH) || defined(BOARD_CYD_28) || defined(BOARD_28_INCH) || defined(BOARD_28_DUAL) || defined(BOARD_CYD_28_DUAL) || defined(BOARD_SUNTON_70)
  if (enableRadar) {
    int rYPos = (5 * tabHeight) + (tabHeight / 2);
    tft.setTextColor((currentTab == 5) ? ACCENT_CYAN : TEXT_GRAY, BG_COLOR);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("R", PX(21), rYPos, 2);
  }
#endif
}

void drawHomeTab() {
  tft.fillRect(PX(42), 0, SCREEN_W - PX(42), SCREEN_H, BG_COLOR);
  tft.setTextSize(TEXT_SCALE);

  tft.fillRoundRect(PX(48), PY(25), PX(130), PY(52), 6, CARD_COLOR);
  tft.drawRoundRect(PX(48), PY(25), PX(130), PY(52), 6, tft.color565(50, 55, 65));
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TEXT_GRAY, CARD_COLOR);
  tft.drawString("BED TEMP", PX(113), PY(32), 1);

  tft.fillRoundRect(PX(184), PY(25), PX(130), PY(52), 6, CARD_COLOR);
  tft.drawRoundRect(PX(184), PY(25), PX(130), PY(52), 6, tft.color565(50, 55, 65));
  tft.drawString("ACTIVE TOOL", PX(249), PY(32), 1);

  tft.fillRoundRect(PX(48), PY(85), PX(130), PY(52), 6, CARD_COLOR);
  tft.drawRoundRect(PX(48), PY(85), PX(130), PY(52), 6, tft.color565(50, 55, 65));
  tft.drawString("TOOL TEMP", PX(113), PY(92), 1);

  tft.fillRoundRect(PX(184), PY(85), PX(130), PY(52), 6, CARD_COLOR);
  tft.drawRoundRect(PX(184), PY(85), PX(130), PY(52), 6, tft.color565(50, 55, 65));
  tft.drawString("SPEED / FAN", PX(249), PY(92), 1);

  tft.fillRoundRect(PX(48), PY(174), PX(130), PY(52), 6, BTN_BLUE);
  tft.setTextColor(TFT_WHITE, BTN_BLUE); tft.setTextDatum(MC_DATUM);
  tft.drawString("II Pause", PX(113), PY(200), 2);

  tft.fillRoundRect(PX(184), PY(174), PX(130), PY(52), 6, BTN_RED);
  tft.setTextColor(tft.color565(255, 180, 180), BTN_RED);
  tft.drawString("Stop", PX(249), PY(200), 2);

  tft.setTextDatum(TL_DATUM); 
  updateDynamicUI();
}

void drawTempModal(int type) {
  tft.fillRect(PX(42), 0, SCREEN_W - PX(42), SCREEN_H, BG_COLOR); 
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, BG_COLOR);
  
  String title = "Set Temperature";
  if (type == 1) title = "Set Bed Temp";
  else if (type == 2) title = "Set Active Tool Temp";
  else if (type >= 10) title = "Set Tool " + String(type - 9) + " Temp";
  
  tft.drawString(title, PX(42) + (SCREEN_W-PX(42))/2, PY(20), 2);

  const int* vals = (type == 1) ? presetBed : presetTool;

  for(int i = 0; i < 6; i++) {
    int r = i / 2;
    int c = i % 2;
    int x = PX(60) + c * PX(130);
    int y = PY(45) + r * PY(60);
    
    tft.fillRoundRect(x, y, PX(110), PY(45), 6, CARD_COLOR);
    tft.drawRoundRect(x, y, PX(110), PY(45), 6, ACCENT_CYAN);
    
    tft.setTextColor(TFT_WHITE, CARD_COLOR);
    if (vals[i] == 0) tft.drawString("OFF", x + PX(55), y + PY(22), 2);
    else tft.drawString(String(vals[i]) + "C", x + PX(55), y + PY(22), 2);
  }
  
  tft.setTextColor(TEXT_GRAY, BG_COLOR);
  tft.drawString("Tap anywhere else to cancel", PX(42) + (SCREEN_W-PX(42))/2, SCREEN_H - PY(10), 1);
  tft.setTextDatum(TL_DATUM);
}

void drawSpeedFanModal() {
  tft.fillRect(PX(42), 0, SCREEN_W - PX(42), SCREEN_H, BG_COLOR); 
  tft.setTextDatum(MC_DATUM);
  
  tft.setTextColor(TEXT_GRAY, BG_COLOR);
  tft.drawString("Print Speed", PX(42) + (SCREEN_W-PX(42))/2, PY(25), 2);
  
  int speeds[] = {50, 100, 150};
  for (int c = 0; c < 3; c++) {
    int x = PX(50) + c * PX(75);
    tft.fillRoundRect(x, PY(55), PX(65), PY(45), 6, CARD_COLOR);
    tft.drawRoundRect(x, PY(55), PX(65), PY(45), 6, ACCENT_CYAN);
    tft.setTextColor(TFT_WHITE, CARD_COLOR);
    tft.drawString(String(speeds[c]) + "%", x + PX(32), PY(77), 2);
  }

  tft.setTextColor(TEXT_GRAY, BG_COLOR);
  tft.drawString("Part Fan", PX(42) + (SCREEN_W-PX(42))/2, PY(115), 2);
  
  int fans[] = {0, 50, 100};
  for (int c = 0; c < 3; c++) {
    int x = PX(50) + c * PX(75);
    tft.fillRoundRect(x, PY(125), PX(65), PY(45), 6, CARD_COLOR);
    tft.drawRoundRect(x, PY(125), PX(65), PY(45), 6, ACCENT_CYAN);
    tft.setTextColor(TFT_WHITE, CARD_COLOR);
    tft.drawString(String(fans[c]) + "%", x + PX(32), PY(147), 2);
  }

  tft.setTextColor(TEXT_GRAY, BG_COLOR);
  tft.drawString("Tap anywhere else to cancel", PX(42) + (SCREEN_W-PX(42))/2, SCREEN_H - PY(10), 1);
  tft.setTextDatum(TL_DATUM);
}

void drawCancelModal() {
  tft.fillRect(PX(42), 0, SCREEN_W - PX(42), SCREEN_H, BG_COLOR); 
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, BG_COLOR);
  tft.setTextFont(2);
  tft.drawString("CANCEL PRINT?", PX(42) + (SCREEN_W-PX(42))/2, PY(60));
  
  tft.setTextFont(1);
  tft.setTextColor(TEXT_GRAY, BG_COLOR);
  tft.drawString("Are you sure you want to stop the print?", PX(42) + (SCREEN_W-PX(42))/2, PY(100));

  tft.fillRoundRect(PX(60), PY(140), PX(100), PY(40), 6, tft.color565(50, 55, 65));
  tft.setTextColor(TFT_WHITE);
  tft.drawString("NO (Back)", PX(110), PY(160));

  tft.fillRoundRect(PX(180), PY(140), PX(100), PY(40), 6, BTN_RED);
  tft.drawString("YES (Cancel)", PX(230), PY(160));
  tft.setTextDatum(TL_DATUM);
}

void drawPauseModal() {
  tft.fillRect(PX(42), 0, SCREEN_W - PX(42), SCREEN_H, BG_COLOR); 
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, BG_COLOR);
  tft.setTextFont(2);
  tft.drawString(currentState == "paused" ? "RESUME PRINT?" : "PAUSE PRINT?", PX(42) + (SCREEN_W-PX(42))/2, PY(60));
  
  tft.setTextFont(1);
  tft.setTextColor(TEXT_GRAY, BG_COLOR);
  tft.drawString(currentState == "paused" ? "Are you sure you want to resume?" : "Are you sure you want to pause?", PX(42) + (SCREEN_W-PX(42))/2, PY(100));

  tft.fillRoundRect(PX(60), PY(140), PX(100), PY(40), 6, tft.color565(50, 55, 65));
  tft.setTextColor(TFT_WHITE);
  tft.drawString("NO (Back)", PX(110), PY(160));

  uint16_t btnColor = (currentState == "paused") ? ACCENT_CYAN : BTN_BLUE;
  tft.fillRoundRect(PX(180), PY(140), PX(100), PY(40), 6, btnColor);
  tft.setTextColor(currentState == "paused" ? BG_COLOR : TFT_WHITE, btnColor);
  tft.drawString("YES", PX(230), PY(160));
  tft.setTextDatum(TL_DATUM);
}

void drawActiveToolModal() {
  tft.fillRect(PX(42), 0, SCREEN_W - PX(42), SCREEN_H, BG_COLOR); 
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, BG_COLOR);
  tft.drawString("Select Active Tool", PX(42) + (SCREEN_W-PX(42))/2, PY(20), 2);

  for(int i = 0; i < 4; i++) {
    int r = i / 2;
    int c = i % 2;
    int x = PX(60) + c * PX(130);
    int y = PY(50) + r * PY(60);
    
    tft.fillRoundRect(x, y, PX(110), PY(45), 6, CARD_COLOR);
    tft.drawRoundRect(x, y, PX(110), PY(45), 6, ACCENT_CYAN);
    
    tft.setTextColor(TFT_WHITE, CARD_COLOR);
    tft.drawString("Tool " + String(i + 1), x + PX(55), y + PY(22), 2);
  }
  
  tft.fillRoundRect(PX(60), PY(180), PX(240), PY(40), 6, BTN_RED);
  tft.setTextColor(TFT_WHITE, BTN_RED);
  tft.drawString("None", PX(42) + (SCREEN_W-PX(42))/2, PY(200), 2); 
  
  tft.setTextDatum(TL_DATUM);
}

void drawToolModal(int idx) {
  tft.fillRect(PX(42), 0, SCREEN_W - PX(42), SCREEN_H, BG_COLOR);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, BG_COLOR);
  tft.drawString("Tool " + String(idx + 1) + " Settings", PX(42) + (SCREEN_W-PX(42))/2, PY(20), 2);

  String tempLbl[] = {"OFF", "200C", "220C", "240C"};
  for(int i=0; i<4; i++) {
    int x = PX(50) + (i * PX(65));
    tft.fillRoundRect(x, PY(40), PX(55), PY(35), 4, CARD_COLOR);
    tft.drawRoundRect(x, PY(40), PX(55), PY(35), 4, ACCENT_CYAN);
    tft.setTextColor(TFT_WHITE, CARD_COLOR);
    tft.drawString(tempLbl[i], x + PX(27), PY(40) + PY(17), 1);
  }

  tft.fillRoundRect(PX(50), PY(90), PX(120), PY(35), 4, CARD_COLOR);
  tft.drawRoundRect(PX(50), PY(90), PX(120), PY(35), 4, ACCENT_CYAN);
  tft.setTextColor(TFT_WHITE, CARD_COLOR);
  tft.drawString("Type: " + filType[idx], PX(50) + PX(60), PY(90) + PY(17), 1);

  tft.fillRoundRect(PX(180), PY(90), PX(120), PY(35), 4, CARD_COLOR);
  tft.drawRoundRect(PX(180), PY(90), PX(120), PY(35), 4, ACCENT_CYAN);
  tft.drawString("Color", PX(180) + PX(40), PY(90) + PY(17), 1);
  tft.fillCircle(PX(180) + PX(90), PY(90) + PY(17), PX(10), filColor[idx]);

  tft.fillRoundRect(PX(50), PY(140), PX(120), PY(35), 4, toolAttached[idx] ? BTN_RED : BTN_GREEN);
  tft.setTextColor(TFT_WHITE, toolAttached[idx] ? BTN_RED : BTN_GREEN);
  tft.drawString(toolAttached[idx] ? "Detach" : "Attach", PX(50) + PX(60), PY(140) + PY(17), 2);

  tft.fillRoundRect(PX(180), PY(140), PX(120), PY(35), 4, toolLoaded[idx] ? BTN_RED : BTN_GREEN);
  tft.setTextColor(TFT_WHITE, toolLoaded[idx] ? BTN_RED : BTN_GREEN);
  tft.drawString(toolLoaded[idx] ? "Unload" : "Load", PX(180) + PX(60), PY(140) + PY(17), 2);

  tft.fillRoundRect(PX(50), PY(185), PX(250), PY(35), 4, BTN_BLUE);
  tft.setTextColor(TFT_WHITE, BTN_BLUE);
  tft.drawString("Close", PX(42) + (SCREEN_W-PX(42))/2, PY(185) + PY(17), 2);

  tft.setTextDatum(TL_DATUM);
}

void drawFilamentTypeModal(int idx) {
  tft.fillRect(PX(42), 0, SCREEN_W - PX(42), SCREEN_H, BG_COLOR);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, BG_COLOR);
  tft.drawString("Select Material for Tool " + String(idx + 1), PX(42) + (SCREEN_W-PX(42))/2, PY(15), 2);

  for (int i=0; i<12; i++) {
    int r = i / 3;
    int c = i % 3;
    int bx = PX(52) + (c * PX(85));
    int by = PY(40) + (r * PY(45));

    if (i < 10) {
        tft.fillRoundRect(bx, by, PX(75), PY(35), 4, CARD_COLOR);
        tft.drawRoundRect(bx, by, PX(75), PY(35), 4, ACCENT_CYAN);
        tft.setTextColor(TFT_WHITE, CARD_COLOR);
        tft.drawString(FIL_TYPES[i], bx + PX(37), by + PY(17), 1);
    } else if (i == 10) {
        tft.fillRoundRect(bx, by, PX(75), PY(35), 4, BTN_BLUE);
        tft.setTextColor(TFT_WHITE, BTN_BLUE);
        tft.drawString("CUSTOM", bx + PX(37), by + PY(17), 1);
    } else if (i == 11) {
        tft.fillRoundRect(bx, by, PX(75), PY(35), 4, tft.color565(50, 55, 65));
        tft.setTextColor(TFT_WHITE);
        tft.drawString("CANCEL", bx + PX(37), by + PY(17), 1);
    }
  }
  tft.setTextDatum(TL_DATUM);
}

void updateKeyboardInputBox() {
  tft.fillRoundRect(PX(50), PY(35), PX(260), PY(30), 4, CARD_COLOR);
  tft.drawRoundRect(PX(50), PY(35), PX(260), PY(30), 4, ACCENT_CYAN);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(ACCENT_CYAN, CARD_COLOR);
  tft.drawString(kbInput + "_", PX(42) + (SCREEN_W-PX(42))/2, PY(50), 2);
}

void drawKeyboardModal(int idx) {
  tft.fillRect(PX(42), 0, SCREEN_W - PX(42), SCREEN_H, BG_COLOR);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, BG_COLOR);
  
  if (kbMode == 0) {
      tft.drawString("Type Filament for Tool " + String(idx + 1), PX(42) + (SCREEN_W-PX(42))/2, PY(15), 2);
  } else if (kbMode == 1) {
      tft.drawString("Enter HEX Color (RRGGBB)", PX(42) + (SCREEN_W-PX(42))/2, PY(15), 2);
  } else if (kbMode == 2) {
      tft.drawString("Enter Printer IP Address", PX(42) + (SCREEN_W-PX(42))/2, PY(15), 2);
  } else if (kbMode == 3) {
      tft.drawString("Enter Printer Name", PX(42) + (SCREEN_W-PX(42))/2, PY(15), 2);
  }

  updateKeyboardInputBox();

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE);

  if (kbMode == 2) { 
      const char* numpad[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", ".", "0", "DEL"};
      for (int i=0; i<12; i++) {
          int r = i / 3;
          int c = i % 3;
          int bx = PX(70) + c * PX(60);
          int by = PY(75) + r * PY(32);
          tft.fillRoundRect(bx, by, PX(50), PY(28), 4, tft.color565(50, 55, 65));
          tft.drawString(numpad[i], bx + PX(25), by + PY(14), 2);
      }
  } else { 
      for (int r=0; r<4; r++) {
        for (int c=0; c<10; c++) {
          char key = kbRows[r][c];
          int bx = PX(45) + (c * PX(27));
          int by = PY(75) + (r * PY(30));
          tft.fillRoundRect(bx, by, PX(25), PY(28), 4, tft.color565(50, 55, 65));
          
          if (key == '<') {
              tft.drawString("DEL", bx + PX(12), by + PY(14), 1);
          } else {
              char lbl[2] = {key, '\0'};
              tft.drawString(lbl, bx + PX(12), by + PY(14), 2);
          }
        }
      }
  }

  if (kbMode == 2) {
      tft.fillRoundRect(PX(48), PY(205), PX(100), PY(30), 4, BTN_RED);
      tft.drawString("CANCEL", PX(48)+PX(50), PY(205)+PY(15), 2);
      tft.fillRoundRect(PX(172), PY(205), PX(100), PY(30), 4, BTN_BLUE);
      tft.drawString("SAVE", PX(172)+PX(50), PY(205)+PY(15), 2);
  } else {
      tft.fillRoundRect(PX(48), PY(200), PX(70), PY(30), 4, BTN_RED);
      tft.drawString("CANCEL", PX(48)+PX(35), PY(200)+PY(15), 1);

      if (kbMode == 0 || kbMode == 3) {
        tft.fillRoundRect(PX(128), PY(200), PX(100), PY(30), 4, tft.color565(50, 55, 65));
        tft.drawString("SPACE", PX(128)+PX(50), PY(200)+PY(15), 1);
      }

      tft.fillRoundRect(PX(238), PY(200), PX(70), PY(30), 4, BTN_BLUE);
      tft.drawString("SAVE", PX(238)+PX(35), PY(200)+PY(15), 2);
  }
  
  tft.setTextDatum(TL_DATUM);
}

void drawColorPickerModal(int idx) {
  tft.fillRect(PX(42), 0, SCREEN_W - PX(42), SCREEN_H, BG_COLOR);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, BG_COLOR);
  tft.drawString("Select Color for Tool " + String(idx + 1), PX(42) + (SCREEN_W-PX(42))/2, PY(15), 2);

  for (int i=0; i<10; i++) {
    int r = i / 5;
    int c = i % 5;
    int bx = PX(50) + (c * PX(50));
    int by = PY(45) + (r * PY(55));
    
    tft.fillRoundRect(bx, by, PX(40), PY(45), 6, FIL_COLORS[i]);
    if (filColor[idx] == FIL_COLORS[i]) {
        tft.drawRoundRect(bx-PX(2), by-PY(2), PX(44), PY(49), 6, ACCENT_CYAN);
        tft.drawRoundRect(bx-PX(1), by-PY(1), PX(42), PY(47), 6, ACCENT_CYAN);
    }
  }

  tft.fillRoundRect(PX(50), PY(160), PX(260), PY(32), 4, tft.color565(40, 60, 90));
  tft.setTextColor(ACCENT_CYAN);
  tft.drawString("Custom HEX...", PX(42) + (SCREEN_W-PX(42))/2, PY(160) + PY(16), 2);

  tft.fillRoundRect(PX(50), PY(200), PX(260), PY(32), 4, tft.color565(50, 55, 65));
  tft.setTextColor(TFT_WHITE);
  tft.drawString("Cancel", PX(42) + (SCREEN_W-PX(42))/2, PY(200) + PY(16), 2);
  
  tft.setTextDatum(TL_DATUM);
}

void drawPrintOptionsModal() {
  tft.fillRect(PX(42), 0, SCREEN_W - PX(42), SCREEN_H, BG_COLOR);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, BG_COLOR);
  String headerStr = "Launch: " + selectedFile;
  if(headerStr.length() > 28) headerStr = headerStr.substring(0, 25) + "...";
  tft.drawString(headerStr, PX(42) + (SCREEN_W-PX(42))/2, PY(20), 2);

  tft.setTextDatum(TL_DATUM);

  tft.drawRect(PX(50), PY(45), PX(20), PY(20), TEXT_GRAY);
  if(optBedLevel) tft.fillRect(PX(53), PY(48), PX(14), PY(14), ACCENT_CYAN);
  tft.drawString("Auto Bed Leveling", PX(80), PY(48), 2);

  tft.drawRect(PX(50), PY(75), PX(20), PY(20), TEXT_GRAY);
  if(optTimelapse) tft.fillRect(PX(53), PY(78), PX(14), PY(14), ACCENT_CYAN);
  tft.drawString("Record Timelapse", PX(80), PY(78), 2);

  tft.drawRect(PX(50), PY(105), PX(20), PY(20), TEXT_GRAY);
  if(optAdvMap) tft.fillRect(PX(53), PY(108), PX(14), PY(14), ACCENT_CYAN);
  tft.drawString("Adv Tool Mapping", PX(80), PY(108), 2);

  tft.drawString("Map Main Extruder (T0) To:", PX(50), PY(135), 1);
  for(int i=0; i<4; i++) {
     int bx = PX(50) + (i*PX(55));
     tft.fillRoundRect(bx, PY(145), PX(50), PY(45), 4, optT0 == i ? ACCENT_CYAN : tft.color565(50,55,65));
     tft.setTextColor(optT0 == i ? BG_COLOR : TFT_WHITE);
     tft.setTextDatum(MC_DATUM);
     tft.drawString("T"+String(i+1), bx+PX(25), PY(155), 2);
     
     String fType = filType[i];
     if(fType.length() > 6) fType = fType.substring(0, 6);
     tft.drawString(fType, bx+PX(25), PY(170), 1);
     tft.fillCircle(bx+PX(25), PY(180), PX(4), filColor[i]);
  }

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE);
  tft.fillRoundRect(PX(50), PY(195), PX(90), PY(35), 4, BTN_RED);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("CANCEL", PX(95), PY(212), 1);

  tft.fillRoundRect(PX(150), PY(195), PX(150), PY(35), 4, BTN_GREEN);
  tft.drawString("LAUNCH PRINT", PX(225), PY(212), 2);

  tft.setTextDatum(TL_DATUM);
}

void drawToolheadTab() {
  tft.fillRect(PX(42), 0, SCREEN_W - PX(42), SCREEN_H, BG_COLOR);
  tft.setTextSize(TEXT_SCALE);
  tft.setTextDatum(TL_DATUM);
  
  for (int i = 0; i < 4; i++) {
    int bx = PX(45) + (i * PX(69)); 
    tft.fillRoundRect(bx, PY(40), PX(64), PY(160), 6, CARD_COLOR);
  }
  updateDynamicUI();
}

void drawMoveTab() {
  tft.fillRect(PX(42), 0, SCREEN_W - PX(42), SCREEN_H, BG_COLOR);
  tft.setTextSize(TEXT_SCALE); tft.setTextColor(TFT_WHITE, BG_COLOR);
  tft.setCursor(PX(55), PY(5)); tft.print("Move & Home");

  String distLabels[] = {"0.1", "1", "10", "50"};
  for (int i=0; i<4; i++) {
    int x = PX(55) + (i * PX(60));
    bool active = false;
    if (i == 0 && moveDist == 0.1) active = true;
    if (i == 1 && moveDist == 1.0) active = true;
    if (i == 2 && moveDist == 10.0) active = true;
    if (i == 3 && moveDist == 50.0) active = true;

    tft.fillRoundRect(x, PY(25), PX(50), PY(30), 4, active ? ACCENT_CYAN : CARD_COLOR);
    tft.setTextColor(active ? BG_COLOR : TFT_WHITE, active ? ACCENT_CYAN : CARD_COLOR);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(distLabels[i], x + PX(25), PY(40), 2);
  }

  tft.setTextColor(TFT_WHITE, CARD_COLOR);
  tft.fillRoundRect(PX(120), PY(130), PX(40), PY(40), 6, CARD_COLOR); tft.drawString("H",  PX(140), PY(150), 2);
  tft.fillRoundRect(PX(120), PY(80),  PX(40), PY(40), 6, CARD_COLOR); tft.drawString("Y+", PX(140), PY(100), 2);
  tft.fillRoundRect(PX(120), PY(180), PX(40), PY(40), 6, CARD_COLOR); tft.drawString("Y-", PX(140), PY(200), 2);
  tft.fillRoundRect(PX(70),  PY(130), PX(40), PY(40), 6, CARD_COLOR); tft.drawString("X-", PX(90),  PY(150), 2);
  tft.fillRoundRect(PX(170), PY(130), PX(40), PY(40), 6, CARD_COLOR); tft.drawString("X+", PX(190), PY(150), 2);
  
  tft.fillRoundRect(PX(235), PY(80),  PX(50), PY(40), 6, CARD_COLOR); tft.drawString("Z+", PX(260), PY(100), 2);
  tft.fillRoundRect(PX(235), PY(130), PX(50), PY(40), 6, CARD_COLOR); tft.drawString("HZ", PX(260), PY(150), 2); 
  tft.fillRoundRect(PX(235), PY(180), PX(50), PY(40), 6, CARD_COLOR); tft.drawString("Z-", PX(260), PY(200), 2);
  tft.setTextDatum(TL_DATUM);
  
  updateDynamicUI(); 
}

void drawPrintTab() {
  tft.fillRect(PX(42), 0, SCREEN_W - PX(42), SCREEN_H, BG_COLOR);
  tft.setTextDatum(TL_DATUM); 
  tft.setTextSize(TEXT_SCALE);
  tft.setTextColor(TFT_WHITE, BG_COLOR);

  int btnW = PX(85);
  
  tft.fillRoundRect(PX(50), PY(5), btnW, PY(26), 4, printTabSource == 0 ? ACCENT_CYAN : CARD_COLOR);
  tft.setTextColor(printTabSource == 0 ? BG_COLOR : TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Printer", PX(50) + btnW/2, PY(18), 1);

  tft.fillRoundRect(PX(145), PY(5), btnW, PY(26), 4, printTabSource == 1 ? ACCENT_CYAN : CARD_COLOR);
  tft.setTextColor(printTabSource == 1 ? BG_COLOR : TFT_WHITE);
  tft.drawString("SD Card", PX(145) + btnW/2, PY(18), 1);
  
  tft.setTextDatum(TL_DATUM);

  int count = printTabSource == 0 ? klipperFileCount : fileCount;
  String* list = printTabSource == 0 ? klipperFileList : fileList;

  if (count == 0) {
    tft.setTextColor(TEXT_GRAY);
    tft.drawString("No files found.", PX(55), PY(50), 1);
    return;
  }

  for (int i=0; i<4; i++) {
     int idx = fileScrollIndex + i;
     if (idx >= count) break;
     int by = PY(45) + (i * PY(45));
     tft.fillRoundRect(PX(50), by, PX(200), PY(40), 4, CARD_COLOR);
     tft.setTextColor(TFT_WHITE, CARD_COLOR);
     String fn = list[idx];
     if(fn.length() > 28) fn = fn.substring(0, 25) + "...";
     tft.drawString(fn, PX(55), by + PY(12), 1);
  }

  tft.fillRoundRect(PX(260), PY(45), PX(45), PY(85), 4, tft.color565(50, 55, 65));
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("UP", PX(282), PY(87), 2);
  
  tft.fillRoundRect(PX(260), PY(135), PX(45), PY(85), 4, tft.color565(50, 55, 65));
  tft.drawString("DN", PX(282), PY(177), 2);
  tft.setTextDatum(TL_DATUM);
  
  updateDynamicUI(); 
}

void drawSettingsTab() {
  tft.fillRect(PX(42), 0, SCREEN_W - PX(42), SCREEN_H, BG_COLOR);
  
  tft.setTextFont(1); 
  tft.setTextSize(TEXT_SCALE);
  
  tft.setTextColor(TFT_WHITE, BG_COLOR);
  tft.drawString("SYS_CONFIG", PX(55), PY(6), 2);

  tft.setTextColor(TEXT_GRAY, BG_COLOR);
  
  tft.setCursor(PX(55), PY(25));  tft.print("Printer IP: " + String(printerIP));
  tft.setCursor(PX(55), PY(38));  tft.print("WiFi: " + WiFi.SSID() + " (" + String(WiFi.RSSI()) + "dBm)");
  tft.setCursor(PX(55), PY(51));  tft.print("Display IP: " + WiFi.localIP().toString());
  tft.setCursor(PX(55), PY(64));  tft.print("URL: http://" + String(mdnsName) + ".local");
  tft.setCursor(PX(55), PY(77));  tft.print("MAC: " + WiFi.macAddress());

  if (sdCardReady) {
    tft.setTextColor(TFT_GREEN, BG_COLOR);
    tft.setCursor(PX(55), PY(90)); tft.print("SD Card: " + String((unsigned long)sdCardTotal) + " MB Total");
  } else {
    tft.setTextColor(TFT_RED, BG_COLOR);
    tft.setCursor(PX(55), PY(90)); tft.print("SD Card: Not Mounted");
  }

  tft.fillRoundRect(PX(48), PY(130), PX(130), PY(38), 6, CARD_COLOR);
  tft.setTextColor(TFT_WHITE, CARD_COLOR); 
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Edit IP", PX(113), PY(149), 2);

  tft.fillRoundRect(PX(184), PY(130), PX(130), PY(38), 6, CARD_COLOR);
  tft.drawString("Edit Name", PX(249), PY(149), 2);

  tft.fillRoundRect(PX(48), PY(175), PX(130), PY(38), 6, BTN_BLUE); 
  tft.setTextColor(TFT_WHITE, BTN_BLUE); 
  tft.drawString("Calibrate", PX(113), PY(194), 2);

  tft.fillRoundRect(PX(184), PY(175), PX(130), PY(38), 6, CARD_COLOR); 
  tft.setTextColor(ACCENT_CYAN, CARD_COLOR);
  tft.drawString("Reset WiFi", PX(249), PY(194), 2);
  
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(ACCENT_CYAN, BG_COLOR);
  tft.drawString(String(FW_VERSION) + " Firmware by Nates Print Shop", PX(48), PY(225), 1);
  
  updateDynamicUI(); 
}


void updateDynamicUI() {
  if (activeModal > 0) return; 

  tft.setTextFont(1);
  tft.setTextSize(TEXT_SCALE);
  tft.setTextPadding(0);

  if (currentTab == 0) {
    tft.fillRect(PX(48), PY(2), PX(266), PY(21), BG_COLOR); 
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_WHITE, BG_COLOR);
    String headerStr = String(printerName) + " : " + currentState;
    if (headerStr.length() > 25) headerStr = headerStr.substring(0, 23) + "..";
    tft.drawString(headerStr, PX(48), PY(3), 2);

    tft.setTextDatum(MC_DATUM);

    tft.fillRect(PX(50), PY(47), PX(126), PY(28), CARD_COLOR);
    tft.setTextColor(TFT_WHITE, CARD_COLOR);
    tft.drawString(String(bedTemp, 1) + "C", PX(113), PY(60), 2);
    
    tft.fillRect(PX(186), PY(47), PX(126), PY(28), CARD_COLOR);
    tft.setTextColor(ACCENT_CYAN, CARD_COLOR);
    if (activeTool >= 0 && activeTool <= 3) {
        tft.drawString("Tool " + String(activeTool + 1), PX(249), PY(60), 2);
    } else {
        tft.setTextColor(TEXT_GRAY, CARD_COLOR);
        tft.drawString("None", PX(249), PY(60), 2);
    }
    
    tft.fillRect(PX(50), PY(107), PX(126), PY(28), CARD_COLOR);
    if (activeTool >= 0 && activeTool <= 3) {
        tft.setTextColor(toolTarget[activeTool] > 0 ? tft.color565(255, 120, 120) : TFT_WHITE, CARD_COLOR);
        tft.drawString(String(toolTemp[activeTool], 1) + "C", PX(113), PY(120), 2);
    } else {
        tft.setTextColor(TEXT_GRAY, CARD_COLOR);
        tft.drawString("-- C", PX(113), PY(120), 2);
    }
    
    tft.fillRect(PX(186), PY(107), PX(126), PY(28), CARD_COLOR);
    tft.setTextColor(TFT_WHITE, CARD_COLOR);
    tft.drawString(String(printSpeed) + "% / " + String(fanSpeed) + "%", PX(249), PY(120), 2);

    tft.fillRect(PX(48), PY(142), PX(266), PY(26), BG_COLOR);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TEXT_GRAY, BG_COLOR);
    String shortFile = currentFile;
    int lastSlash = shortFile.lastIndexOf('/');
    if (lastSlash >= 0) shortFile = shortFile.substring(lastSlash + 1);
    if (shortFile.length() > 20) shortFile = shortFile.substring(0, 17) + "...";
    tft.drawString(shortFile, PX(48), PY(142), 1);
    
    tft.setTextDatum(TR_DATUM);
    // Show time LEFT while a job is running/paused; fall back to elapsed otherwise.
    bool jobActive = (currentState == "printing" || currentState == "paused");
    int remaining = -1;
    if (jobActive) {
      if (slicerEstimate > 0) {
        // Slicer estimate is the most stable source early in a print.
        remaining = slicerEstimate - printDuration;
        if (remaining < 0) remaining = 0;
      } else if (printProgress > 1.0) {
        // Extrapolate from progress (percent) when the file has no estimate.
        float frac = printProgress / 100.0f;
        remaining = (int)(printDuration * (1.0f - frac) / frac);
      }
    }
    String timeStr;
    if (remaining >= 0) {
      timeStr = String(remaining / 3600) + "h " + String((remaining % 3600) / 60) + "m left";
    } else if (jobActive) {
      timeStr = "Calculating...";
    } else {
      timeStr = String(printDuration / 3600) + "h " + String((printDuration % 3600) / 60) + "m";
    }
    tft.drawString(timeStr, PX(314), PY(142), 1);

    int barWidth = PX(266);
    int fillWidth = (int)((printProgress / 100.0) * barWidth);
    fillWidth = constrain(fillWidth, 0, barWidth);
    
    tft.drawRect(PX(48), PY(156), barWidth, PY(10), tft.color565(50, 55, 65)); 
    if(fillWidth > 0) tft.fillRect(PX(49), PY(157), fillWidth, PY(8), ACCENT_CYAN); 

    tft.setTextDatum(TL_DATUM); 
  } 
  else if (currentTab == 1) {
    tft.fillRect(PX(48), PY(2), PX(266), PY(21), BG_COLOR); 
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_WHITE, BG_COLOR);
    tft.drawString(String(printerName) + " : " + currentState, PX(48), PY(3), 2);

    tft.setTextDatum(MC_DATUM);

    for (int i = 0; i < 4; i++) {
      int bx = PX(45) + (i * PX(69));
      
      tft.drawRoundRect(bx, PY(40), PX(64), PY(160), 6, (activeTool == i) ? ACCENT_CYAN : tft.color565(50, 55, 65));
      
      tft.setTextColor(TEXT_GRAY, CARD_COLOR);
      tft.drawString(String(i + 1), bx + PX(32), PY(40) + PY(15), 1);

      tft.fillCircle(bx + PX(32), PY(40) + PY(60), PX(15), filColor[i]);

      tft.fillRect(bx + PX(2), PY(40) + PY(90), PX(60), PY(20), CARD_COLOR);
      tft.setTextColor(TFT_WHITE, CARD_COLOR);
      tft.drawString(filType[i], bx + PX(32), PY(40) + PY(100), 1);

      tft.fillRect(bx + PX(2), PY(40) + PY(125), PX(60), PY(25), CARD_COLOR);
      tft.setTextColor(toolTarget[i] > 0 ? tft.color565(255, 120, 120) : TEXT_GRAY, CARD_COLOR);
      tft.drawString(String(toolTemp[i], 1) + "C", bx + PX(32), PY(40) + PY(135), 1);
    }
    tft.setTextDatum(TL_DATUM);
  }
  else if (currentTab == 4) {
#if defined(BOARD_40_INCH) || defined(BOARD_WAVESHARE_43) || defined(BOARD_SUNTON_70)
    if (envMode > 0 || tcMode > 0) {
      tft.fillRect(PX(52), PY(102), PX(260), PY(24), BG_COLOR); 
      tft.setTextDatum(TL_DATUM);
      int lineY = PY(104);

      // Render BME280 on Settings Screen if enabled
      if (envMode > 0) {
        if (bmeReady) {
          float tempC = bme.readTemperature();
          if (!isnan(tempC) && tempC < 100.0 && tempC > -40.0) {
            float tempV = useFahrenheit ? (tempC * 9.0 / 5.0) + 32.0 : tempC;
            tempV += tempOffset; 
            String unitV = useFahrenheit ? "F" : "C";
            float hum = bme.readHumidity() + humOffset;

            String printStatus = "Optimal";
            if (hum > 55.0) printStatus = "High Humid!";
            else if (tempC < 15.0) printStatus = "Too Cold!";
            else if (tempC > 35.0) printStatus = "Too Hot!";

            tft.setTextColor(TEXT_GRAY, BG_COLOR);
            tft.drawString("Env: " + String(tempV, 1) + "&deg;" + unitV + ", " + String(hum, 0) + "% RH (" + printStatus + ")", PX(55), lineY, 1);
            lineY += PY(12);
          }
        } else {
          tft.setTextColor(tft.color565(255, 180, 180), BG_COLOR);
          tft.drawString("Env Sensor: Scanning...", PX(55), lineY, 1);
          lineY += PY(12);
        }
      }

      // Render MAX31855 Thermocouple on Settings Screen if enabled
      if (tcMode > 0) {
        if (max31855Ready) {
          double tcC = thermocouple.readCelsius();
          if (!isnan(tcC)) {
            float tcVal = useFahrenheit ? (tcC * 9.0 / 5.0) + 32.0 : tcC;
            String tcUnit = useFahrenheit ? "F" : "C";
            tft.setTextColor(ACCENT_CYAN, BG_COLOR);
            tft.drawString("TC: " + String(tcVal, 1) + "&deg;" + tcUnit, PX(55), lineY, 1);
          } else {
            tft.setTextColor(tft.color565(255, 180, 180), BG_COLOR);
            tft.drawString("TC: Error / Fault", PX(55), lineY, 1);
          }
        } else {
          tft.setTextColor(TEXT_GRAY, BG_COLOR);
          tft.drawString("TC: Not Detected", PX(55), lineY, 1);
        }
      }
    }
#endif
  }

  // Draw battery on non-radar tabs
  if (showBattery && currentTab != 5) {
      int pct = getBatteryPercentage(getBatteryVoltage());
      tft.setTextDatum(TR_DATUM);
      tft.setTextColor(TFT_GREEN, BG_COLOR);
      tft.setTextPadding(PX(45)); 
      tft.drawString(String(pct) + "%", PX(314), PY(3), 2);
      tft.setTextPadding(0);
  }
  
#if defined(BOARD_40_INCH) || defined(BOARD_WAVESHARE_43) || defined(BOARD_SUNTON_70)
  // Draw environment readings on non-radar tabs (Header Overlay)
  if (envMode == 2 && bmeReady && currentTab != 5) {
      float tempC = bme.readTemperature();
      if (!isnan(tempC) && tempC < 100.0 && tempC > -40.0) {
          float tempVal = useFahrenheit ? (tempC * 9.0 / 5.0) + 32.0 : tempC;
          tempVal += tempOffset; 
          String unitStr = useFahrenheit ? "F" : "C";
          float hum = bme.readHumidity() + humOffset; 
          
          tft.setTextDatum(TR_DATUM);
          tft.setTextColor(tft.color565(255, 200, 50), BG_COLOR); 
          tft.setTextPadding(PX(100)); 
          
          int envY = showBattery ? PY(15) : PY(3);
          int envSize = showBattery ? 1 : 2; 
          tft.drawString(String(tempVal, 1) + unitStr + "  " + String(hum, 0) + "% RH", PX(314), envY, envSize);
          tft.setTextPadding(0);
      }
  }

  // Draw MAX31855 Thermocouple reading on non-radar tabs (Header Overlay)
  if (tcMode == 2 && max31855Ready && currentTab != 5) {
    double tcC = thermocouple.readCelsius();
    if (!isnan(tcC)) {
      float tcVal = useFahrenheit ? (tcC * 9.0 / 5.0) + 32.0 : tcC;
      String tcUnit = useFahrenheit ? "F" : "C";
      
      // Calculate dynamic Y position to prevent overlap with Battery / BME280
      int tcY = PY(3);
      if (showBattery) tcY += PY(12);
      if (envMode == 2 && bmeReady) tcY += PY(12);

      tft.setTextDatum(TR_DATUM);
      tft.setTextColor(ACCENT_CYAN, BG_COLOR);
      tft.setTextPadding(PX(80));
      tft.drawString("TC: " + String(tcVal, 1) + tcUnit, PX(314), tcY, 1);
      tft.setTextPadding(0);
      tft.setTextDatum(TL_DATUM);
    }
  }
#endif

}


// =====================================================================
// Background printer polling
// ---------------------------------------------------------------------
// The HTTP request + JSON parse used to run on the UI loop, freezing touch
// for every poll (~0.1-0.3s normally, 0.8s+ when the printer is off). It now
// runs in its own FreeRTOS task on core 0 and hands a finished snapshot to
// the UI loop, which only copies it (a few ms) and redraws.
// =====================================================================
struct PrinterSnapshot {
  bool ok = false;          // this poll succeeded
  bool offline = false;     // 3+ consecutive failures
  String state, file;
  float bedTemp = 0, bedTarget = 0;
  float toolTemp[4] = {0, 0, 0, 0}, toolTarget[4] = {0, 0, 0, 0};
  int speed = 100, fan = 0;
  float progress = 0;
  int duration = 0;
  int activeTool = -1;
  bool hasFilType[4] = {false, false, false, false};
  bool hasFilColor[4] = {false, false, false, false};
  String filTypeV[4];
  uint16_t filColorV[4] = {0, 0, 0, 0};
  int slicerEstimate = 0;
};

static PrinterSnapshot pollSnap;              // written by the poll task
static SemaphoreHandle_t pollMutex = NULL;
static volatile bool pollReady = false;

static bool queryPrinter(const char *ip, PrinterSnapshot &s) {
  HTTPClient http;
  http.setConnectTimeout(800);
  http.setTimeout(1500);
  http.begin("http://" + String(ip) + ":7125/printer/objects/query?print_stats&toolhead&extruder&extruder1&extruder2&extruder3&heater_bed&fan&gcode_move&display_status&print_task_config");
  int httpCode = http.GET();
  if (httpCode != 200) { http.end(); return false; }

  String payload = http.getString();
  http.end();
  JsonDocument doc;
  if (deserializeJson(doc, payload)) return false;

  JsonObject status = doc["result"]["status"];
  s.state = status["print_stats"]["state"].as<String>();
  s.file = status["print_stats"]["filename"].as<String>();
  if (s.file == "null" || s.file == "") s.file = "Idle";

  s.bedTemp = status["heater_bed"]["temperature"].as<float>();
  s.bedTarget = status["heater_bed"]["target"].as<float>();
  const char *ext[4] = {"extruder", "extruder1", "extruder2", "extruder3"};
  for (int i = 0; i < 4; i++) {
    s.toolTemp[i] = status[ext[i]]["temperature"].as<float>();
    s.toolTarget[i] = status[ext[i]]["target"].as<float>();
  }
  s.speed = status["gcode_move"]["speed_factor"].as<float>() * 100.0;
  s.fan = status["fan"]["speed"].as<float>() * 100.0;
  s.progress = status["display_status"]["progress"].as<float>() * 100.0;
  s.duration = status["print_stats"]["print_duration"].as<int>();

  String tHead = status["toolhead"]["extruder"].as<String>();
  s.activeTool = -1;
  for (int i = 0; i < 4; i++) if (tHead == ext[i]) s.activeTool = i;

  JsonObject taskConfig = status["print_task_config"];
  if (!taskConfig.isNull()) {
    JsonArray types = taskConfig["filament_type"];
    JsonArray colors = taskConfig["filament_color_rgba"];
    if (!types.isNull() && !colors.isNull()) {
      for (int i = 0; i < 4; i++) {
        if (!types[i].isNull()) {
          String fType = types[i].as<String>();
          if (fType != "null" && fType.length() > 0) { s.filTypeV[i] = fType; s.hasFilType[i] = true; }
        }
        if (!colors[i].isNull()) {
          String fColor = colors[i].as<String>();
          if (fColor != "null" && fColor.length() >= 6) { s.filColorV[i] = hexToRGB565(fColor.substring(0, 6)); s.hasFilColor[i] = true; }
        }
      }
    }
  }
  return true;
}

// Slicer's estimated print time for a file (0 if unknown).
static int queryEstimate(const char *ip, const String &file) {
  int est = 0;
  HTTPClient meta;
  meta.setConnectTimeout(800);
  meta.setTimeout(1500);
  meta.begin("http://" + String(ip) + ":7125/server/files/metadata?filename=" + urlEncode(file));
  if (meta.GET() == 200) {
    JsonDocument filter;
    filter["result"]["estimated_time"] = true;
    JsonDocument mdoc;
    if (!deserializeJson(mdoc, meta.getString(), DeserializationOption::Filter(filter))) {
      est = mdoc["result"]["estimated_time"] | 0;
    }
  }
  meta.end();
  return est;
}

void printerPollTask(void *param) {
  String estFile = "";
  int est = 0;
  int fails = 0;
  bool offline = false;
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      char ip[32];
      strlcpy(ip, printerIP, sizeof(ip));   // printerIP only changes on user edits
      PERF_T0(poll)
      PrinterSnapshot s;
      s.ok = queryPrinter(ip, s);
      if (s.ok) {
        fails = 0; offline = false;
        if (s.file != estFile) {             // look up the slicer estimate once per job
          estFile = s.file;
          est = (s.file != "Idle") ? queryEstimate(ip, s.file) : 0;
        }
        s.slicerEstimate = est;
      } else if (++fails >= 3) {
        offline = true;
      }
      s.offline = offline;
      PERF_LOG(poll, s.ok ? "poll (bg) ok" : "poll (bg) fail")
      xSemaphoreTake(pollMutex, portMAX_DELAY);
      pollSnap = s;
      pollReady = true;
      xSemaphoreGive(pollMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(offline ? offlineRetryInterval : updateInterval));
  }
}

void startPrinterPolling() {
  pollMutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(printerPollTask, "PrinterPoll", 12288, NULL, 1, NULL, 0);
}

// Runs on the UI loop: copy the newest snapshot into the display globals.
// Returns true when there was new data to draw.
bool applyPrinterSnapshot() {
  if (!pollReady || pollMutex == NULL) return false;
  PrinterSnapshot s;
  xSemaphoreTake(pollMutex, portMAX_DELAY);
  s = pollSnap;
  pollReady = false;
  xSemaphoreGive(pollMutex);

  if (!s.ok) {
    if (s.offline && !printerOffline) { printerOffline = true; currentState = "Offline"; return true; }
    return false;                           // transient failure: keep showing last data
  }
  printerOffline = false;
  currentState = s.state;
  currentFile = s.file;
  bedTemp = s.bedTemp; bedTarget = s.bedTarget;
  for (int i = 0; i < 4; i++) { toolTemp[i] = s.toolTemp[i]; toolTarget[i] = s.toolTarget[i]; }
  printSpeed = s.speed; fanSpeed = s.fan;
  printProgress = s.progress;
  printDuration = s.duration;
  activeTool = s.activeTool;
  slicerEstimate = s.slicerEstimate;
  estimateForFile = s.file;
  // Don't let a poll that started before a local filament edit overwrite it.
  if (millis() > blockSyncUntil) {
    for (int i = 0; i < 4; i++) {
      if (s.hasFilType[i]) filType[i] = s.filTypeV[i];
      if (s.hasFilColor[i]) filColor[i] = s.filColorV[i];
    }
  }
  return true;
}

void sendGcode(String command) {
  if (printerOffline) { Serial.println("sendGcode skipped: printer offline"); return; }
  HTTPClient http;
  http.setConnectTimeout(800);
  http.setTimeout(2000);
  http.begin("http://" + String(printerIP) + ":7125/printer/gcode/script");
  http.addHeader("Content-Type", "application/json");
  
  String json = "{\"script\": \"";
  for (size_t i = 0; i < command.length(); i++) {
    if (command[i] == '"') json += "\\\"";
    else if (command[i] == '\\') json += "\\\\";
    else if (command[i] == '\n') json += "\\n";
    else json += command[i];
  }
  json += "\"}";
  
  http.POST(json);
  http.end();
}

String getWebHeader(String activePage) {
  String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
  if (activePage == "home") { html += "<meta http-equiv='refresh' content='5'>"; }
  
  html += "<style>";
  html += "body { background:#0f1115; color:white; font-family:sans-serif; margin:0; padding:20px; } ";
  html += ".container { max-width: 600px; margin: auto; } ";
  html += ".nav { display: flex; gap: 10px; margin-bottom: 20px; } ";
  html += ".nav a { flex: 1; text-align: center; padding: 12px; background: #161B22; color: #888; text-decoration: none; border-radius: 8px; font-weight: bold; border: 1px solid #333; } ";
  html += ".nav a.active { background: #2A3B5C; color: #00E5FF; border-color: #00E5FF; } ";
  html += ".card { background: #161B22; padding: 20px; border-radius: 12px; border: 1px solid #333; margin-bottom: 20px; text-align: left; } ";
  html += ".grid { display: grid; grid-template-columns: 1fr 1fr; gap: 15px; } ";
  html += ".stat { background: #1a1d24; padding: 15px; border-radius: 8px; border: 1px solid #2a2d36; text-align: center; transition: 0.2s; } ";
  html += ".stat h4 { margin: 0; font-size: 12px; color: #888; text-transform: uppercase; } ";
  html += ".stat div { font-size: 24px; font-weight: bold; margin-top: 8px; color: #00E5FF; font-family: 'Courier New', Courier, monospace; } ";
  html += ".stat.clickable:hover { border-color: #00E5FF; background: #2A3B5C; cursor: pointer; } ";
  html += "input[type=text], input[type=password], input[type=number], select { width: 100%; padding: 12px; margin: 8px 0 16px; box-sizing: border-box; background: #1a1d24; border: 1px solid #333; color: white; border-radius: 6px; } ";
  html += ".btn { width: 100%; padding: 14px; background: #00E5FF; color: black; border: none; border-radius: 6px; font-weight: bold; font-size: 16px; cursor: display: block; box-sizing: border-box; text-align: center; text-decoration: none; } ";
  html += "</style>";
  html += "<script>";
  html += "function sc(cmd, pmt) { let v = prompt(pmt); if(v !== null && v !== '') { document.getElementById('c_param').name = cmd; document.getElementById('c_param').value = v; document.getElementById('c_form').submit(); } }";
  html += "function toggleCustom() { var sel = document.getElementById('themeSelect'); var div = document.getElementById('customColors'); if(sel && div) { div.style.display = (sel.value == '4') ? 'block' : 'none'; } }";
  html += "</script>";
  html += "</head><body><div class='container'>";
  
  html += "<h2 style='text-align:center; color:#00E5FF; text-transform:uppercase; letter-spacing:2px; font-family:monospace; margin-bottom:25px;'>U1 Command Terminal</h2><div class='nav'>";
  html += "<a href='/' class='" + String(activePage == "home" ? "active" : "") + "'>Dashboard</a>";
  
  if (enableAudio) {
      html += "<a href='/audio' class='" + String(activePage == "audio" ? "active" : "") + "'>Audio Player</a>";
  }
  
  html += "<a href='/tech' class='" + String(activePage == "tech" ? "active" : "") + "'>Hardware & Config</a></div>";
  return html;
}

void handleRoot() {
  String html = getWebHeader("home");
  
  html += "<form id='c_form' action='/control' method='POST' style='display:none;'><input type='hidden' id='c_param' name='' value=''></form>";
  
  html += "<div class='card'><h3 style='margin-top:0;'>Live Status</h3>";
  html += "<p style='color:#888; margin-bottom:20px;'><b>Target:</b> " + String(printerName) + " &nbsp;|&nbsp; <b>State:</b> <span style='color:white;'>" + currentState + "</span></p>";
  
  html += "<div class='grid'>";
  html += "<div class='stat clickable' onclick='sc(\"bed\", \"Set Bed Temp (C):\")'><h4>Bed Temp</h4><div>" + String(bedTemp, 1) + "&deg;C</div></div>";
  html += "<div class='stat clickable' style='border-color:#00E5FF;' onclick='sc(\"tool_active\", \"Set Active Tool (0-3):\")'><h4>Active Tool</h4><div>Tool " + String(activeTool + 1) + "</div></div>";
  html += "<div class='stat clickable' onclick='sc(\"t0\", \"Set Tool 1 Temp (C):\")'><h4>Tool 1</h4><div>" + String(toolTemp[0], 1) + "&deg;C</div></div>";
  html += "<div class='stat clickable' onclick='sc(\"t1\", \"Set Tool 2 Temp (C):\")'><h4>Tool 2</h4><div>" + String(toolTemp[1], 1) + "&deg;C</div></div>";
  html += "<div class='stat clickable' onclick='sc(\"t2\", \"Set Tool 3 Temp (C):\")'><h4>Tool 3</h4><div>" + String(toolTemp[2], 1) + "&deg;C</div></div>";
  html += "<div class='stat clickable' onclick='sc(\"t3\", \"Set Tool 4 Temp (C):\")'><h4>Tool 4</h4><div>" + String(toolTemp[3], 1) + "&deg;C</div></div>";
  html += "<div class='stat clickable' onclick='sc(\"speed\", \"Set Print Speed (%):\")'><h4>Speed</h4><div>" + String(printSpeed) + "%</div></div>";
  html += "<div class='stat clickable' onclick='sc(\"fan\", \"Set Fan Speed (0-255):\")'><h4>Fan</h4><div>" + String(fanSpeed) + "%</div></div>";
  html += "<div class='stat'><h4>Progress</h4><div>" + String(printProgress, 1) + "%</div></div>";
  html += "</div></div>";

  html += "<div class='card'><h3 style='margin-top:0;'>Filament Settings</h3>";
  html += "<form action='/save_fil' method='POST'>";
  for(int i=0; i<4; i++) {
      html += "<div style='display:flex; justify-content:space-between; margin-bottom:10px;'>";
      html += "<label style='color:#888;'>Tool " + String(i+1) + " Type: <select name='t" + String(i) + "_type' style='background:#1a1d24; color:white; border:1px solid #333; padding:5px;'>";
      for (int j=0; j<10; j++) {
          String sel = (filType[i] == FIL_TYPES[j]) ? "selected" : "";
          html += "<option value='" + FIL_TYPES[j] + "' " + sel + ">" + FIL_TYPES[j] + "</option>";
      }
      html += "</select></label>";
      html += "<label style='color:#888;'>Color: <input type='color' name='t" + String(i) + "_color' value='" + rgb565ToHex(filColor[i]) + "' style='background:none; border:none; height:30px; cursor:pointer;'></label>";
      html += "</div>";
  }
  html += "<input type='submit' value='Save Filaments' class='btn' style='margin-top:10px;'></form></div>";

  html += "</div></body></html>";
  server.send(200, "text/html", html);
}

void handleAudio() {
  if (!enableAudio) {
      server.sendHeader("Location", "/tech", true);
      server.send(302, "text/plain", "");
      return;
  }
  
  String html = getWebHeader("audio");

  html += "<div class='card'><h3 style='margin-top:0;'>Speaker Controls</h3>";
  html += "<p style='color:#888; font-weight:bold;'>Volume Level: " + String(currentVolume) + "%</p>";
  html += "<div class='grid'>";
  html += "<a href='/vol_down' class='btn' style='background:#503232; color:white;'>Volume Down</a>";
  html += "<a href='/vol_up' class='btn' style='background:#325032; color:white;'>Volume Up</a>";
  html += "</div>";
  html += "<a href='/stop_sound' class='btn' style='background:#b30000; color:white; margin-top:15px;'>Stop Audio</a>";
  html += "</div>";

  html += "<div class='card'><h3 style='margin-top:0;'>SD Card Audio Playlist</h3>";
  if (sdCardReady) {
      File root = SD.open("/");
      File file = root.openNextFile();
      bool foundAudio = false;
      while(file) {
          String fn = String(file.name());
          if(!file.isDirectory() && (fn.endsWith(".wav") || fn.endsWith(".WAV") || fn.endsWith(".mp3") || fn.endsWith(".MP3") || fn.endsWith(".m4a"))) {
              foundAudio = true;
              if(!fn.startsWith("/")) fn = "/" + fn;
              html += "<div style='display:flex; justify-content:space-between; align-items:center; padding:10px; border-bottom:1px solid #333;'>";
              html += "<span style='color:white;'>" + fn + "</span>";
              html += "<a href='/play_sound?file=" + urlEncode(fn) + "' class='btn' style='width:auto; padding:8px 20px; margin:0;'>Play</a>";
              html += "</div>";
          }
          file = root.openNextFile();
      }
      if (!foundAudio) {
          html += "<p style='color:#888;'>No .mp3 or .wav files found on the SD card.</p>";
      }
      
      html += "<hr style='border:1px solid #333; margin:15px 0;'>";
      html += "<form method='POST' action='/upload_audio' enctype='multipart/form-data'>";
      html += "<label style='color:#888; font-weight:bold; font-size:12px;'>UPLOAD AUDIO FILE TO SD CARD</label><br>";
      html += "<input type='file' name='f' accept='.wav,.mp3,.m4a' style='background:none; border:none; color:white; padding:10px 0; width:100%;'>";
      html += "<input type='submit' value='Upload to Screen SD' class='btn' style='margin-top:5px;'></form>";
  } else {
      html += "<p style='color:#ff4f4f; font-weight:bold;'>SD Card Not Mounted</p>";
  }
  html += "</div>";

  html += "</div></body></html>";
  server.send(200, "text/html", html);
}


void handleTech() {
  String html = getWebHeader("tech");

#ifdef BOARD_WAVESHARE_43
  String sysBoard = "Waveshare ESP32-S3 4.3\"";
  String sysRes = "4.3 inch RGB LCD (800 x 480)";
  String sysDisp = "RGB Parallel Bus (LovyanGFX)";
  String sysTouch = "GT911 Capacitive Touch (I2C)";
#elif defined(BOARD_SUNTON_70)
  String sysBoard = "AITRIP / Sunton ESP32-8048S070C 7.0\"";
  String sysRes = "7.0 inch RGB LCD (800 x 480)";
  String sysDisp = "RGB Parallel Bus (EK9716)";
  String sysTouch = "GT911 Capacitive Touch (I2C)";
#elif defined(BOARD_40_INCH)
  String sysBoard = "ESP32 4.0-inch CYD";
  String sysRes = "4.0 inch TFT LCD (480 x 320)";
  String sysDisp = "ST7796 (VSPI)";
  String sysTouch = "XPT2046 (Shared SPI)";
#elif defined(BOARD_CYD_28) || defined(BOARD_28_DUAL) || defined(BOARD_CYD_28_DUAL)
  String sysBoard = "ESP32-2432S028 (CYD 2.8)";
  String sysRes = "2.8 inch TFT LCD (320 x 240)";
#ifdef ST7789_2_DRIVER
  String sysDisp = "ST7789 (Dual-USB Edition)";
#else
  String sysDisp = "ILI9341 (Standard)";
#endif
  String sysTouch = "XPT2046 (Resistive SPI)";
#else
  String sysBoard = "Unknown ESP32";
  String sysRes = "Unknown Resolution";
  String sysDisp = "Unknown Controller";
  String sysTouch = "Unknown Touch";
#endif

  html += "<div class='card'><h3 style='margin-top:0;'>Hardware Diagnostics</h3>";
  html += "<p><b>Board Type:</b> " + sysBoard + "</p>";
  html += "<p><b>Screen Spec:</b> " + sysRes + "</p>";
  html += "<p><b>Display Controller:</b> " + sysDisp + "</p>";
  html += "<p><b>Touch Controller:</b> " + sysTouch + "</p>";
  html += "<p><b>Target Printer IP:</b> " + String(printerIP) + "</p>";
  html += "<p><b>Wi-Fi Network:</b> " + WiFi.SSID() + "</p>";
  html += "<p><b>Signal Strength:</b> " + String(WiFi.RSSI()) + " dBm</p>";
  html += "<p><b>MAC Address:</b> " + WiFi.macAddress() + "</p>";
  
  if (showBattery) {
      float v = getBatteryVoltage();
      html += "<p><b>Battery Status:</b> " + String(getBatteryPercentage(v)) + "% (" + String(v, 2) + "V)</p>";
  } else {
      html += "<p><b>Battery Status:</b> Disabled</p>";
  }
  
#if defined(BOARD_40_INCH) || defined(BOARD_WAVESHARE_43) || defined(BOARD_SUNTON_70)
  if (envMode > 0) {
      if (bmeReady) {
          float tempC = bme.readTemperature();
          if (isnan(tempC) || tempC > 100.0 || tempC < -40.0) {
              html += "<p><b>Env Sensor:</b> Error (Connection Lost - Check Wires!)</p>";
          } else {
              float tempV = useFahrenheit ? (tempC * 9.0 / 5.0) + 32.0 : tempC;
              tempV += tempOffset; 
              String unitV = useFahrenheit ? "F" : "C";
              float hum = bme.readHumidity() + humOffset; 
              float pressV = bme.readPressure() / 100.0F;

              String printStatus = "Optimal for Printing";
              if (hum > 55.0) printStatus = "Poor (High Humidity - Dry Filament!)";
              else if (tempC < 15.0) printStatus = "Poor (Too Cold - Risk of Warping)";
              else if (tempC > 35.0) printStatus = "Poor (Too Hot - Heat Creep Risk)";
              else if (hum > 45.0) printStatus = "Fair (Monitor Moisture)";

              html += "<p><b>Env Sensor:</b> " + String(tempV, 1) + "&deg;" + unitV + ", " + String(hum, 1) + "% RH, " + String(pressV, 1) + " hPa<br><span style='color:#00E5FF; font-size:14px;'><i>Status: " + printStatus + "</i></span></p>";
          }
      } else {
          html += "<p><b>Env Sensor:</b> Error (Not Detected on Boot)</p>";
      }
  } else {
      html += "<p><b>Env Sensor:</b> Disabled</p>";
  }

  // Thermocouple Hardware Diagnostic
  if (tcMode > 0) {
    if (max31855Ready) {
      double tcC = thermocouple.readCelsius();
      if (isnan(tcC)) {
        html += "<p><b>Thermocouple:</b> Error (Open Circuit / Fault)</p>";
      } else {
        float tcVal = useFahrenheit ? (tcC * 9.0 / 5.0) + 32.0 : tcC;
        String tcUnit = useFahrenheit ? "&deg;F" : "&deg;C";
        html += "<p><b>Thermocouple:</b> " + String(tcVal, 1) + tcUnit + "</p>";
      }
    } else {
      html += "<p><b>Thermocouple:</b> Error (Not Detected on Boot)</p>";
    }
  } else {
    html += "<p><b>Thermocouple:</b> Disabled</p>";
  }
#endif
  
  html += "<p><b>Free RAM Heap:</b> " + String(ESP.getFreeHeap() / 1024) + " KB</p>";
  html += "</div>";

  html += "<div class='card'><h3 style='margin-top:0;'>Screen SD Card Storage</h3>";
  if (sdCardReady) {
    html += "<p style='color:#28c828; font-weight:bold;'>MOUNTED SUCCESSFULLY</p>";
    html += "<div class='grid'>";
    html += "<div class='stat'><h4>Total Space</h4><div>" + String((unsigned long)sdCardTotal) + " MB</div></div>";
    html += "<div class='stat'><h4>Used Space</h4><div>" + String((unsigned long)sdCardUsed) + " MB</div></div>";
    html += "</div>";
    
    html += "<hr style='border:1px solid #333; margin:15px 0;'>";
    html += "<form method='POST' action='/upload' enctype='multipart/form-data'>";
    html += "<label style='color:#888; font-weight:bold; font-size:12px;'>UPLOAD FILE TO DISPLAY SD CARD</label><br>";
    html += "<input type='file' name='f' accept='.gcode,.3mf' style='background:none; border:none; color:white; padding:10px 0; width:100%;'>";
    html += "<input type='submit' value='Upload GCODE to Screen SD' class='btn' style='margin-top:5px; background:#00E5FF; color:black;'></form>";

    html += "<hr style='border:1px solid #333; margin:15px 0;'>";
    html += "<a href='/setup_sd' class='btn' style='background:#2A3B5C; color:#00E5FF;'>Initialize SD Card Directories</a>";
    html += "<p style='color:#888; font-size:11px; text-align:center;'>Creates default folders needed for screen features.</p>";
  } else {
    html += "<p style='color:#ff4f4f; font-weight:bold;'>NOT MOUNTED</p>";
  }
  html += "</div>";

  html += "<div class='card'><h3 style='margin-top:0;'>System Configuration</h3>";
  html += "<form action='/save' method='POST'>";
  
  html += "<label style='color:#888; font-weight:bold; font-size:12px;'>PRINTER IP ADDRESS</label><br>";
  html += "<input type='text' name='ip' value='" + String(printerIP) + "'>";
  html += "<label style='color:#888; font-weight:bold; font-size:12px;'>DISPLAY NAME</label><br>";
  html += "<input type='text' name='name' value='" + String(printerName) + "'>";
  html += "<label style='color:#888; font-weight:bold; font-size:12px;'>SCREEN URL (mDNS Hostname)</label><br>";
  html += "<input type='text' name='mdns' value='" + String(mdnsName) + "'>";
  
  html += "<label style='color:#888; font-weight:bold; font-size:12px;'>INVERT DISPLAY COLORS</label><br>";
  html += "<select name='invert_display'>";
  html += "<option value='0'" + String(!invertDisplay ? " selected" : "") + ">Normal</option>";
  html += "<option value='1'" + String(invertDisplay ? " selected" : "") + ">Inverted (Fixes washed out colors)</option>";
  html += "</select>";

  int currentRot = preferences.getInt("rotation", 3);
  html += "<label style='color:#888; font-weight:bold; font-size:12px;'>SCREEN ROTATION</label><br>";
  html += "<select name='rotation'>";
  html += "<option value='3'" + String(currentRot == 3 ? " selected" : "") + ">Landscape Default</option>";
  html += "<option value='1'" + String(currentRot == 1 ? " selected" : "") + ">Landscape Inverted</option>";
  html += "<option value='0'" + String(currentRot == 0 ? " selected" : "") + ">Portrait</option>";
  html += "<option value='2'" + String(currentRot == 2 ? " selected" : "") + ">Portrait Inverted</option>";
  html += "</select>";
  
  html += "<label style='color:#888; font-weight:bold; font-size:12px;'>ENABLE ON-SCREEN BATTERY MONITOR</label><br>";
  html += "<select name='show_battery'>";
  html += "<option value='0'" + String(!showBattery ? " selected" : "") + ">Disabled</option>";
  html += "<option value='1'" + String(showBattery ? " selected" : "") + ">Enabled</option>";
  html += "</select>";

  html += "<label style='color:#888; font-weight:bold; font-size:12px;'>ENABLE SPEAKER / AUDIO PLAYER</label><br>";
  html += "<select name='enable_audio'>";
  html += "<option value='0'" + String(!enableAudio ? " selected" : "") + ">Disabled</option>";
  html += "<option value='1'" + String(enableAudio ? " selected" : "") + ">Enabled</option>";
  html += "</select>";

#if defined(BOARD_40_INCH) || defined(BOARD_WAVESHARE_43) || defined(BOARD_SUNTON_70)
  html += "<label style='color:#888; font-weight:bold; font-size:12px;'>ENABLE BME280 ENV SENSOR</label><br>";
  html += "<select name='env_mode'>";
  html += "<option value='0'" + String(envMode == 0 ? " selected" : "") + ">Disabled</option>";
  html += "<option value='1'" + String(envMode == 1 ? " selected" : "") + ">Web UI Only</option>";
  html += "<option value='2'" + String(envMode == 2 ? " selected" : "") + ">Web UI + Display Screen</option>";
  html += "</select>";

  html += "<label style='color:#888; font-weight:bold; font-size:12px;'>ENABLE MAX31855 THERMOCOUPLE</label><br>";
  html += "<select name='tc_mode'>";
  html += "<option value='0'" + String(tcMode == 0 ? " selected" : "") + ">Disabled</option>";
  html += "<option value='1'" + String(tcMode == 1 ? " selected" : "") + ">Web UI Only</option>";
  html += "<option value='2'" + String(tcMode == 2 ? " selected" : "") + ">Web UI + Display Screen</option>";
  html += "</select>";
  
  #ifdef BOARD_40_INCH
  html += "<div class='grid'>";
  html += "<div><label style='color:#888; font-weight:bold; font-size:12px;'>SENSOR SDA PIN</label><br>";
  html += "<input type='number' name='bme_sda' value='" + String(bmeSDA) + "'></div>";
  html += "<div><label style='color:#888; font-weight:bold; font-size:12px;'>SENSOR SCL PIN</label><br>";
  html += "<input type='number' name='bme_scl' value='" + String(bmeSCL) + "'></div>";
  html += "</div>";
  #endif

  html += "<label style='color:#888; font-weight:bold; font-size:12px;'>TEMPERATURE UNIT</label><br>";
  html += "<select name='use_f'>";
  html += "<option value='0'" + String(!useFahrenheit ? " selected" : "") + ">Celsius (&deg;C)</option>";
  html += "<option value='1'" + String(useFahrenheit ? " selected" : "") + ">Fahrenheit (&deg;F)</option>";
  html += "</select>";
  
  html += "<hr style='border:1px solid #333; margin:15px 0;'>";
  html += "<h3 style='color:#00E5FF; margin-top:0;'>Sensor Calibration</h3>";
  html += "<label style='color:#888; font-weight:bold; font-size:12px;'>TEMP OFFSET (" + String(useFahrenheit ? "&deg;F" : "&deg;C") + ")</label><br>";
  html += "<input type='number' step='0.1' name='t_off' value='" + String(tempOffset, 1) + "'>";
  html += "<label style='color:#888; font-weight:bold; font-size:12px;'>HUMIDITY OFFSET (%)</label><br>";
  html += "<input type='number' step='0.1' name='h_off' value='" + String(humOffset, 1) + "'>";
#endif

  html += "<hr style='border:1px solid #333; margin:15px 0;'>";
  html += "<h3 style='color:#00E5FF; margin-top:0;'>Theme Customization</h3>";
  html += "<label style='color:#888; font-weight:bold; font-size:12px;'>UI THEME</label><br>";
  html += "<select name='theme' id='themeSelect' onchange='toggleCustom()' style='margin-bottom:10px;'>";
  html += "<option value='0'" + String(currentTheme == 0 ? " selected" : "") + ">Dark Mode (Default)</option>";
  html += "<option value='1'" + String(currentTheme == 1 ? " selected" : "") + ">Light Mode</option>";
  html += "<option value='2'" + String(currentTheme == 2 ? " selected" : "") + ">Cyberpunk</option>";
  html += "<option value='3'" + String(currentTheme == 3 ? " selected" : "") + ">Retro Terminal</option>";
  html += "<option value='4'" + String(currentTheme == 4 ? " selected" : "") + ">Custom Colors</option>";
  html += "</select>";

  html += "<div id='customColors' style='display:" + String(currentTheme == 4 ? "block" : "none") + ";'>";
  html += "<div style='display:flex; justify-content:space-between; margin-bottom:5px;'><label style='color:#888;'>Background</label><input type='color' name='c_bg' value='" + customBG + "' style='width:50%; height:30px;'></div>";
  html += "<div style='display:flex; justify-content:space-between; margin-bottom:5px;'><label style='color:#888;'>Sidebar</label><input type='color' name='c_side' value='" + customSidebar + "' style='width:50%; height:30px;'></div>";
  html += "<div style='display:flex; justify-content:space-between; margin-bottom:5px;'><label style='color:#888;'>Cards/Modals</label><input type='color' name='c_card' value='" + customCard + "' style='width:50%; height:30px;'></div>";
  html += "<div style='display:flex; justify-content:space-between; margin-bottom:5px;'><label style='color:#888;'>Text</label><input type='color' name='c_text' value='" + customText + "' style='width:50%; height:30px;'></div>";
  html += "<div style='display:flex; justify-content:space-between; margin-bottom:5px;'><label style='color:#888;'>Accent</label><input type='color' name='c_acc' value='" + customAccent + "' style='width:50%; height:30px;'></div>";
  html += "</div>";
  html += "<script>toggleCustom();</script>";

#if defined(BOARD_WAVESHARE_43) || defined(BOARD_40_INCH) || defined(BOARD_CYD_28) || defined(BOARD_28_INCH) || defined(BOARD_28_DUAL) || defined(BOARD_CYD_28_DUAL) || defined(BOARD_SUNTON_70)
  html += "<hr style='border:1px solid #333; margin:15px 0;'>";
  html += "<h3 style='color:#00E5FF; margin-top:0;'>Airspace Slicer Settings</h3>";
  html += "<p style='color:#888; font-size:12px; margin-top:-10px;'>Anonymous gives 400 credits/day. Standard OpenSky OAuth2 accounts get 4,000 credits/day.</p>";
  
  html += "<label style='color:#888; font-weight:bold; font-size:12px;'>ENABLE FLIGHT RADAR TAB</label><br>";
  html += "<select name='enable_radar'>";
  html += "<option value='0'" + String(!enableRadar ? " selected" : "") + ">Disabled</option>";
  html += "<option value='1'" + String(enableRadar ? " selected" : "") + ">Enabled</option>";
  html += "</select>";

  html += "<label style='color:#888; font-weight:bold; font-size:12px;'>AUTHENTICATION MODE</label><br>";
  html += "<select name='r_auth'>";
  html += "<option value='0'" + String(radarAuthMode == 0 ? " selected" : "") + ">Anonymous (No Account Needed - 400 Credits)</option>";
  html += "<option value='1'" + String(radarAuthMode == 1 ? " selected" : "") + ">OAuth2 (Client Credentials - 4,000 Credits)</option>";
  html += "</select>";
  
  html += "<div class='grid'><div><label style='color:#888; font-weight:bold; font-size:12px;'>LATITUDE</label><br>";
  html += "<input type='text' name='r_lat' value='" + String(homeLat, 4) + "'></div>";
  html += "<div><label style='color:#888; font-weight:bold; font-size:12px;'>LONGITUDE</label><br>";
  html += "<input type='text' name='r_lon' value='" + String(homeLon, 4) + "'></div></div>";
  
  html += "<div class='grid'><div><label style='color:#888; font-weight:bold; font-size:12px;'>OPENSKY CLIENT ID</label><br>";
  html += "<input type='text' name='r_cid' value='" + radarClientId + "'></div>";
  html += "<div><label style='color:#888; font-weight:bold; font-size:12px;'>OPENSKY CLIENT SECRET</label><br>";
  html += "<input type='text' name='r_csec' value='" + radarClientSecret + "' placeholder='Leave blank to keep saved secret'></div></div>";

  html += "<label style='color:#888; font-weight:bold; font-size:12px;'>RADAR RADIUS (Degrees: 0.5 ≈ 35 miles, Max: 2.0)</label><br>";
  html += "<input type='text' name='r_rad' value='" + String(radarRadiusDeg, 2) + "'>";
#endif
  
  html += "<hr style='border:1px solid #333; margin:15px 0;'>";
  html += "<label style='color:#888; font-weight:bold; font-size:12px;'>NEW WI-FI SSID (Leave blank to keep current)</label><br>";
  html += "<input type='text' name='wifi_ssid' placeholder='Current: " + WiFi.SSID() + "'>";
  html += "<label style='color:#888; font-weight:bold; font-size:12px;'>NEW WI-FI PASSWORD</label><br>";
  html += "<input type='password' name='wifi_pass' placeholder='********'>";
  html += "<input type='submit' value='Save & Reboot' class='btn' style='margin-top:10px;'></form>";
  html += "<a href='/calibrate' class='btn' style='background:#2A3B5C; color:#00E5FF; margin-top:15px;'>Recalibrate Touchscreen</a></div>";
  
  html += "<div class='footer'>" + String(FW_VERSION) + " Firmware by Nates Print Shop</div>";
  html += "</div></body></html>";
  server.send(200, "text/html", html);
}


void handleFileUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.startsWith("/")) filename = "/" + filename;
    if (SD.exists(filename)) SD.remove(filename);
    fsUploadFile = SD.open(filename, FILE_WRITE);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (fsUploadFile) fsUploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (fsUploadFile) {
      fsUploadFile.close();
      sdCardUsed = SD.usedBytes() / (1024 * 1024);
      loadFilesFromSD(); 
    }
  }
}

void handleControl() {
  if (server.hasArg("bed") && server.arg("bed") != "") sendGcode("M140 S" + server.arg("bed"));
  if (server.hasArg("tool") && server.arg("tool") != "") sendGcode("M104 T" + String(activeTool) + " S" + server.arg("tool"));
  if (server.hasArg("t0") && server.arg("t0") != "") sendGcode("M104 T0 S" + server.arg("t0"));
  if (server.hasArg("t1") && server.arg("t1") != "") sendGcode("M104 T1 S" + server.arg("t1"));
  if (server.hasArg("t2") && server.arg("t2") != "") sendGcode("M104 T2 S" + server.arg("t2"));
  if (server.hasArg("t3") && server.arg("t3") != "") sendGcode("M104 T3 S" + server.arg("t3"));
  if (server.hasArg("tool_active") && server.arg("tool_active") != "") sendGcode("T" + server.arg("tool_active"));
  if (server.hasArg("speed") && server.arg("speed") != "") sendGcode("M220 S" + server.arg("speed"));
  if (server.hasArg("fan") && server.arg("fan") != "") sendGcode("M106 S" + server.arg("fan"));
  
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void handleSave() {
  // Preferences is already open (begun in setup and never ended).

  String newIP = server.arg("ip");
  String newName = server.arg("name");
  String newMDNS = server.arg("mdns");
  String newSSID = server.arg("wifi_ssid");
  String newPass = server.arg("wifi_pass");
  
  if (newIP.length() > 0) newIP.toCharArray(printerIP, sizeof(printerIP));
  if (newName.length() > 0) newName.toCharArray(printerName, sizeof(printerName));
  if (newMDNS.length() > 0) newMDNS.toCharArray(mdnsName, sizeof(mdnsName));
  
  preferences.putString("printer_ip", printerIP);
  preferences.putString("printer_name", printerName);
  preferences.putString("mdns_name", mdnsName);
  
  if (server.hasArg("invert_display")) {
    invertDisplay = (server.arg("invert_display") == "1");
    preferences.putBool("invertDisplay", invertDisplay);
#if !defined(BOARD_WAVESHARE_43) && !defined(BOARD_SUNTON_70)
    tft.invertDisplay(invertDisplay);
#endif
  }

  if (server.hasArg("rotation")) {
    int newRot = server.arg("rotation").toInt();
    preferences.putInt("rotation", newRot);
  }
  
  if (server.hasArg("show_battery")) {
    showBattery = (server.arg("show_battery") == "1");
    preferences.putBool("showBattery", showBattery);
  }

  if (server.hasArg("enable_audio")) {
    enableAudio = (server.arg("enable_audio") == "1");
    preferences.putBool("enableAudio", enableAudio);
  }

  if (server.hasArg("env_mode")) {
    envMode = server.arg("env_mode").toInt();
    preferences.putInt("envMode", envMode);
  }

#if defined(BOARD_40_INCH) || defined(BOARD_WAVESHARE_43) || defined(BOARD_SUNTON_70)
  if (server.hasArg("tc_mode")) {
    tcMode = server.arg("tc_mode").toInt();
    preferences.putInt("tcMode", tcMode);
  }
#endif

#ifdef BOARD_40_INCH
  if (server.hasArg("bme_sda")) preferences.putInt("bme_sda", server.arg("bme_sda").toInt());
  if (server.hasArg("bme_scl")) preferences.putInt("bme_scl", server.arg("bme_scl").toInt());
#endif

  if (server.hasArg("use_f")) {
    useFahrenheit = (server.arg("use_f") == "1");
    preferences.putBool("useFahrenheit", useFahrenheit);
  }
  
  if (server.hasArg("t_off")) preferences.putFloat("tempOffset", server.arg("t_off").toFloat());
  if (server.hasArg("h_off")) preferences.putFloat("humOffset", server.arg("h_off").toFloat());

#if defined(BOARD_WAVESHARE_43) || defined(BOARD_40_INCH) || defined(BOARD_CYD_28) || defined(BOARD_28_INCH) || defined(BOARD_28_DUAL) || defined(BOARD_CYD_28_DUAL) || defined(BOARD_SUNTON_70)
  if (server.hasArg("enable_radar")) {
    enableRadar = (server.arg("enable_radar") == "1" || server.arg("enable_radar") == "on" || server.arg("enable_radar") == "true");
    preferences.putBool("enableRadar", enableRadar);
  }

  if (server.hasArg("r_auth")) {
    radarAuthMode = server.arg("r_auth").toInt();
    preferences.putInt("radarAuthMode", radarAuthMode);
  }
  if (server.hasArg("r_lat")) { 
    homeLat = server.arg("r_lat").toFloat(); 
    preferences.putFloat("radarLat", homeLat); 
  }
  if (server.hasArg("r_lon")) { 
    homeLon = server.arg("r_lon").toFloat(); 
    preferences.putFloat("radarLon", homeLon); 
  }
  if (server.hasArg("r_rad")) { 
    radarRadiusDeg = server.arg("r_rad").toFloat(); 
    if (radarRadiusDeg > 2.0f) radarRadiusDeg = 2.0f; 
    if (radarRadiusDeg < 0.1f) radarRadiusDeg = 0.1f;
    preferences.putFloat("radarRad", radarRadiusDeg); 
  }
  if (server.hasArg("r_cid")) { 
    radarClientId = server.arg("r_cid"); 
    preferences.putString("radarClientId", radarClientId); 
  }
  if (server.hasArg("r_csec") && server.arg("r_csec").length() > 0) { 
    radarClientSecret = server.arg("r_csec"); 
    preferences.putString("radarClientSecret", radarClientSecret); 
  }
#endif

  if (server.hasArg("theme")) {
    currentTheme = server.arg("theme").toInt();
    preferences.putInt("theme", currentTheme);
  }
  if (server.hasArg("c_bg")) preferences.putString("customBG", server.arg("c_bg"));
  if (server.hasArg("c_side")) preferences.putString("customSidebar", server.arg("c_side"));
  if (server.hasArg("c_card")) preferences.putString("customCard", server.arg("c_card"));
  if (server.hasArg("c_text")) preferences.putString("customText", server.arg("c_text"));
  if (server.hasArg("c_acc")) preferences.putString("customAccent", server.arg("c_acc"));

  bool changeWifi = (newSSID.length() > 0);
  if (changeWifi) {
    WiFi.begin(newSSID.c_str(), newPass.c_str());
  }
  
  String html = getWebHeader("tech") + "<div class='card'><h2 style='color:#00E5FF; text-align:center;'>Saved! Rebooting...</h2></div><script>setTimeout(function(){window.location.href='/';}, 5000);</script></div></body></html>";
  server.send(200, "text/html", html);
  
  delay(1000);
  ESP.restart(); 
}

void handleSaveFilament() {
  blockSyncUntil = millis() + 5000; 

  for (int i=0; i<4; i++) {
      String t = server.arg("t" + String(i) + "_type");
      String c = server.arg("t" + String(i) + "_color");
      
      String tKey = "filType" + String(i);
      String cKey = "filColor" + String(i);
      
      if (t != "") {
          filType[i] = t;
          preferences.putString(tKey.c_str(), t);
          syncFilamentToKlipper(i); 
      }
      if (c != "") {
          filColor[i] = hexToRGB565(c);
          preferences.putString(cKey.c_str(), c);
          syncFilamentToKlipper(i); 
      }
  }
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void handleCalibrateWeb() {
  server.send(200, "text/html", "<html><body style='background:#1e232d; color:white; text-align:center; padding:50px;'><h2>Look at your display screen to complete the 4-point calibration!</h2><a href='/'>Return to Dashboard</a></body></html>");
  isCalibrated = false;
  touchCalibrate();
  tft.fillScreen(BG_COLOR);
  drawSidebar();
  drawSettingsTab();
}