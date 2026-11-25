#include "kobra_backend.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "mbedtls/md5.h"
#include "mbedtls/aes.h"
#include "mbedtls/base64.h"
#include <esp_heap_caps.h>

// ---------------------------------------------------------------------------
// Shared state. Everything below is touched by the poll task; the web handlers
// (UI loop) only go through the locked accessors at the bottom.
// ---------------------------------------------------------------------------
static SemaphoreHandle_t kbMutex = NULL;
static KobraState kbState;
static volatile KobraLink kbLink = KB_PAIRING;
static String kbDetail = "";                 // last error / progress note
static volatile bool kbReloadCert = false;
static volatile bool kbForget = false;

static char kbDev[64] = "", kbUser[64] = "", kbPass[80] = "", kbModel[12] = "";
static char kbPairIp[32] = "";               // printer IP the saved login belongs to
static String kbCert, kbKey;                 // optional uploaded cert (fallback); must outlive the TLS client
static String kbDevCert, kbDevKey;           // the printer's own client cert, from pairing
static char kbClientId[16] = "";
static bool useUploaded = false;             // which cert the next login tries
static unsigned long lastAceQuery = 0;

// Commands queued by the UI, published by the poll task.
struct KbCmd { char type[16]; char action[16]; String data; };
static const int CMD_N = 8;
static KbCmd kbCmds[CMD_N];
static int kbCmdHead = 0, kbCmdCount = 0;

// Printer file list (newest first).
static const int FILES_MAX = 40;
static String kbFiles[FILES_MAX];
static int kbFileCount = 0;
static uint32_t kbFilesVersion = 0;
static String kbFilesError = "";

// Raw message log: one fixed buffer allocated at start (a String per message
// scattered the heap on boards without PSRAM). Entries are "<secs>s <topic>\n<payload>".
static char *kbLogMem = nullptr;
static int kbLogN = 0, kbLogW = 0;   // entries, bytes per entry
static int kbLogHead = 0;

static WiFiClientSecure *net = nullptr;
static PubSubClient *mqtt = nullptr;
static unsigned long lastAttempt = 0, backoff = 0, lastQuery = 0, downSince = 0, upSince = 0;
static bool wasUp = false;

static void lock() { if (kbMutex) xSemaphoreTake(kbMutex, portMAX_DELAY); }
static void unlock() { if (kbMutex) xSemaphoreGive(kbMutex); }

static void setLink(KobraLink l, const String &detail) {
  lock(); kbLink = l; kbDetail = detail; unlock();
}

static void logRaw(const char *topic, const uint8_t *payload, unsigned int len) {
  if (!kbLogMem) return;
  lock();
  char *e = kbLogMem + kbLogHead * kbLogW;
  int h = snprintf(e, kbLogW, "%lus %s\n", millis() / 1000, topic);
  if (h < 0 || h >= kbLogW) h = kbLogW - 1;
  int room = kbLogW - h - 24;   // keep space for the "...(N bytes)" note
  unsigned int n = room > 0 ? (len < (unsigned)room ? len : (unsigned)room) : 0;
  memcpy(e + h, payload, n);
  int end = h + n;
  if (len > n) end += snprintf(e + end, kbLogW - end, " ...(%u bytes)", len);
  e[end < kbLogW ? end : kbLogW - 1] = '\0';
  kbLogHead = (kbLogHead + 1) % kbLogN;
  unlock();
}

// ---------------------------------------------------------------------------
// Settings (own NVS namespace, so the poll task never shares the UI's
// Preferences object).
// ---------------------------------------------------------------------------
static void loadSettings() {
  Preferences p;
  p.begin("kobra", true);
  kbCert = p.getString("cert", "");
  kbKey = p.getString("key", "");
  strlcpy(kbDev, p.getString("dev", "").c_str(), sizeof(kbDev));
  strlcpy(kbUser, p.getString("user", "").c_str(), sizeof(kbUser));
  strlcpy(kbPass, p.getString("pass", "").c_str(), sizeof(kbPass));
  strlcpy(kbModel, p.getString("model", "").c_str(), sizeof(kbModel));
  strlcpy(kbPairIp, p.getString("ip", "").c_str(), sizeof(kbPairIp));
  kbDevCert = p.getString("dcert", "");
  kbDevKey = p.getString("dkey", "");
  p.end();
}

