#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <TFT_eSPI.h> 
#include <TFT_Touch.h> 
#include <Preferences.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <SD.h>
#include <FS.h>

// --- HARDWARE PINS (LCDWIKI E32R28T / CYD ESP32-32E) ---
#define TFT_BL 21
#define SD_CS 5 

// Touch Pins (XPT2046)
#define RTP_DOUT 39
#define RTP_DIN  32
#define RTP_SCK  25
#define RTP_CS   33

TFT_eSPI tft = TFT_eSPI();
TFT_Touch touch = TFT_Touch(RTP_CS, RTP_SCK, RTP_DIN, RTP_DOUT);

Preferences preferences;
WebServer server(80);

// --- DYNAMIC SETTINGS ---
char printerIP[32] = "192.168.87.125"; 
char printerName[32] = "Snapmaker U1"; 
char mdnsName[32] = "u1-display"; 
bool shouldSaveConfig = false;
bool sdCardReady = false;
uint64_t sdCardTotal = 0;
uint64_t sdCardUsed = 0;

// --- STATE VARIABLES ---
unsigned long lastUpdate = 0;
const int updateInterval = 2000;
String currentState = "Offline";
String currentFile = "Idle";
float bedTemp = 0.0, bedTarget = 0.0;
float toolTemp[4] = {0,0,0,0};
float toolTarget[4] = {0,0,0,0};
int printSpeed = 100, fanSpeed = 0;
float printProgress = 0.0;
int activeTool = -1; // -1 represents "None"
float moveDist = 10.0; 

// Filament & Tool State Data
String filType[4] = {"PLA", "PLA", "PLA-CF", "WOOD"}; 
uint16_t filColor[4] = {0x2104, 0x07FF, 0x3186, 0xA285}; 
bool toolAttached[4] = {true, false, false, false}; 
bool toolLoaded[4] = {true, true, true, true};      

// Preset Lists for Web UI & Quick Palette
const String FIL_TYPES[] = {"PLA", "PETG", "ABS", "TPU", "ASA", "PC", "NYLON", "PLA-CF", "PETG-CF", "WOOD"};
const uint16_t FIL_COLORS[] = {TFT_WHITE, TFT_RED, TFT_BLUE, TFT_GREEN, TFT_YELLOW, TFT_ORANGE, TFT_CYAN, TFT_MAGENTA, 0x2104, 0xA285};

unsigned long lastTouchTime = 0; 
int currentTab = 0; // 0=Home, 1=Toolhead, 2=Move, 3=Print, 4=Settings

// --- SD CARD & KLIPPER FILES ---
String fileList[20];
int fileCount = 0;

String klipperFileList[20];
int klipperFileCount = 0;

int fileScrollIndex = 0;
String selectedFile = "";
int printTabSource = 0; // 0 = Klipper, 1 = Screen SD

// --- MODAL STATE ---
int activeModal = 0; 
int kbMode = 0; // 0 = Text, 1 = HEX Color, 2 = IP Address, 3 = Printer Name
String kbInput = ""; 
bool optBedLevel = true;
bool optTimelapse = false;
bool optAdvMap = false;
int optT0 = 0;

// --- TOAST NOTIFICATIONS ---
String toastTitle = "";
String toastMsg = "";
bool toastError = false;
bool toastDrawn = false; 
unsigned long toastExpireTime = 0;
bool forceRedrawForToast = false; 

// Pro QWERTY Layout
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

// --- UI COLORS ---
#define BG_COLOR tft.color565(15, 17, 21)       
#define SIDEBAR_COLOR tft.color565(22, 27, 34)  
#define CARD_COLOR tft.color565(26, 29, 36)     
#define TEXT_GRAY tft.color565(136, 136, 136)   
#define ACCENT_CYAN tft.color565(0, 229, 255)   
#define BTN_BLUE tft.color565(30, 80, 150)
#define BTN_GREEN tft.color565(40, 120, 60)
#define BTN_RED tft.color565(150, 50, 50)       

// Declarations
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
void drawToolModal(int idx);
void drawKeyboardModal(int idx);
void updateKeyboardInputBox();
void drawColorPickerModal(int idx);
void drawPrintOptionsModal();
void updateDynamicUI();
void fetchPrinterData();
void sendGcode(String command);
void handleRoot();
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

// --- HELPER FUNCTIONS ---
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
  tft.fillRoundRect(50, 10, 260, 45, 6, CARD_COLOR);
  tft.drawRoundRect(50, 10, 260, 45, 6, toastError ? BTN_RED : BTN_GREEN);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(toastTitle, 60, 22, 2);
  tft.setTextColor(TEXT_GRAY);
  
  String shortMsg = toastMsg;
  if(shortMsg.length() > 35) shortMsg = shortMsg.substring(0, 32) + "...";
  tft.drawString(shortMsg, 60, 38, 1);
  tft.setTextDatum(TL_DATUM);
}

// -------------------------------------------------------------
// VECTOR BOOT LOGO: "CYD SNAPMAKER SCREEN"
// -------------------------------------------------------------
void drawBootLogo() {
  tft.fillScreen(BG_COLOR);
  
  // Outer Tech Border
  tft.drawRect(10, 10, 300, 220, ACCENT_CYAN);
  tft.drawRect(12, 12, 296, 216, tft.color565(50, 55, 65));

  // Circuit Trace Accents (Top Left)
  tft.drawLine(10, 40, 30, 40, ACCENT_CYAN);
  tft.drawLine(30, 40, 40, 30, ACCENT_CYAN);
  tft.drawLine(40, 30, 100, 30, ACCENT_CYAN);

  // Circuit Trace Accents (Bottom Right)
  tft.drawLine(310, 200, 290, 200, ACCENT_CYAN);
  tft.drawLine(290, 200, 280, 210, ACCENT_CYAN);
  tft.drawLine(280, 210, 220, 210, ACCENT_CYAN);

  // Logo Typography
  tft.setTextColor(ACCENT_CYAN);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("CYD", 160, 90, 4); 
  
  tft.setTextColor(TFT_WHITE);
  tft.drawString("SNAPMAKER SCREEN", 160, 130, 2);
  
  tft.setTextColor(ACCENT_CYAN);
  tft.drawString("v1.0 - Made by Nates Print Shop", 160, 155, 1);
  
  tft.setTextColor(TEXT_GRAY);
  tft.drawString("Initializing Core Systems...", 160, 190, 1);
  tft.setTextDatum(TL_DATUM);
}

// -------------------------------------------------------------
// WI-FI SETUP SCREEN (Overrides Boot Logo during Captive Portal)
// -------------------------------------------------------------
void configModeCallback(WiFiManager *myWiFiManager) {
  tft.fillScreen(BG_COLOR);
  
  tft.setTextColor(ACCENT_CYAN);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Wi-Fi Setup Required", 160, 60, 4);
  
  tft.setTextColor(TFT_WHITE);
  tft.drawString("Connect phone/PC to network:", 160, 120, 2);
  
  tft.setTextColor(TFT_YELLOW);
  tft.drawString(myWiFiManager->getConfigPortalSSID(), 160, 150, 2);
  
  tft.setTextColor(TEXT_GRAY);
  tft.drawString("Then go to http://192.168.4.1", 160, 190, 1);
  tft.setTextDatum(TL_DATUM);
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
  http.begin("http://" + String(printerIP) + ":7125/server/files/list?root=gcodes");
  int httpCode = http.GET();
  
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
  HTTPClient http;
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
  
  tft.drawRect(30, yOffset, 260, 20, tft.color565(50, 55, 65));

  while (f.available()) {
      size_t c = f.read(buf, 4096);
      client.write(buf, c);
      uploaded += c;
      
      if (fileLen > 0) {
          int p = (uploaded * 100) / fileLen;
          if (p > lastP && p <= 100) {
              tft.fillRect(32, yOffset + 2, (int)((p / 100.0) * 256), 16, ACCENT_CYAN);
              tft.setTextColor(BG_COLOR);
              tft.setTextDatum(MC_DATUM);
              tft.drawString(String(p) + "% Complete", 160, yOffset + 10, 1);
              lastP = p;
          }
      }
      server.handleClient(); 
      yield(); 
  }
  
  free(buf);
  client.print(tail);
  
  long timeout = millis() + 5000;
  while(client.connected() && millis() < timeout) {
      if(client.available()) {
          String line = client.readStringUntil('\n');
          if (line == "\r") break;
      }
  }
  f.close();
  client.stop();
  return true;
}