static void saveCreds() {
  Preferences p;
  p.begin("kobra", false);
  p.putString("dev", kbDev); p.putString("user", kbUser);
  p.putString("pass", kbPass); p.putString("model", kbModel);
  p.putString("dcert", kbDevCert); p.putString("dkey", kbDevKey);
  p.putString("ip", kbPairIp);
  p.end();
}

static void clearCreds() {
  kbDev[0] = kbUser[0] = kbPass[0] = '\0';
  Preferences p;
  p.begin("kobra", false);
  p.remove("dev"); p.remove("user"); p.remove("pass"); p.remove("dcert"); p.remove("dkey"); p.remove("ip"); p.remove("model");
  p.end();
  kbDevCert = ""; kbDevKey = ""; kbPairIp[0] = kbModel[0] = 0;
}

// ---------------------------------------------------------------------------
// Pairing: GET /info -> token; signed POST /ctrl -> AES-CBC blob with the MQTT
// credentials (same steps as KobraCommandCenter's KobraAuthService).
// ---------------------------------------------------------------------------
static String md5hex(const String &in) {
  uint8_t h[16];
  mbedtls_md5_context c;
  mbedtls_md5_init(&c);
  mbedtls_md5_starts_ret(&c);
  mbedtls_md5_update_ret(&c, (const uint8_t *)in.c_str(), in.length());
  mbedtls_md5_finish_ret(&c, h);
  mbedtls_md5_free(&c);
  char out[33];
  for (int i = 0; i < 16; i++) sprintf(out + i * 2, "%02x", h[i]);
  return String(out);
}

static uint64_t epochMs() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint64_t)tv.tv_sec * 1000ULL + tv.tv_usec / 1000;
}

static bool timeValid() { return time(nullptr) > 1700000000; }

static bool decryptCreds(const char *b64, const char *key16, const char *iv, String &out) {
  size_t inLen = strlen(b64), outLen = 0;
  size_t cap = inLen * 3 / 4 + 4;
  uint8_t *cipher = (uint8_t *)malloc(cap);
  uint8_t *plain = (uint8_t *)malloc(cap);
  bool ok = false;
  if (cipher && plain &&
      mbedtls_base64_decode(cipher, cap, &outLen, (const uint8_t *)b64, inLen) == 0 &&
      outLen > 0 && outLen % 16 == 0 && strlen(iv) >= 16) {
    uint8_t ivb[16];
    memcpy(ivb, iv, 16);
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    if (mbedtls_aes_setkey_dec(&aes, (const uint8_t *)key16, 128) == 0 &&
        mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, outLen, ivb, cipher, plain) == 0) {
      uint8_t pad = plain[outLen - 1];
      if (pad >= 1 && pad <= 16) {
        out = "";
        out.reserve(outLen);
        for (size_t i = 0; i < outLen - pad; i++) out += (char)plain[i];
        ok = true;
      }
    }
    mbedtls_aes_free(&aes);
  }
  free(cipher); free(plain);
  return ok;
}

static bool pair(const char *ip) {
  setLink(KB_PAIRING, "Pairing with printer...");
  if (!timeValid()) {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    for (int i = 0; i < 50 && !timeValid(); i++) vTaskDelay(pdMS_TO_TICKS(100));
  }

  HTTPClient http;
  http.setConnectTimeout(2000);
  http.setTimeout(4000);
  http.begin("http://" + String(ip) + ":18910/info");
  int code = http.GET();
  if (code != 200) { http.end(); setLink(KB_PAIR_FAIL, "No answer on port 18910 (HTTP " + String(code) + "). Turn on LAN Mode on the printer (Settings > Network)"); return false; }
  const String timeNote = timeValid() ? "" : " (no network time)";
  String token, modelName;
  {
    JsonDocument doc;
    if (deserializeJson(doc, http.getString())) { http.end(); setLink(KB_PAIR_FAIL, "Bad /info reply"); return false; }
    token = doc["token"] | "";
    modelName = doc["modelName"] | "";
  }
  http.end();
  if (token.length() < 32) { setLink(KB_PAIR_FAIL, "Printer gave no token"); return false; }

  char nonce[7];
  static const char cs[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  for (int i = 0; i < 6; i++) nonce[i] = cs[esp_random() % (sizeof(cs) - 1)];
  nonce[6] = '\0';
  char ts[24];
  snprintf(ts, sizeof(ts), "%llu", (unsigned long long)epochMs());
  String sign = md5hex(md5hex(token.substring(0, 16)) + ts + nonce);

  http.begin("http://" + String(ip) + ":18910/ctrl?ts=" + ts + "&nonce=" + nonce + "&sign=" + sign + "&did=random");
  http.addHeader("Content-Type", "application/json");
  code = http.POST("{}");
  if (code != 200) { http.end(); setLink(KB_PAIR_FAIL, "Pairing refused (HTTP " + String(code) + ")" + timeNote); return false; }
  String plain;
  {
    JsonDocument doc;
    if (deserializeJson(doc, http.getString())) { http.end(); setLink(KB_PAIR_FAIL, "Bad /ctrl reply"); return false; }
    const char *info = doc["data"]["info"] | "";
    const char *iv = doc["data"]["token"] | "";
    if (!info[0] || !iv[0]) { http.end(); setLink(KB_PAIR_FAIL, "Printer sent no credentials"); return false; }
    String key16 = token.substring(16, 32);
    if (!decryptCreds(info, key16.c_str(), iv, plain)) { http.end(); setLink(KB_PAIR_FAIL, "Could not decrypt credentials"); return false; }
  }
  http.end();

  JsonDocument creds;
  if (deserializeJson(creds, plain)) { setLink(KB_PAIR_FAIL, "Credentials not JSON"); return false; }
  strlcpy(kbDev, creds["deviceId"] | "", sizeof(kbDev));
  strlcpy(kbUser, creds["username"] | "", sizeof(kbUser));
  strlcpy(kbPass, creds["password"] | "", sizeof(kbPass));
  String model = creds["modeId"].is<const char *>() ? String(creds["modeId"].as<const char *>())
               : creds["modeId"].isNull() ? String(modelName.indexOf('X') >= 0 ? "20030" : "20025")
               : String(creds["modeId"].as<long>());
  strlcpy(kbModel, model.c_str(), sizeof(kbModel));
  // The pairing reply also carries the printer's own client certificate; the
  // broker accepts it (tested on a Kobra S1), so no Anycubic cert is needed.
  kbDevCert = creds["devicecrt"] | "";
  kbDevKey = creds["devicepk"] | "";
  strlcpy(kbPairIp, ip, sizeof(kbPairIp));
  if (!kbDev[0] || !kbUser[0]) { setLink(KB_PAIR_FAIL, "Credentials incomplete"); return false; }
  saveCreds();
  return true;
}

// ---------------------------------------------------------------------------
// MQTT
// ---------------------------------------------------------------------------
// Boards without PSRAM keep a 6 KB MQTT buffer, too small for an S1 file list
// (~28 KB). There PubSubClient also copies every payload byte into this stream,
// which picks out the file names as they arrive (kfsFeed); onMessage then reads
// the result at the end of each message. Only used for "/file/" replies.
static const uint16_t KB_BUF_SMALL = 6144;
// Allocated only once TLS is up: on these boards every fixed KB comes out of the
// largest free block, and the TLS handshake needs ~16 KB of it in one piece.
static KobraFileScan *kbScan = nullptr;
static bool kbUseScan = false;

class KbScanStream : public Stream {
 public:
  size_t write(uint8_t c) override { char ch = (char)c; if (kbScan) kfsFeed(*kbScan, &ch, 1); return 1; }
  size_t write(const uint8_t *b, size_t n) override { if (kbScan) kfsFeed(*kbScan, (const char *)b, n); return n; }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
};
static KbScanStream kbScanStream;

static void setFileList(const char *action, const char *state, const char *msg, const char (*names)[64], int count) {
  if (strcmp(action, "listLocal") && strcmp(action, "listUdisk")) return;
  lock();
  kbFileCount = 0;
  kbFilesError = "";
  if (strcmp(state, "done")) kbFilesError = String(msg[0] ? msg : "The printer couldn't list its files");
  for (int i = 0; i < count && kbFileCount < FILES_MAX; i++) if (names[i][0]) kbFiles[kbFileCount++] = names[i];
  kbFilesVersion++;
  unlock();
}

static void onFileList(const uint8_t *payload, unsigned int len) {
  // Lists are big (215 files = 28 KB on an S1); keep only the names.
  JsonDocument filter;
  filter["action"] = true; filter["state"] = true; filter["msg"] = true;
  filter["data"]["records"][0]["filename"] = true;
  filter["data"]["records"][0]["is_dir"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, (const char *)payload, len, DeserializationOption::Filter(filter))) return;
  char (*names)[64] = (char (*)[64])malloc(FILES_MAX * 64);   // PSRAM boards only
  if (!names) return;
  int n = 0;
  for (JsonVariantConst r : doc["data"]["records"].as<JsonArrayConst>()) {
    if (n >= FILES_MAX) break;
    if (r["is_dir"] | false) continue;
    strlcpy(names[n], r["filename"] | "", 64);
    if (names[n][0]) n++;
  }
  setFileList(doc["action"] | "", doc["state"] | "", doc["msg"] | "", names, n);
  free(names);
}