// -------------------------------------------------------------
// THE ULTIMATE KLIPPER DUMP-ZONE SMART PATCHER 
// -------------------------------------------------------------
void launchPatchedPrint(String filename, int source, bool bBed, bool bTime, bool bAdv, int t0_map) {
  tft.fillScreen(BG_COLOR);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Preparing Job...", 160, 40, 2);
  
  String targetFilename = filename;
  bool needsPatching = (bBed || bTime || bAdv || t0_map != 0);

  if (needsPatching) {
      tft.drawString("Patching File & Variables...", 160, 70, 1);
      
      if (!SD.exists("/dump_zone")) SD.mkdir("/dump_zone");
      
      int extIdx = targetFilename.lastIndexOf('.');
      if (extIdx > 0) {
          targetFilename = targetFilename.substring(0, extIdx) + "_screen" + targetFilename.substring(extIdx);
      } else {
          targetFilename += "_screen.gcode";
      }

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

      tft.drawRect(30, 90, 260, 20, tft.color565(50, 55, 65));

      if (source == 0) { 
          HTTPClient http;
          http.begin("http://" + String(printerIP) + ":7125/server/files/gcodes/" + urlEncode(filename));
          int httpCode = http.GET();
          if (httpCode == 200) {
              totalBytes = http.getSize();
              if (totalBytes < 0) totalBytes = 0; 
              
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
                          int p = (processedBytes * 100) / totalBytes;
                          if (p > lastP && p <= 100) {
                              tft.fillRect(32, 92, (int)((p / 100.0) * 256), 16, ACCENT_CYAN);
                              tft.setTextColor(BG_COLOR);
                              tft.drawString(String(p) + "%", 160, 100, 1);
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
                      int p = (processedBytes * 100) / totalBytes;
                      if (p > lastP && p <= 100) {
                          tft.fillRect(32, 92, (int)((p / 100.0) * 256), 16, ACCENT_CYAN);
                          tft.setTextColor(BG_COLOR);
                          tft.drawString(String(p) + "%", 160, 100, 1);
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

      tft.setTextColor(TFT_WHITE, BG_COLOR);
      tft.drawString("Uploading Patched File...", 160, 130, 1);
      
      if(!uploadFileToKlipper("dump_zone/" + targetFilename, targetFilename, 150)) {
          showToast("Error", "Upload to printer failed!", true);
          activeModal = 0; currentTab = 0; forceRedrawForToast = true; return;
      }
  } else {
      if (source == 1) {
          tft.drawString("Uploading to Printer...", 160, 110, 1);
          if(!uploadFileToKlipper(filename, targetFilename, 130)) {
              showToast("Error", "Upload failed", true);
              activeModal = 0; currentTab = 0; forceRedrawForToast = true; return;
          }
      }
  }

  tft.setTextColor(TFT_WHITE, BG_COLOR);
  tft.drawString("Configuring & Launching...", 160, 200, 1);

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

void setup() {
  Serial.begin(115200);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  tft.init();
  tft.setRotation(3); 
  tft.invertDisplay(false);
  tft.setTextSize(1); 
  
  // Custom Boot Logo
  drawBootLogo();

  touch.setRotation(3);

  if (SD.begin(SD_CS)) {
    sdCardReady = true;
    sdCardTotal = SD.totalBytes() / (1024 * 1024); 
    sdCardUsed = SD.usedBytes() / (1024 * 1024);
    loadFilesFromSD();
  }

  preferences.begin("u1_config", false);
  String savedIP = preferences.getString("printer_ip", "192.168.87.125");
  String savedName = preferences.getString("printer_name", "Snapmaker U1");
  String savedMDNS = preferences.getString("mdns_name", "u1-display");
  
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
  if (sdCardReady && SD.exists("/calibration.json")) {
    File file = SD.open("/calibration.json", FILE_READ);
    if (file) {
      JsonDocument doc;
      if (!deserializeJson(doc, file)) {
        touch.setCal(doc["calXMin"], doc["calXMax"], doc["calYMin"], doc["calYMax"], 320, 240, doc["swapXY"]);
        loadedFromSD = true;
        isCalibrated = true;
      }
      file.close();
    }
  }

  if (!loadedFromSD) {
    isCalibrated = preferences.getBool("isCalibrated", false);
    if (isCalibrated) {
      int hmin = preferences.getInt("calXMin", 495);
      int hmax = preferences.getInt("calXMax", 3398);
      int vmin = preferences.getInt("calYMin", 721);
      int vmax = preferences.getInt("calYMax", 3448);
      bool xyswap = preferences.getBool("swapXY", true);
      touch.setCal(hmin, hmax, vmin, vmax, 320, 240, xyswap);
    } else {
      touch.setCal(495, 3398, 721, 3448, 320, 240, 1);
    }
  }

  WiFiManager wm;
  wm.setSaveConfigCallback(saveConfigCallback);
  
  // Set the callback that updates the screen if Wi-Fi setup is needed
  wm.setAPCallback(configModeCallback);
  
  WiFiManagerParameter custom_ip("printer_ip", "U1 IP Address", printerIP, 32);
  wm.addParameter(&custom_ip);

  // drawBootLogo() acts as the visual wait screen for this
  if (!wm.autoConnect("Snapmaker-U1-Display")) ESP.restart();
  
  if (shouldSaveConfig) {
    strcpy(printerIP, custom_ip.getValue());
    preferences.putString("printer_ip", printerIP);
  }

  if (MDNS.begin(mdnsName)) Serial.println("mDNS started");
  
  // Define Web Routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/tech", HTTP_GET, handleTech);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/save_fil", HTTP_POST, handleSaveFilament);
  server.on("/control", HTTP_POST, handleControl);
  server.on("/calibrate", HTTP_GET, handleCalibrateWeb);
  
  server.on("/upload", HTTP_POST, []() {
    server.sendHeader("Location", "/tech", true);
    server.send(302, "text/plain", "");
  }, handleFileUpload);

  server.begin();

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
  server.handleClient(); 

  if (touch.Pressed()) {
    if (millis() - lastTouchTime > 80) { 
      lastTouchTime = millis(); 
      
      int touchX = touch.X();
      int touchY = touch.Y();

      bool modalForceClosed = false;

      // RULE 1: SIDEBAR ALWAYS WINS
      if (activeModal > 0 && touchX < 42) {
          activeModal = 0;
          modalForceClosed = true;
      }
      
      // RULE 2: PROCESS MODAL TOUCHES
      else if (activeModal > 0) {
        bool closedModal = false;
        bool returnToTool = false;
        int targetTool = 0;
        bool handledTouch = false;
        
        if (activeModal == 1 || activeModal == 2) {
          for (int i = 0; i < 6; i++) {
            int r = i / 2;
            int c = i % 2;
            int bx = 60 + c * 130;
            int by = 45 + r * 60;
            if (touchX > bx && touchX < bx + 110 && touchY > by && touchY < by + 45) {
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
               closedModal = true; handledTouch = true; break;
            }
          }
          if (!handledTouch) closedModal = true; 
        } 
        else if (activeModal == 7) {
          for (int c = 0; c < 3; c++) {
            int bx = 50 + c * 75;
            if (touchX > bx && touchX < bx + 65 && touchY > 55 && touchY < 100) {
               int speeds[] = {50, 100, 150};
               sendGcode("M220 S" + String(speeds[c]));
               showToast("Print Speed", String(speeds[c]) + "%");
               closedModal = true; handledTouch = true; break;
            }
            if (touchX > bx && touchX < bx + 65 && touchY > 125 && touchY < 170) {
               int fans[] = {0, 127, 255};
               sendGcode("M106 S" + String(fans[c]));
               showToast("Part Fan", String((int)(fans[c]*100/255)) + "%");
               closedModal = true; handledTouch = true; break;
            }
          }
          if (!handledTouch) closedModal = true;
        }
        else if (activeModal == 8) {
          for (int i = 0; i < 4; i++) {
            int r = i / 2;
            int c = i % 2;
            int bx = 60 + c * 130;
            int by = 55 + r * 70;
            if (touchX > bx && touchX < bx + 110 && touchY > by && touchY < by + 50) {
               sendGcode("T" + String(i));
               showToast("Tool Change", "Swapped to T" + String(i+1));
               closedModal = true; handledTouch = true; break;
            }
          }
          if (!handledTouch) closedModal = true;
        }
        else if (activeModal >= 10 && activeModal <= 13) {
          int tIdx = activeModal - 10;
          if (touchY > 40 && touchY < 75) {
            if (touchX > 50 && touchX < 105) { sendGcode("M104 T" + String(tIdx) + " S0"); showToast("Heater Off", "Tool " + String(tIdx+1)); handledTouch = true; }
            else if (touchX > 115 && touchX < 170) { sendGcode("M104 T" + String(tIdx) + " S200"); showToast("Heating", "Tool " + String(tIdx+1) + " to 200C"); handledTouch = true; }
            else if (touchX > 180 && touchX < 235) { sendGcode("M104 T" + String(tIdx) + " S220"); showToast("Heating", "Tool " + String(tIdx+1) + " to 220C"); handledTouch = true; }
            else if (touchX > 245 && touchX < 300) { sendGcode("M104 T" + String(tIdx) + " S240"); showToast("Heating", "Tool " + String(tIdx+1) + " to 240C"); handledTouch = true; }
          }
          else if (touchY > 90 && touchY < 125) {
            if (touchX > 50 && touchX < 170) { 
              kbInput = filType[tIdx];
              kbMode = 0; 
              activeModal = 20 + tIdx; 
              drawKeyboardModal(tIdx); 
              return; 
            } 
            else if (touchX > 180 && touchX < 300) { 
              activeModal = 30 + tIdx; 
              drawColorPickerModal(tIdx); 
              return; 
            }
          }
          else if (touchY > 140 && touchY < 175) {
            if (touchX > 50 && touchX < 170) {
              if (toolAttached[tIdx]) { sendGcode("DETACH"); showToast("Tool", "Detaching..."); }
              else { sendGcode("ATTACH"); showToast("Tool", "Attaching..."); }
              toolAttached[tIdx] = !toolAttached[tIdx];
              drawToolModal(tIdx);
              handledTouch = true;
            } else if (touchX > 180 && touchX < 300) {
              if (toolLoaded[tIdx]) { sendGcode("UNLOAD_FILAMENT"); showToast("Filament", "Unloading T" + String(tIdx+1)); }
              else { sendGcode("LOAD_FILAMENT"); showToast("Filament", "Loading T" + String(tIdx+1)); }
              toolLoaded[tIdx] = !toolLoaded[tIdx];
              drawToolModal(tIdx);
              handledTouch = true;
            }
          }
          else if (touchY > 185 && touchX > 50 && touchX < 300) {
            closedModal = true; handledTouch = true;
          }
          if (!handledTouch) closedModal = true;
        }
        else if ((activeModal >= 20 && activeModal <= 23) || activeModal == 51) {
          int tIdx = (activeModal == 51) ? 0 : activeModal - 20;
          if (touchY < 70) { handledTouch = true; }
          else if (touchY >= 70 && touchY < 195) {
              int r = (touchY - 70) / 31; 
              if (r < 0) r = 0; if (r > 3) r = 3;
              
              int c = (touchX - 45) / 27; 
              if (touchX > 275) c = 9; 
              if (c < 0) c = 0; if (c > 9) c = 9;
              
              char key = kbRows[r][c];
              if (key == '<') {
                  if (kbInput.length() > 0) kbInput.remove(kbInput.length()-1);
              } else {
                  int maxLen = (kbMode == 0) ? 10 : ((kbMode == 3) ? 20 : 6);
                  if (kbInput.length() < maxLen) kbInput += key;
              }
              updateKeyboardInputBox(); 
              handledTouch = true;
          }
          else if (touchY >= 195) {
             if (touchX < 125) { 
                closedModal = true; 
                if(activeModal != 51) { returnToTool = true; targetTool = tIdx; }
             }
             else if (touchX >= 125 && touchX < 230) { 
                if ((kbMode == 0 || kbMode == 3) && kbInput.length() < ((kbMode == 3) ? 20 : 10)) { 
                    kbInput += " "; updateKeyboardInputBox(); 
                }
                handledTouch = true;
             }
             else if (touchX >= 230) { 
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
        else if (activeModal == 50) { // IP ADDRESS NUMPAD
          if (touchY < 70) { handledTouch = true; }
          else if (touchY >= 70 && touchY < 200) {
              int r = (touchY - 75) / 32;
              int c = (touchX - 70) / 60;
              if (r >= 0 && r < 4 && c >= 0 && c < 3) {
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
                  updateKeyboardInputBox();
              }
              handledTouch = true;
          }
          else if (touchY >= 200) {
              if (touchX < 160) { 
                  closedModal = true; 
              } else { 
                  kbInput.toCharArray(printerIP, sizeof(printerIP));
                  preferences.putString("printer_ip", printerIP);
                  showToast("Saved", "Printer IP Updated!");
                  closedModal = true; 
              }
              handledTouch = true;
          }
        }
        else if (activeModal >= 30 && activeModal <= 33) {
          int tIdx = activeModal - 30;
          if (touchY >= 40 && touchY < 155) {
              int r = (touchY - 40) / 55;
              if (r < 0) r = 0; if (r > 1) r = 1;
              int c = (touchX - 42) / 55;
              if (c < 0) c = 0; if (c > 4) c = 4;
              
              int i = r * 5 + c;
              if (i >= 0 && i < 10) {
                  filColor[tIdx] = FIL_COLORS[i];
                  preferences.putString(("filColor" + String(tIdx)).c_str(), rgb565ToHex(filColor[tIdx]));
                  syncFilamentToKlipper(tIdx);
                  showToast("Saved", "Tool " + String(tIdx+1) + " color updated");
                  closedModal = true; returnToTool = true; targetTool = tIdx; 
              }
          }
          else if (touchY >= 155 && touchY < 195) { 
              kbMode = 1; 
              kbInput = "";
              activeModal = 20 + tIdx; 
              drawKeyboardModal(tIdx);
              return; 
          }
          else if (touchY >= 195) { 
              closedModal = true; returnToTool = true; targetTool = tIdx; 
          }
        }
        else if (activeModal == 40) { 
          if (touchY >= 40 && touchY < 70 && touchX < 200) { optBedLevel = !optBedLevel; drawPrintOptionsModal(); handledTouch=true; }
          else if (touchY >= 70 && touchY < 100 && touchX < 200) { optTimelapse = !optTimelapse; drawPrintOptionsModal(); handledTouch=true; }
          else if (touchY >= 100 && touchY < 130 && touchX < 200) { optAdvMap = !optAdvMap; drawPrintOptionsModal(); handledTouch=true; }
          else if (touchY >= 140 && touchY < 195) {
             int i = (touchX - 50) / 55;
             if(i >= 0 && i < 4) { optT0 = i; drawPrintOptionsModal(); }
             handledTouch=true;
          }
          else if (touchY >= 195) {
             if (touchX < 145) { 
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
          }
        }
        if (!modalForceClosed) return; 
      }

      if (touchX < 42) {
        int tappedTab = touchY / 48; 
        if (tappedTab < 5) {
          if (tappedTab != currentTab || modalForceClosed) {
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
          }
        }
        return;
      } 
      
      if (currentTab == 0) {
        if (touchX > 48 && touchX < 178 && touchY > 22 && touchY < 76) { activeModal = 1; drawTempModal(1); }
        else if (touchX > 184 && touchX < 314 && touchY > 22 && touchY < 76) { activeModal = 8; drawActiveToolModal(); }
        else if (touchX > 48 && touchX < 178 && touchY > 82 && touchY < 136) { activeModal = 2; drawTempModal(2); }
        else if (touchX > 184 && touchX < 314 && touchY > 82 && touchY < 136) { activeModal = 7; drawSpeedFanModal(); }
        
        else if (touchX > 48 && touchX < 178 && touchY > 174 && touchY < 226) {
          if (currentState == "paused") { sendGcode("RESUME"); showToast("Command", "Resuming print..."); }
          else { sendGcode("PAUSE"); showToast("Command", "Pausing print..."); }
        }
        else if (touchX > 184 && touchX < 314 && touchY > 174 && touchY < 226) { sendGcode("CANCEL_PRINT"); showToast("Command", "Cancelling print...", true); }
      }
      else if (currentTab == 1) {
        for (int i=0; i<4; i++) {
           int bx = 45 + (i * 69);
           if (touchX > bx && touchX < bx + 64 && touchY > 40 && touchY < 200) {
               activeModal = 10 + i; 
               drawToolModal(i);
               break;
           }
        }
      }
      else if (currentTab == 2) {
        if (touchY > 20 && touchY < 70) {
          if (touchX > 60 && touchX < 110) moveDist = 0.1;
          if (touchX > 120 && touchX < 170) moveDist = 1.0;
          if (touchX > 180 && touchX < 230) moveDist = 10.0;
          if (touchX > 240 && touchX < 290) moveDist = 50.0;
          drawMoveTab(); 
        } else {
          if (touchX > 125 && touchX < 165 && touchY > 90 && touchY < 130) { sendGcode("G91\nG1 Y" + String(moveDist) + " F6000\nG90"); showToast("Jog", "Y+ " + String(moveDist)); }
          if (touchX > 125 && touchX < 165 && touchY > 190 && touchY < 230) { sendGcode("G91\nG1 Y-" + String(moveDist) + " F6000\nG90"); showToast("Jog", "Y- " + String(moveDist)); }
          if (touchX > 75 && touchX < 115 && touchY > 140 && touchY < 180) { sendGcode("G91\nG1 X-" + String(moveDist) + " F6000\nG90"); showToast("Jog", "X- " + String(moveDist)); }
          if (touchX > 175 && touchX < 215 && touchY > 140 && touchY < 180) { sendGcode("G91\nG1 X" + String(moveDist) + " F6000\nG90"); showToast("Jog", "X+ " + String(moveDist)); }
          if (touchX > 125 && touchX < 165 && touchY > 140 && touchY < 180) { sendGcode("G28 X Y"); showToast("Homing", "X and Y axis"); }
          if (touchX > 240 && touchX < 290 && touchY > 90 && touchY < 130) { sendGcode("G91\nG1 Z" + String(moveDist) + " F600\nG90"); showToast("Jog", "Z+ " + String(moveDist)); }
          if (touchX > 240 && touchX < 290 && touchY > 190 && touchY < 230) { sendGcode("G91\nG1 Z-" + String(moveDist) + " F600\nG90"); showToast("Jog", "Z- " + String(moveDist)); }
        }
      }
      else if (currentTab == 3) {
        if (touchY < 40) {
            if (touchX > 50 && touchX < 155 && printTabSource != 0) { 
                printTabSource = 0; fileScrollIndex = 0; fetchKlipperFiles(); drawPrintTab(); 
                showToast("File Source", "Loaded Printer Klipper Files");
            }
            if (touchX >= 160 && touchX < 265 && printTabSource != 1) { 
                printTabSource = 1; fileScrollIndex = 0; loadFilesFromSD(); drawPrintTab(); 
                showToast("File Source", "Loaded Screen SD Files");
            }
            return;
        }

        int count = printTabSource == 0 ? klipperFileCount : fileCount;
        String* list = printTabSource == 0 ? klipperFileList : fileList;

        if (touchX > 260) {
           if (touchY < 130 && fileScrollIndex > 0) { fileScrollIndex--; drawPrintTab(); }
           if (touchY > 130 && fileScrollIndex + 4 < count) { fileScrollIndex++; drawPrintTab(); }
        } else if (touchX > 50 && touchX < 250) {
           int i = (touchY - 45) / 45;
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
        if (touchX > 48 && touchX < 314 && touchY > 125 && touchY < 165) {
            kbInput = String(printerIP);
            kbMode = 2; // IP Mode
            activeModal = 50; 
            drawKeyboardModal(0);
        }
        else if (touchX > 184 && touchX < 314 && touchY > 125 && touchY < 165) {
            kbInput = String(printerName);
            kbMode = 3; // Name Mode
            activeModal = 51; 
            drawKeyboardModal(0);
        }
        else if (touchX > 48 && touchX < 178 && touchY > 175 && touchY < 215) {
          touchCalibrate();
          tft.fillScreen(BG_COLOR);
          drawSidebar();
          drawSettingsTab();
        }
        else if (touchX > 184 && touchX < 314 && touchY > 175 && touchY < 215) {
          tft.fillScreen(BG_COLOR);
          tft.setTextColor(TFT_YELLOW);
          tft.setTextDatum(MC_DATUM);
          tft.drawString("Resetting Wi-Fi...", 160, 100, 2);
          delay(1500);
          WiFiManager wm;
          wm.resetSettings();
          ESP.restart();
        }
        else if (touchX > 48 && touchX < 200 && touchY > 220) {
          showToast("Nates Print Shop", "Custom Firmware v1.0 Active");
        }
      }
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
          else if (activeModal >= 10 && activeModal <= 13) drawToolModal(activeModal - 10);
          else if ((activeModal >= 20 && activeModal <= 23) || activeModal == 50 || activeModal == 51) drawKeyboardModal(activeModal >= 20 && activeModal <= 23 ? activeModal - 20 : 0);
          else if (activeModal >= 30 && activeModal <= 33) drawColorPickerModal(activeModal - 30);
          else if (activeModal == 40) drawPrintOptionsModal();
      } else {
          tft.fillScreen(BG_COLOR);
          drawSidebar();
          if (currentTab == 0) drawHomeTab();
          else if (currentTab == 1) drawToolheadTab();
          else if (currentTab == 2) drawMoveTab();
          else if (currentTab == 3) drawPrintTab();
          else if (currentTab == 4) drawSettingsTab();
      }
  }

  if (millis() - lastUpdate > updateInterval) {
    if (WiFi.status() == WL_CONNECTED) {
      fetchPrinterData();
      if (currentTab == 0 || currentTab == 1) updateDynamicUI(); 
    }
    lastUpdate = millis();
  }
}

void drawCornerMarker(int corner, uint16_t color) {
  int w = tft.width();
  int h = tft.height();
  if (corner == 0) { tft.drawFastHLine(0, 0, 15, color); tft.drawLine(0, 0, 15, 15, color); tft.drawFastVLine(0, 0, 15, color); } 
  else if (corner == 1) { tft.drawFastHLine(0, h - 1, 15, color); tft.drawLine(0, h - 1, 15, h - 16, color); tft.drawFastVLine(0, h - 16, 15, color); } 
  else if (corner == 2) { tft.drawFastHLine(w - 16, 0, 15, color); tft.drawLine(w - 16, 15, w - 1, 0, color); tft.drawFastVLine(w - 1, 0, 15, color); } 
  else if (corner == 3) { tft.drawFastHLine(w - 16, h - 1, 15, color); tft.drawLine(w - 16, h - 16, w - 1, h - 1, color); tft.drawFastVLine(w - 1, h - 16, 15, color); }
}

void touchCalibrate() {
  long val[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  touch.setCal(0, 4095, 0, 4095, 320, 240, 0);

  tft.fillScreen(BG_COLOR);
  tft.setTextColor(TFT_WHITE, BG_COLOR);
  tft.setTextDatum(MC_DATUM); 
  tft.drawString("Touch corners as indicated", 160, 120, 2);

  for (int i = 0; i < 4; i++) {
    drawCornerMarker(i, TFT_RED);
    if (i > 0) delay(1000); 

    for (int j = 0; j < 8; j++) {
      while (!touch.Pressed()) { server.handleClient(); yield(); }
      val[i*2] += touch.RawX();
      val[i*2+1] += touch.RawY();
      while (touch.Pressed()) { server.handleClient(); yield(); } 
      delay(20);
    }
    val[i*2] /= 8;
    val[i*2+1] /= 8;
    drawCornerMarker(i, BG_COLOR); 
  }

  bool xyswap = false;
  int cal_x1, cal_x2, cal_y1, cal_y2;

  if (abs(val[0] - val[2]) > abs(val[1] - val[3])) {
    xyswap = true;
    cal_x1 = (val[1] + val[3]) / 2; cal_x2 = (val[5] + val[7]) / 2;
    cal_y1 = (val[0] + val[4]) / 2; cal_y2 = (val[2] + val[6]) / 2;
  } else {
    xyswap = false;
    cal_x1 = (val[0] + val[2]) / 2; cal_x2 = (val[4] + val[6]) / 2;
    cal_y1 = (val[1] + val[5]) / 2; cal_y2 = (val[3] + val[7]) / 2;
  }

  if (cal_x1 > cal_x2) { int tmp = cal_x1; cal_x1 = cal_x2; cal_x2 = tmp; }
  if (cal_y1 > cal_y2) { int tmp = cal_y1; cal_y1 = cal_y2; cal_y2 = tmp; }

  if(cal_x1 == 0) cal_x1 = 1; if(cal_x2 == 0) cal_x2 = 1;
  if(cal_y1 == 0) cal_y1 = 1; if(cal_y2 == 0) cal_y2 = 1;

  touch.setCal(cal_x1, cal_x2, cal_y1, cal_y2, 320, 240, xyswap);

  preferences.putInt("calXMin", cal_x1); preferences.putInt("calXMax", cal_x2);
  preferences.putInt("calYMin", cal_y1); preferences.putInt("calYMax", cal_y2);
  preferences.putBool("swapXY", xyswap); preferences.putBool("isCalibrated", true);
  
  if (sdCardReady) {
    File file = SD.open("/calibration.json", FILE_WRITE);
    if (file) {
      JsonDocument doc;
      doc["calXMin"] = cal_x1; doc["calXMax"] = cal_x2;
      doc["calYMin"] = cal_y1; doc["calYMax"] = cal_y2; doc["swapXY"] = xyswap;
      serializeJson(doc, file); file.close();
    }
  }

  isCalibrated = true;
  tft.fillScreen(BG_COLOR);
  tft.setTextColor(TFT_GREEN, BG_COLOR); 
  tft.drawString("CALIBRATION SAVED!", 160, 120, 2);
  tft.setTextDatum(TL_DATUM);
  delay(1200);
}

void drawSidebar() {
  tft.fillRect(0, 0, 42, 240, SIDEBAR_COLOR);
  tft.setTextSize(1);
  for (int i = 0; i < 5; i++) {
    int y = i * 48;
    if (i == currentTab) {
      tft.fillRect(0, y, 4, 48, ACCENT_CYAN); 
      tft.setTextColor(TFT_WHITE, SIDEBAR_COLOR);
    } else {
      tft.setTextColor(TEXT_GRAY, SIDEBAR_COLOR);
    }
    tft.setTextDatum(MC_DATUM);
    if (i == 0) tft.drawString("H", 23, y + 24, 2); 
    if (i == 1) tft.drawString("T", 23, y + 24, 2); 
    if (i == 2) tft.drawString("M", 23, y + 24, 2); 
    if (i == 3) tft.drawString("P", 23, y + 24, 2); 
    if (i == 4) tft.drawString("S", 23, y + 24, 2); 
  }
}

void drawHomeTab() {
  tft.fillRect(42, 0, 278, 240, BG_COLOR);
  tft.setTextSize(1);

  tft.fillRoundRect(48, 22, 130, 54, 6, CARD_COLOR);
  tft.drawRoundRect(48, 22, 130, 54, 6, tft.color565(50, 55, 65));
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TEXT_GRAY, CARD_COLOR);
  tft.drawString("BED TEMP", 113, 29, 1);

  tft.fillRoundRect(184, 22, 130, 54, 6, CARD_COLOR);
  tft.drawRoundRect(184, 22, 130, 54, 6, tft.color565(50, 55, 65));
  tft.drawString("ACTIVE TOOL", 249, 29, 1);

  tft.fillRoundRect(48, 82, 130, 54, 6, CARD_COLOR);
  tft.drawRoundRect(48, 82, 130, 54, 6, tft.color565(50, 55, 65));
  tft.drawString("TOOL TEMP", 113, 89, 1);

  tft.fillRoundRect(184, 82, 130, 54, 6, CARD_COLOR);
  tft.drawRoundRect(184, 82, 130, 54, 6, tft.color565(50, 55, 65));
  tft.drawString("SPEED / FAN", 249, 89, 1);

  tft.fillRoundRect(48, 174, 130, 52, 6, BTN_BLUE);
  tft.setTextColor(TFT_WHITE, BTN_BLUE); tft.setTextDatum(MC_DATUM);
  tft.drawString("II Pause", 113, 200, 2);

  tft.fillRoundRect(184, 174, 130, 52, 6, BTN_RED);
  tft.setTextColor(tft.color565(255, 180, 180), BTN_RED);
  tft.drawString("Stop", 249, 200, 2);

  tft.setTextDatum(TL_DATUM); 
  updateDynamicUI();
}

void drawTempModal(int type) {
  tft.fillRect(42, 0, 278, 240, BG_COLOR); 
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, BG_COLOR);
  
  String title = "Set Temperature";
  if (type == 1) title = "Set Bed Temp";
  else if (type == 2) title = "Set Active Tool Temp";
  else if (type >= 10) title = "Set Tool " + String(type - 9) + " Temp";
  
  tft.drawString(title, 42 + 278/2, 20, 2);

  const int* vals = (type == 1) ? presetBed : presetTool;

  for(int i = 0; i < 6; i++) {
    int r = i / 2;
    int c = i % 2;
    int x = 60 + c * 130;
    int y = 45 + r * 60;
    
    tft.fillRoundRect(x, y, 110, 45, 6, CARD_COLOR);
    tft.drawRoundRect(x, y, 110, 45, 6, ACCENT_CYAN);
    
    tft.setTextColor(TFT_WHITE, CARD_COLOR);
    if (vals[i] == 0) tft.drawString("OFF", x + 55, y + 22, 2);
    else tft.drawString(String(vals[i]) + "C", x + 55, y + 22, 2);
  }
  
  tft.setTextColor(TEXT_GRAY, BG_COLOR);
  tft.drawString("Tap anywhere else to cancel", 42 + 278/2, 230, 1);
  tft.setTextDatum(TL_DATUM);
}

void drawSpeedFanModal() {
  tft.fillRect(42, 0, 278, 240, BG_COLOR); 
  tft.setTextDatum(MC_DATUM);
  
  tft.setTextColor(TEXT_GRAY, BG_COLOR);
  tft.drawString("Print Speed", 42 + 278/2, 25, 2);
  
  int speeds[] = {50, 100, 150};
  for (int c = 0; c < 3; c++) {
    int x = 50 + c * 75;
    tft.fillRoundRect(x, 55, 65, 45, 6, CARD_COLOR);
    tft.drawRoundRect(x, 55, 65, 45, 6, ACCENT_CYAN);
    tft.setTextColor(TFT_WHITE, CARD_COLOR);
    tft.drawString(String(speeds[c]) + "%", x + 32, 77, 2);
  }

  tft.setTextColor(TEXT_GRAY, BG_COLOR);
  tft.drawString("Part Fan", 42 + 278/2, 115, 2);
  
  int fans[] = {0, 50, 100};
  for (int c = 0; c < 3; c++) {
    int x = 50 + c * 75;
    tft.fillRoundRect(x, 125, 65, 45, 6, CARD_COLOR);
    tft.drawRoundRect(x, 125, 65, 45, 6, ACCENT_CYAN);
    tft.setTextColor(TFT_WHITE, CARD_COLOR);
    tft.drawString(String(fans[c]) + "%", x + 32, 147, 2);
  }

  tft.setTextColor(TEXT_GRAY, BG_COLOR);
  tft.drawString("Tap anywhere else to cancel", 42 + 278/2, 230, 1);
  tft.setTextDatum(TL_DATUM);
}

void drawActiveToolModal() {
  tft.fillRect(42, 0, 278, 240, BG_COLOR); 
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, BG_COLOR);
  tft.drawString("Select Active Tool", 42 + 278/2, 20, 2);

  for(int i = 0; i < 4; i++) {
    int r = i / 2;
    int c = i % 2;
    int x = 60 + c * 130;
    int y = 55 + r * 70;
    
    tft.fillRoundRect(x, y, 110, 50, 6, CARD_COLOR);
    tft.drawRoundRect(x, y, 110, 50, 6, ACCENT_CYAN);
    
    tft.setTextColor(TFT_WHITE, CARD_COLOR);
    tft.drawString("Tool " + String(i + 1), x + 55, y + 25, 2);
  }
  
  tft.setTextColor(TEXT_GRAY, BG_COLOR);
  tft.drawString("Tap anywhere else to cancel", 42 + 278/2, 230, 1);
  tft.setTextDatum(TL_DATUM);
}

void drawToolModal(int idx) {
  tft.fillRect(42, 0, 278, 240, BG_COLOR);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, BG_COLOR);
  tft.drawString("Tool " + String(idx + 1) + " Settings", 42 + 278/2, 20, 2);

  String tempLbl[] = {"OFF", "200C", "220C", "240C"};
  for(int i=0; i<4; i++) {
    int x = 50 + (i * 65);
    tft.fillRoundRect(x, 40, 55, 35, 4, CARD_COLOR);
    tft.drawRoundRect(x, 40, 55, 35, 4, ACCENT_CYAN);
    tft.setTextColor(TFT_WHITE, CARD_COLOR);
    tft.drawString(tempLbl[i], x + 27, 40 + 17, 1);
  }

  // Type Box 
  tft.fillRoundRect(50, 90, 120, 35, 4, CARD_COLOR);
  tft.drawRoundRect(50, 90, 120, 35, 4, ACCENT_CYAN);
  tft.setTextColor(TFT_WHITE, CARD_COLOR);
  tft.drawString("Type: " + filType[idx], 50 + 60, 90 + 17, 1);

  // Color Box
  tft.fillRoundRect(180, 90, 120, 35, 4, CARD_COLOR);
  tft.drawRoundRect(180, 90, 120, 35, 4, ACCENT_CYAN);
  tft.drawString("Color", 180 + 40, 90 + 17, 1);
  tft.fillCircle(180 + 90, 90 + 17, 10, filColor[idx]);

  tft.fillRoundRect(50, 140, 120, 35, 4, toolAttached[idx] ? BTN_RED : BTN_GREEN);
  tft.setTextColor(TFT_WHITE, toolAttached[idx] ? BTN_RED : BTN_GREEN);
  tft.drawString(toolAttached[idx] ? "Detach" : "Attach", 50 + 60, 140 + 17, 2);

  tft.fillRoundRect(180, 140, 120, 35, 4, toolLoaded[idx] ? BTN_RED : BTN_GREEN);
  tft.setTextColor(TFT_WHITE, toolLoaded[idx] ? BTN_RED : BTN_GREEN);
  tft.drawString(toolLoaded[idx] ? "Unload" : "Load", 180 + 60, 140 + 17, 2);

  tft.fillRoundRect(50, 185, 250, 35, 4, BTN_BLUE);
  tft.setTextColor(TFT_WHITE, BTN_BLUE);
  tft.drawString("Close", 42 + 278/2, 185 + 17, 2);

  tft.setTextDatum(TL_DATUM);
}

void updateKeyboardInputBox() {
  tft.fillRoundRect(50, 35, 260, 30, 4, CARD_COLOR);
  tft.drawRoundRect(50, 35, 260, 30, 4, ACCENT_CYAN);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(ACCENT_CYAN, CARD_COLOR);
  tft.drawString(kbInput + "_", 42 + 278/2, 50, 2);
}

void drawKeyboardModal(int idx) {
  tft.fillRect(42, 0, 278, 240, BG_COLOR);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, BG_COLOR);
  
  if (kbMode == 0) {
      tft.drawString("Type Filament for Tool " + String(idx + 1), 42 + 278/2, 15, 2);
  } else if (kbMode == 1) {
      tft.drawString("Enter HEX Color (RRGGBB)", 42 + 278/2, 15, 2);
  } else if (kbMode == 2) {
      tft.drawString("Enter Printer IP Address", 42 + 278/2, 15, 2);
  } else if (kbMode == 3) {
      tft.drawString("Enter Printer Name", 42 + 278/2, 15, 2);
  }

  updateKeyboardInputBox();

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE);

  if (kbMode == 2) { // Numeric Keypad Mode for IP
      const char* numpad[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", ".", "0", "DEL"};
      for (int i=0; i<12; i++) {
          int r = i / 3;
          int c = i % 3;
          int bx = 70 + c * 60;
          int by = 75 + r * 32;
          tft.fillRoundRect(bx, by, 50, 28, 4, tft.color565(50, 55, 65));
          tft.drawString(numpad[i], bx + 25, by + 14, 2);
      }
  } else { // Standard QWERTY Mode
      for (int r=0; r<4; r++) {
        for (int c=0; c<10; c++) {
          char key = kbRows[r][c];
          int bx = 45 + (c * 27);
          int by = 75 + (r * 30);
          tft.fillRoundRect(bx, by, 25, 28, 4, tft.color565(50, 55, 65));
          
          if (key == '<') {
              tft.drawString("DEL", bx + 12, by + 14, 1);
          } else {
              char lbl[2] = {key, '\0'};
              tft.drawString(lbl, bx + 12, by + 14, 2);
          }
        }
      }
  }

  if (kbMode == 2) {
      tft.fillRoundRect(48, 205, 100, 30, 4, BTN_RED);
      tft.drawString("CANCEL", 48+50, 205+15, 2);
      tft.fillRoundRect(172, 205, 100, 30, 4, BTN_BLUE);
      tft.drawString("SAVE", 172+50, 205+15, 2);
  } else {
      tft.fillRoundRect(48, 200, 70, 30, 4, BTN_RED);
      tft.drawString("CANCEL", 48+35, 200+15, 1);

      if (kbMode == 0 || kbMode == 3) {
        tft.fillRoundRect(128, 200, 100, 30, 4, tft.color565(50, 55, 65));
        tft.drawString("SPACE", 128+50, 200+15, 1);
      }

      tft.fillRoundRect(238, 200, 70, 30, 4, BTN_BLUE);
      tft.drawString("SAVE", 238+35, 200+15, 2);
  }
  
  tft.setTextDatum(TL_DATUM);
}

void drawColorPickerModal(int idx) {
  tft.fillRect(42, 0, 278, 240, BG_COLOR);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, BG_COLOR);
  tft.drawString("Select Color for Tool " + String(idx + 1), 42 + 278/2, 15, 2);

  for (int i=0; i<10; i++) {
    int r = i / 5;
    int c = i % 5;
    int bx = 50 + (c * 50);
    int by = 45 + (r * 55);
    
    tft.fillRoundRect(bx, by, 40, 45, 6, FIL_COLORS[i]);
    if (filColor[idx] == FIL_COLORS[i]) {
        tft.drawRoundRect(bx-2, by-2, 44, 49, 6, ACCENT_CYAN);
        tft.drawRoundRect(bx-1, by-1, 42, 47, 6, ACCENT_CYAN);
    }
  }

  tft.fillRoundRect(50, 160, 260, 32, 4, tft.color565(40, 60, 90));
  tft.setTextColor(ACCENT_CYAN);
  tft.drawString("Custom HEX...", 42 + 278/2, 160 + 16, 2);

  tft.fillRoundRect(50, 200, 260, 32, 4, tft.color565(50, 55, 65));
  tft.setTextColor(TFT_WHITE);
  tft.drawString("Cancel", 42 + 278/2, 200 + 16, 2);
  
  tft.setTextDatum(TL_DATUM);
}

void drawPrintOptionsModal() {
  tft.fillRect(42, 0, 278, 240, BG_COLOR);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, BG_COLOR);
  String headerStr = "Launch: " + selectedFile;
  if(headerStr.length() > 28) headerStr = headerStr.substring(0, 25) + "...";
  tft.drawString(headerStr, 42 + 278/2, 20, 2);

  tft.setTextDatum(TL_DATUM);

  tft.drawRect(50, 45, 20, 20, TEXT_GRAY);
  if(optBedLevel) tft.fillRect(53, 48, 14, 14, ACCENT_CYAN);
  tft.drawString("Auto Bed Leveling", 80, 48, 2);

  tft.drawRect(50, 75, 20, 20, TEXT_GRAY);
  if(optTimelapse) tft.fillRect(53, 78, 14, 14, ACCENT_CYAN);
  tft.drawString("Record Timelapse", 80, 78, 2);

  tft.drawRect(50, 105, 20, 20, TEXT_GRAY);
  if(optAdvMap) tft.fillRect(53, 108, 14, 14, ACCENT_CYAN);
  tft.drawString("Adv Tool Mapping", 80, 108, 2);

  tft.drawString("Map Main Extruder (T0) To:", 50, 135, 1);
  for(int i=0; i<4; i++) {
     int bx = 50 + (i*55);
     tft.fillRoundRect(bx, 145, 50, 45, 4, optT0 == i ? ACCENT_CYAN : tft.color565(50,55,65));
     tft.setTextColor(optT0 == i ? BG_COLOR : TFT_WHITE);
     tft.setTextDatum(MC_DATUM);
     tft.drawString("T"+String(i+1), bx+25, 155, 2);
     
     String fType = filType[i];
     if(fType.length() > 6) fType = fType.substring(0, 6);
     tft.drawString(fType, bx+25, 170, 1);
     tft.fillCircle(bx+25, 180, 4, filColor[i]);
  }

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE);
  tft.fillRoundRect(50, 195, 90, 35, 4, BTN_RED);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("CANCEL", 95, 212, 1);

  tft.fillRoundRect(150, 195, 150, 35, 4, BTN_GREEN);
  tft.drawString("LAUNCH PRINT", 225, 212, 2);

  tft.setTextDatum(TL_DATUM);
}

void drawToolheadTab() {
  tft.fillRect(42, 0, 278, 240, BG_COLOR);
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  
  for (int i = 0; i < 4; i++) {
    int bx = 45 + (i * 69); 
    tft.fillRoundRect(bx, 40, 64, 160, 6, CARD_COLOR);
  }
  updateDynamicUI();
}

void drawMoveTab() {
  tft.fillRect(42, 0, 278, 240, BG_COLOR);
  tft.setTextSize(1); tft.setTextColor(TFT_WHITE, BG_COLOR);
  tft.setCursor(55, 5); tft.print("Move & Home");

  String distLabels[] = {"0.1", "1", "10", "50"};
  for (int i=0; i<4; i++) {
    int x = 55 + (i * 60);
    bool active = false;
    if (i == 0 && moveDist == 0.1) active = true;
    if (i == 1 && moveDist == 1.0) active = true;
    if (i == 2 && moveDist == 10.0) active = true;
    if (i == 3 && moveDist == 50.0) active = true;

    tft.fillRoundRect(x, 25, 50, 30, 4, active ? ACCENT_CYAN : CARD_COLOR);
    tft.setTextColor(active ? BG_COLOR : TFT_WHITE, active ? ACCENT_CYAN : CARD_COLOR);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(distLabels[i], x + 25, 40, 2);
  }

  tft.setTextColor(TFT_WHITE, CARD_COLOR);
  tft.fillRoundRect(120, 130, 40, 40, 6, CARD_COLOR); tft.drawString("H", 140, 150, 2);
  tft.fillRoundRect(120, 80, 40, 40, 6, CARD_COLOR); tft.drawString("Y+", 140, 100, 2);
  tft.fillRoundRect(120, 180, 40, 40, 6, CARD_COLOR); tft.drawString("Y-", 140, 200, 2);
  tft.fillRoundRect(70, 130, 40, 40, 6, CARD_COLOR); tft.drawString("X-", 90, 150, 2);
  tft.fillRoundRect(170, 130, 40, 40, 6, CARD_COLOR); tft.drawString("X+", 190, 150, 2);
  tft.fillRoundRect(235, 80, 50, 40, 6, CARD_COLOR); tft.drawString("Z+", 260, 100, 2);
  tft.fillRoundRect(235, 180, 50, 40, 6, CARD_COLOR); tft.drawString("Z-", 260, 200, 2);
  tft.setTextDatum(TL_DATUM);
}

void drawPrintTab() {
  tft.fillRect(42, 0, 278, 240, BG_COLOR);
  tft.setTextDatum(TL_DATUM); 
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, BG_COLOR);

  tft.fillRoundRect(50, 5, 105, 26, 4, printTabSource == 0 ? ACCENT_CYAN : CARD_COLOR);
  tft.setTextColor(printTabSource == 0 ? BG_COLOR : TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Printer Files", 102, 18, 1);

  tft.fillRoundRect(165, 5, 95, 26, 4, printTabSource == 1 ? ACCENT_CYAN : CARD_COLOR);
  tft.setTextColor(printTabSource == 1 ? BG_COLOR : TFT_WHITE);
  tft.drawString("Screen SD", 212, 18, 1);
  
  tft.setTextDatum(TL_DATUM);

  int count = printTabSource == 0 ? klipperFileCount : fileCount;
  String* list = printTabSource == 0 ? klipperFileList : fileList;

  if (count == 0) {
    tft.setTextColor(TEXT_GRAY);
    tft.drawString("No files found.", 55, 50, 1);
    return;
  }

  for (int i=0; i<4; i++) {
     int idx = fileScrollIndex + i;
     if (idx >= count) break;
     int by = 45 + (i * 45);
     tft.fillRoundRect(50, by, 200, 40, 4, CARD_COLOR);
     tft.setTextColor(TFT_WHITE, CARD_COLOR);
     String fn = list[idx];
     if(fn.length() > 28) fn = fn.substring(0, 25) + "...";
     tft.drawString(fn, 55, by + 12, 1);
  }

  tft.fillRoundRect(260, 45, 45, 85, 4, tft.color565(50, 55, 65));
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("UP", 282, 87, 2);
  
  tft.fillRoundRect(260, 135, 45, 85, 4, tft.color565(50, 55, 65));
  tft.drawString("DN", 282, 177, 2);
  tft.setTextDatum(TL_DATUM);
}

void drawSettingsTab() {
  tft.fillRect(42, 0, 278, 240, BG_COLOR);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, BG_COLOR);
  tft.drawString("SYS_CONFIG", 55, 6, 2);

  tft.setTextColor(TEXT_GRAY, BG_COLOR);
  tft.setCursor(55, 28);  tft.print("Printer IP: " + String(printerIP));
  tft.setCursor(55, 44);  tft.print("WiFi: " + WiFi.SSID() + " (" + String(WiFi.RSSI()) + "dBm)");
  tft.setCursor(55, 60);  tft.print("Display IP: " + WiFi.localIP().toString());
  tft.setCursor(55, 76);  tft.print("URL: http://" + String(mdnsName) + ".local");
  tft.setCursor(55, 92);  tft.print("MAC: " + WiFi.macAddress());

  if (sdCardReady) {
    tft.setTextColor(TFT_GREEN, BG_COLOR);
    tft.setCursor(55, 108); tft.print("SD Card: " + String((unsigned long)sdCardTotal) + " MB Total");
  } else {
    tft.setTextColor(TFT_RED, BG_COLOR);
    tft.setCursor(55, 108); tft.print("SD Card: Not Mounted");
  }

  // Edit IP Button
  tft.fillRoundRect(48, 125, 130, 40, 6, tft.color565(50, 55, 65));
  tft.setTextColor(TFT_WHITE, tft.color565(50, 55, 65)); 
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Edit IP", 113, 145, 2);

  // Edit Name Button
  tft.fillRoundRect(184, 125, 130, 40, 6, tft.color565(50, 55, 65));
  tft.drawString("Edit Name", 249, 145, 2);

  tft.fillRoundRect(48, 175, 130, 40, 6, BTN_BLUE); 
  tft.setTextColor(TFT_WHITE, BTN_BLUE); 
  tft.drawString("Calibrate", 113, 195, 2);

  tft.fillRoundRect(184, 175, 130, 40, 6, tft.color565(40, 60, 90)); 
  tft.setTextColor(ACCENT_CYAN, tft.color565(40, 60, 90));
  tft.drawString("Reset WiFi", 249, 195, 2);
  
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(ACCENT_CYAN, BG_COLOR);
  tft.drawString("v1.0 Firmware by Nates Print Shop", 48, 225, 1);
}

void updateDynamicUI() {
  if (activeModal > 0) return; 

  tft.setTextSize(1);
  tft.setTextPadding(0);

  if (currentTab == 0) {
    tft.fillRect(48, 2, 266, 18, BG_COLOR);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_WHITE, BG_COLOR);
    String headerStr = String(printerName) + " : " + currentState;
    if (headerStr.length() > 25) headerStr = headerStr.substring(0, 23) + "..";
    tft.drawString(headerStr, 48, 3, 2);

    tft.setTextDatum(MC_DATUM);

    tft.fillRect(50, 44, 126, 30, CARD_COLOR);
    tft.setTextColor(TFT_WHITE, CARD_COLOR);
    tft.drawString(String(bedTemp, 1) + "C", 113, 59, 2);
    
    // Active Tool Block
    tft.fillRect(186, 44, 126, 30, CARD_COLOR);
    tft.setTextColor(ACCENT_CYAN, CARD_COLOR);
    if (activeTool >= 0 && activeTool <= 3) {
        tft.drawString("Tool " + String(activeTool + 1), 249, 59, 2);
    } else {
        tft.setTextColor(TEXT_GRAY, CARD_COLOR);
        tft.drawString("None", 249, 59, 2);
    }
    
    // Tool Temp Block
    tft.fillRect(50, 104, 126, 30, CARD_COLOR);
    if (activeTool >= 0 && activeTool <= 3) {
        tft.setTextColor(toolTarget[activeTool] > 0 ? tft.color565(255, 120, 120) : TFT_WHITE, CARD_COLOR);
        tft.drawString(String(toolTemp[activeTool], 1) + "C", 113, 119, 2);
    } else {
        tft.setTextColor(TEXT_GRAY, CARD_COLOR);
        tft.drawString("-- C", 113, 119, 2);
    }
    
    tft.fillRect(186, 104, 126, 30, CARD_COLOR);
    tft.setTextColor(TFT_WHITE, CARD_COLOR);
    tft.drawString(String(printSpeed) + "% / " + String(fanSpeed) + "%", 249, 119, 2);

    tft.fillRect(48, 142, 266, 26, BG_COLOR);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TEXT_GRAY, BG_COLOR);
    String shortFile = currentFile;
    int lastSlash = shortFile.lastIndexOf('/');
    if (lastSlash >= 0) shortFile = shortFile.substring(lastSlash + 1);
    if (shortFile.length() > 20) shortFile = shortFile.substring(0, 17) + "...";
    tft.drawString(shortFile, 48, 142, 1);
    
    tft.setTextDatum(TR_DATUM);
    tft.drawString(String(printProgress, 1) + "%", 314, 142, 1);

    int barWidth = 266;
    int fillWidth = (int)((printProgress / 100.0) * barWidth);
    fillWidth = constrain(fillWidth, 0, barWidth);
    
    tft.drawRect(48, 156, barWidth, 10, tft.color565(50, 55, 65)); 
    if(fillWidth > 0) tft.fillRect(49, 157, fillWidth, 8, ACCENT_CYAN); 

    tft.setTextDatum(TL_DATUM); 
  } 
  else if (currentTab == 1) {
    tft.fillRect(48, 2, 266, 18, BG_COLOR);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_WHITE, BG_COLOR);
    tft.drawString(String(printerName) + " : " + currentState, 48, 3, 2);

    tft.setTextDatum(MC_DATUM);

    for (int i = 0; i < 4; i++) {
      int bx = 45 + (i * 69);
      
      tft.drawRoundRect(bx, 40, 64, 160, 6, (activeTool == i) ? ACCENT_CYAN : tft.color565(50, 55, 65));
      
      tft.setTextColor(TEXT_GRAY, CARD_COLOR);
      tft.drawString(String(i + 1), bx + 32, 40 + 15, 1);

      tft.fillCircle(bx + 32, 40 + 60, 15, filColor[i]);

      tft.fillRect(bx + 2, 40 + 90, 60, 20, CARD_COLOR);
      tft.setTextColor(TFT_WHITE, CARD_COLOR);
      tft.drawString(filType[i], bx + 32, 40 + 100, 1);

      tft.fillRect(bx + 2, 40 + 125, 60, 25, CARD_COLOR);
      tft.setTextColor(toolTarget[i] > 0 ? tft.color565(255, 120, 120) : TEXT_GRAY, CARD_COLOR);
      tft.drawString(String(toolTemp[i], 1) + "C", bx + 32, 40 + 135, 1);
    }
    tft.setTextDatum(TL_DATUM);
  }
}