static void onMessage(char *topic, uint8_t *payload, unsigned int len) {
  logRaw(topic, payload, len);
  if (kbUseScan && kbScan) {
    // The stream already saw this whole message; payload may be cut to the buffer.
    if (strstr(topic, "/file/")) setFileList(kbScan->action, kbScan->state, kbScan->msg, kbScan->names, kbScan->count);
    kfsReset(*kbScan);
    if (strstr(topic, "/file/")) return;
  } else if (strstr(topic, "/file/")) { onFileList(payload, len); return; }
  JsonDocument doc;
  if (deserializeJson(doc, (const char *)payload, len)) return;   // not JSON / truncated
  lock();
  kobraApplyMessage(doc.as<JsonVariantConst>(), kbState);
  unlock();
}

static void publishQuery(const char *type, const char *action, const char *dataJson = "null") {
  char head[160];
  snprintf(head, sizeof(head),
           "{\"type\":\"%s\",\"action\":\"%s\",\"msgid\":\"%08lx%08lx\",\"timestamp\":%llu,\"data\":",
           type, action, (unsigned long)esp_random(), (unsigned long)esp_random(), (unsigned long long)epochMs());
  String body = String(head) + dataJson + "}";
  String t = String("anycubic/anycubicCloud/v1/web/printer/") + kbModel + "/" + kbDev + "/" + type;
  mqtt->publish(t.c_str(), body.c_str());
}

static void queueCmd(const char *type, const char *action, const String &data) {
  lock();
  if (kbCmdCount < CMD_N) {
    KbCmd &c = kbCmds[(kbCmdHead + kbCmdCount) % CMD_N];
    strlcpy(c.type, type, sizeof(c.type)); strlcpy(c.action, action, sizeof(c.action)); c.data = data;
    kbCmdCount++;
  }
  unlock();
}

static void flushCmds() {
  for (;;) {
    KbCmd c;
    lock();
    if (kbCmdCount == 0) { unlock(); return; }
    c = kbCmds[kbCmdHead]; kbCmdHead = (kbCmdHead + 1) % CMD_N; kbCmdCount--;
    unlock();
    publishQuery(c.type, c.action, c.data.c_str());
  }
}

static bool isKobraX() { return !strcmp(kbModel, "20030"); }

// Read-only requests. The S1 only sends temps/job in reply to info/query, so it
// is asked every 10 s (the ACE every 60 s). The Kobra X is asked once per
// connection and then only listened to: frequent questions upset its firmware
// (LayerHQ's finding).
static void sendQueries(bool ace) {
  publishQuery("info", "query");
  if (ace) { publishQuery("multiColorBox", "getInfo"); lastAceQuery = millis(); }
  lastQuery = millis();
}

static bool connectMqtt(const char *ip) {
  setLink(KB_CONNECTING, "Connecting to MQTT (TLS 9883)...");
  if (!net) net = new WiFiClientSecure();
  net->stop();
  net->setInsecure();                       // printer's broker cert is self-signed
  bool haveDev = kbDevCert.length() && kbDevKey.length(), haveUp = kbCert.length() && kbKey.length();
  if (!haveDev && !haveUp) { setLink(KB_PAIR_FAIL, "Printer gave no client certificate; upload one below"); return false; }
  if (useUploaded && !haveUp) useUploaded = false;
  if (!useUploaded && !haveDev) useUploaded = true;
  net->setCertificate(useUploaded ? kbCert.c_str() : kbDevCert.c_str());
  net->setPrivateKey(useUploaded ? kbKey.c_str() : kbDevKey.c_str());
  net->setHandshakeTimeout(15);
  if (!mqtt) {
    mqtt = new PubSubClient(*net);
    // PSRAM boards get room for the printer's file list (28 KB for 215 files on an S1).
    if (!mqtt->setBufferSize(psramFound() ? 49152 : KB_BUF_SMALL)) mqtt->setBufferSize(4096);
    mqtt->setCallback(onMessage);
    mqtt->setKeepAlive(60);
    mqtt->setSocketTimeout(10);
  }
  mqtt->setServer(ip, 9883);
  if (!kbClientId[0]) snprintf(kbClientId, sizeof(kbClientId), "cyd-%08lx", (unsigned long)esp_random());
  // Own client id (not the deviceId), so the slicer / LayerHQ aren't kicked off.
  // Hand the scanner's memory back for the handshake; it's allocated again once connected
  // (which also drops any message the last disconnect cut off).
  if (kbScan) { free(kbScan); kbScan = nullptr; }
  if (!mqtt->connect(kbClientId, kbUser, kbPass)) {
    int rc = mqtt->state();
    if (rc == 4 || rc == 5) {                // bad credentials / not authorised: pair again
      clearCreds();
      setLink(KB_CONNECTING, "Login refused (rc " + String(rc) + "), re-pairing");
    } else {
      char err[100] = "";
      net->lastError(err, sizeof(err));
      // TLS refused with one certificate: try the other one next time (if there is one).
      if (kbCert.length() && kbDevCert.length()) useUploaded = !useUploaded;
      setLink(KB_CONNECTING, "MQTT connect failed (rc " + String(rc) + ") " + err);
    }
    return false;
  }
  String m(kbModel), d(kbDev);
  String subs[2] = {
    "anycubic/anycubicCloud/v1/printer/public/" + m + "/" + d + "/#",
    "anycubic/anycubicCloud/v1/printer/" + m + "/" + d + "/#",
  };
  if (!psramFound() && !kbScan) {
    kbScan = (KobraFileScan *)malloc(sizeof(KobraFileScan));
    if (kbScan) { kfsReset(*kbScan); kbUseScan = true; mqtt->setStream(kbScanStream); }
  }
  for (auto &s : subs) mqtt->subscribe(s.c_str());
  sendQueries(true);
  setLink(KB_CONNECTED, String("Connected (") + (useUploaded ? "uploaded" : "printer's own") + " certificate)");
  return true;
}

// ---------------------------------------------------------------------------
// Poll-task API
// ---------------------------------------------------------------------------
void kobraBegin() {
  if (!kbMutex) kbMutex = xSemaphoreCreateMutex();
  if (!kbLogMem) {
    bool ps = psramFound();
    kbLogN = ps ? 8 : 3; kbLogW = ps ? 860 : 360;
    kbLogMem = (char *)(ps ? ps_calloc(kbLogN, kbLogW) : calloc(kbLogN, kbLogW));
  }
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");   // command timestamps; syncs in the background
  kobraInit(kbState);
  loadSettings();
  downSince = millis();
}

// Free heap, for the /kobra page (no-PSRAM boards are the tight ones).
String kobraHeapText() {
  return String(ESP.getFreeHeap() / 1024) + " KB free, lowest " + String(ESP.getMinFreeHeap() / 1024) + " KB, largest block " + String(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024) + " KB";
}