void fetchPrinterData() {
  HTTPClient http;
  String url = "http://" + String(printerIP) + ":7125/printer/objects/query?print_stats&toolhead&extruder&extruder1&extruder2&extruder3&heater_bed&fan&gcode_move&display_status&print_task_config";
  
  http.begin(url);
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String payload = http.getString(); 
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (!error) {
      JsonObject status = doc["result"]["status"];
      
      currentState = status["print_stats"]["state"].as<String>();
      currentFile = status["print_stats"]["filename"].as<String>();
      if (currentFile == "null" || currentFile == "") currentFile = "Idle";
      
      bedTemp = status["heater_bed"]["temperature"].as<float>();
      bedTarget = status["heater_bed"]["target"].as<float>();
      
      toolTemp[0] = status["extruder"]["temperature"].as<float>();
      toolTarget[0] = status["extruder"]["target"].as<float>();
      toolTemp[1] = status["extruder1"]["temperature"].as<float>();
      toolTarget[1] = status["extruder1"]["target"].as<float>();
      toolTemp[2] = status["extruder2"]["temperature"].as<float>();
      toolTarget[2] = status["extruder2"]["target"].as<float>();
      toolTemp[3] = status["extruder3"]["temperature"].as<float>();
      toolTarget[3] = status["extruder3"]["target"].as<float>();

      // ACTIVE SYNC FROM KLIPPER TO ESP32 MEMORY
      JsonObject taskConfig = status["print_task_config"];
      if (!taskConfig.isNull()) {
        JsonArray colors = taskConfig["filament_color_rgba"];
        JsonArray types = taskConfig["filament_type"];
        
        for (int i = 0; i < 4; i++) {
          if (!colors[i].isNull()) {
             String cStr = colors[i].as<String>();
             if (cStr.length() >= 6) {
                 filColor[i] = hexToRGB565(cStr);
                 preferences.putString(("filColor" + String(i)).c_str(), rgb565ToHex(filColor[i]));
             }
          }
          if (!types[i].isNull()) {
             String tStr = types[i].as<String>();
             if (tStr.length() > 0 && tStr != "null") {
                 filType[i] = tStr;
                 preferences.putString(("filType" + String(i)).c_str(), filType[i]);
             }
          }
        }
      }

      printSpeed = status["gcode_move"]["speed_factor"].as<float>() * 100.0;
      fanSpeed = status["fan"]["speed"].as<float>() * 100.0;
      printProgress = status["display_status"]["progress"].as<float>() * 100.0;
      
      String tHead = status["toolhead"]["extruder"].as<String>();
      if(tHead == "extruder") activeTool = 0;
      else if(tHead == "extruder1") activeTool = 1;
      else if(tHead == "extruder2") activeTool = 2;
      else if(tHead == "extruder3") activeTool = 3;
      else activeTool = -1; // "None" handler
    }
  }
  http.end();
}

void sendGcode(String command) {
  HTTPClient http;
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
  String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
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
  html += "input[type=text], input[type=number] { width: 100%; padding: 12px; margin: 8px 0 16px; box-sizing: border-box; background: #1a1d24; border: 1px solid #333; color: white; border-radius: 6px; } ";
  html += ".btn { width: 100%; padding: 14px; background: #00E5FF; color: black; border: none; border-radius: 6px; font-weight: bold; font-size: 16px; cursor: pointer; display: block; box-sizing: border-box; text-align: center; text-decoration: none; } ";
  html += "</style>";
  html += "<script>";
  html += "function sc(cmd, pmt) { let v = prompt(pmt); if(v !== null && v !== '') { document.getElementById('c_param').name = cmd; document.getElementById('c_param').value = v; document.getElementById('c_form').submit(); } }";
  html += "</script>";
  html += "</head><body><div class='container'>";
  
  html += "<h2 style='text-align:center; color:#00E5FF; text-transform:uppercase; letter-spacing:2px; font-family:monospace; margin-bottom:25px;'>U1 Command Terminal</h2><div class='nav'>";
  html += "<a href='/' class='" + String(activePage == "home" ? "active" : "") + "'>Dashboard</a>";
  html += "<a href='/tech' class='" + String(activePage == "tech" ? "active" : "") + "'>Hardware Diags</a></div>";
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

void handleTech() {
  String html = getWebHeader("tech");
  
  html += "<div class='card'><h3 style='margin-top:0;'>Hardware Diagnostics</h3>";
  html += "<p><b>Board Type:</b> ESP32-32E (LCDWIKI E32R28T)</p>";
  html += "<p><b>Screen Spec:</b> 2.8 inch TFT LCD (320 x 240 Resolution)</p>";
  html += "<p><b>Display Controller:</b> ILI9341 (HSPI Interface)</p>";
  html += "<p><b>Touch Controller:</b> XPT2046 (Resistive SPI)</p>";
  html += "<p><b>Target Printer IP:</b> " + String(printerIP) + "</p>";
  html += "<p><b>Wi-Fi Network:</b> " + WiFi.SSID() + "</p>";
  html += "<p><b>Signal Strength:</b> " + String(WiFi.RSSI()) + " dBm</p>";
  html += "<p><b>MAC Address:</b> " + WiFi.macAddress() + "</p>";
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
    html += "<input type='submit' value='Upload to Screen SD' class='btn' style='margin-top:5px; background:#00E5FF; color:black;'></form>";
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
  html += "<hr style='border:1px solid #333; margin:15px 0;'>";
  html += "<label style='color:#888; font-weight:bold; font-size:12px;'>NEW WI-FI SSID (Leave blank to keep current)</label><br>";
  html += "<input type='text' name='wifi_ssid' placeholder='Current: " + WiFi.SSID() + "'>";
  html += "<label style='color:#888; font-weight:bold; font-size:12px;'>NEW WI-FI PASSWORD</label><br>";
  html += "<input type='text' name='wifi_pass' placeholder='••••••••'>";
  html += "<input type='submit' value='Save & Reboot' class='btn' style='margin-top:10px;'></form>";
  html += "<a href='/calibrate' class='btn' style='background:#2A3B5C; color:#00E5FF; margin-top:15px;'>Recalibrate Touchscreen</a></div>";

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
  String newIP = server.arg("ip");
  String newName = server.arg("name");
  String newMDNS = server.arg("mdns");
  String newSSID = server.arg("wifi_ssid");
  String newPass = server.arg("wifi_pass");
  
  newIP.toCharArray(printerIP, sizeof(printerIP));
  newName.toCharArray(printerName, sizeof(printerName));
  newMDNS.toCharArray(mdnsName, sizeof(mdnsName));
  
  preferences.putString("printer_ip", printerIP);
  preferences.putString("printer_name", printerName);
  preferences.putString("mdns_name", mdnsName);
  
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