KobraLink kobraService(const char *ip) {
  if (kbReloadCert || kbForget) {
    if (kbForget) clearCreds();
    kbReloadCert = kbForget = false;
    if (mqtt) mqtt->disconnect();
    if (net) net->stop();
    loadSettings();
    backoff = 0;              // retry right away with the new cert / pairing
  }
  // A login belongs to one printer: a new printer IP means pair again.
  if (kbDev[0] && strcmp(kbPairIp, ip) != 0) {
    if (mqtt) mqtt->disconnect();
    clearCreds();
    setLink(KB_PAIRING, "Printer IP changed, pairing again");
  }
  if (mqtt && mqtt->connected()) {
    mqtt->loop();
    flushCmds();
    if (!isKobraX() && millis() - lastQuery > 10000) sendQueries(millis() - lastAceQuery > 60000);
    // Only a link that stays up counts as recovered (a broker that keeps
    // kicking us, e.g. another app using the same login, must not look online).
    if (millis() - upSince > 10000) downSince = 0;
    if (millis() - upSince > 60000) backoff = 0;
    return KB_CONNECTED;
  }
  if (wasUp) {
    // Just dropped: wait before reconnecting, longer each time it flaps.
    wasUp = false;
    lastAttempt = millis();
    backoff = backoff ? (backoff < 60000 ? backoff * 2 : 60000) : 5000;
    if (downSince == 0) downSince = millis();
    setLink(KB_CONNECTING, "Connection lost, reconnecting");
  }
  if (downSince == 0) downSince = millis();
  if (backoff && millis() - lastAttempt < backoff) return kbLink;

  bool ok = (kbDev[0] && kbUser[0]) || pair(ip);
  if (ok) ok = connectMqtt(ip);
  lastAttempt = millis();
  if (ok) { wasUp = true; upSince = millis(); return KB_CONNECTED; }
  backoff = backoff ? (backoff < 60000 ? backoff * 2 : 60000) : 5000;
  return kbLink;
}

void kobraGetState(KobraState &out) { lock(); out = kbState; unlock(); }
unsigned long kobraDownForMs() { return downSince ? millis() - downSince : 0; }

// ---------------------------------------------------------------------------
// UI / web API
// ---------------------------------------------------------------------------
String kobraStatusText() {
  lock(); String s = kbDetail; unlock();
  return s.length() ? s : String("Starting...");
}

const char *kobraModelName() {
  if (!strcmp(kbModel, "20025")) return "Kobra S1";
  if (!strcmp(kbModel, "20030")) return "Kobra X";
  return "Kobra";
}

bool kobraHasCert() {
  Preferences p;
  p.begin("kobra", true);
  bool has = p.isKey("cert") && p.isKey("key");
  p.end();
  return has;
}

static String fromMarker(const String &s, const char *marker) {
  int i = s.indexOf(marker);
  if (i < 0) return "";
  String out = s.substring(i);
  out.trim();
  out.replace("\r\n", "\n");
  return out + "\n";
}

String kobraSaveCert(const String &certPem, const String &keyPem) {
  String cert = fromMarker(certPem, "-----BEGIN CERTIFICATE-----");
  String key = fromMarker(keyPem, "-----BEGIN ");
  if (cert.length() == 0) return "Certificate must contain -----BEGIN CERTIFICATE-----";
  if (key.indexOf("PRIVATE KEY-----") < 0) return "Key must be a PEM private key (-----BEGIN PRIVATE KEY-----)";
  if (key.indexOf("ENCRYPTED") >= 0) return "Key is password-protected; export it with -nodes";
  if (cert.length() > 3000 || key.length() > 3500) return "PEM too long (max 3000 / 3500 characters)";
  Preferences p;
  p.begin("kobra", false);
  bool ok = p.putString("cert", cert) == cert.length() && p.putString("key", key) == key.length();
  if (!ok) { p.remove("cert"); p.remove("key"); }
  p.end();
  if (!ok) return "Could not store the certificate (settings storage full)";
  kbReloadCert = true;
  return "";
}

void kobraForgetPairing() { kbForget = true; }

void kobraAppendLogHtml(String &html, int maxEntries, int maxChars) {
  lock();
  for (int i = 0, shown = 0; kbLogMem && i < kbLogN && shown < maxEntries; i++) {
    const char *e = kbLogMem + ((kbLogHead + kbLogN - 1 - i) % kbLogN) * kbLogW;   // newest first
    if (!e[0]) continue;
    shown++;
    html += "<pre style='white-space:pre-wrap; word-break:break-all; background:#0b0d10; padding:8px; border-radius:6px; font-size:11px;'>";
    unsigned int len = strlen(e);
    unsigned int n = len < (unsigned)maxChars ? len : (unsigned)maxChars;
    for (unsigned int k = 0; k < n; k++) {
      char c = e[k];
      if (c == '<') html += "&lt;"; else if (c == '>') html += "&gt;"; else if (c == '&') html += "&amp;"; else html += c;
    }
    html += "</pre>";
  }
  unlock();
}

// ---------------------------------------------------------------------------
// Commands (same messages Anycubic's app, KobraCommandCenter and LayerHQ send)
// ---------------------------------------------------------------------------
void kobraPrintControl(const char *action) { queueCmd("print", action, "{\"taskid\":\"-1\"}"); }

void kobraMove(int axis, int dir, float mm) {
  queueCmd("axis", "move", String("{\"axis\":") + axis + ",\"move_type\":" + (dir > 0 ? 1 : 0) + ",\"distance\":" + String(mm, mm < 1 ? 1 : 0) + "}");
}

void kobraHome(bool zOnly) {
  queueCmd("axis", "move", String("{\"axis\":") + (zOnly ? 3 : 4) + ",\"move_type\":2,\"distance\":0}");
}

void kobraAceSetSlot(int slot, const char *type, uint32_t rgb) {
  char data[200];
  snprintf(data, sizeof(data),
           "{\"multi_color_box\":[{\"id\":0,\"slots\":[{\"index\":%d,\"type\":\"%s\",\"color\":[%u,%u,%u]}]}]}",
           slot, type, (unsigned)((rgb >> 16) & 0xFF), (unsigned)((rgb >> 8) & 0xFF), (unsigned)(rgb & 0xFF));
  queueCmd("multiColorBox", "setInfo", data);
  queueCmd("multiColorBox", "getInfo", "null");   // read the slot back
}

void kobraAceFeed(int slot, bool load) {
  // Retract always names slot -1 (the loaded one), as Anycubic's own app does.
  char data[120];
  snprintf(data, sizeof(data), "{\"multi_color_box\":[{\"id\":0,\"feed_status\":{\"slot_index\":%d,\"type\":%d}}]}",
           load ? slot : -1, load ? 1 : 2);
  queueCmd("multiColorBox", "feedFilament", data);
}

static String jsonEscape(const String &s) {
  String o; o.reserve(s.length() + 8);
  for (unsigned i = 0; i < s.length(); i++) { char c = s[i]; if (c == '"' || c == '\\') o += '\\'; o += c; }
  return o;
}

void kobraStartPrint(const String &filename, bool usb, uint32_t size) {
  // Shape from community work on the Kobra X (kobra-lan-cli, OrcaSlicer PR 17654): an empty
  // url starts a stored file; filepath is the folder. Not yet confirmed on an S1.
  // use_ams false: print with whatever is loaded instead of remapping ACE slots.
  String d = String("{\"taskid\":\"-1\",\"url\":\"\",\"filename\":\"") + jsonEscape(filename) +
             "\",\"md5\":\"\",\"filepath\":\"/\",\"filetype\":" + (usb ? 2 : 1) +
             ",\"project_type\":1,\"filesize\":" + size +
             ",\"ams_settings\":{\"use_ams\":false,\"ams_box_mapping\":[]}"
             ",\"task_settings\":{\"auto_leveling\":1,\"vibration_compensation\":0,\"flow_calibration\":0,\"dry_mode\":0,"
             "\"ai_settings\":{\"status\":0,\"count\":0,\"type\":0},\"timelapse\":{\"status\":0,\"count\":0,\"type\":0},"
             "\"drying_settings\":{\"status\":0,\"target_temp\":0,\"duration\":0,\"remain_time\":0},\"model_objects_skip_parts\":[]}}";
  queueCmd("print", "start", d);
}

void kobraRequestFiles(bool usb) {
  lock(); kbFileCount = 0; kbFilesError = ""; unlock();
  queueCmd("file", usb ? "listUdisk" : "listLocal", "{\"path\":\"/\"}");
}

int kobraFiles(String *out, int max, uint32_t &version, String &error) {
  lock();
  int n = kbFileCount < max ? kbFileCount : max;
  for (int i = 0; i < n; i++) out[i] = kbFiles[i];
  version = kbFilesVersion; error = kbFilesError;
  unlock();
  return n;
}
