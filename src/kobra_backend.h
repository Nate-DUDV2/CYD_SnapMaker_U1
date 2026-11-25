// Anycubic Kobra (S1 / X) LAN backend: pairing over HTTP :18910 and a
// watch-only MQTT/TLS session on :9883. Runs entirely inside the printer poll
// task; the web handlers only read status / the raw log and store the cert.
#pragma once
#include <Arduino.h>
#include <PrinterParse.h>

enum KobraLink : uint8_t {
  KB_PAIRING = 0,       // fetching deviceId / username / password from the printer
  KB_PAIR_FAIL,     // printer didn't answer on :18910 or the reply didn't decode
  KB_CONNECTING,    // TLS + MQTT login in progress / backing off
  KB_CONNECTED,     // subscribed, receiving reports
};

// Poll-task side. kobraService() must be called every ~50 ms.
void kobraBegin();
KobraLink kobraService(const char *ip);
// Copy of the merged printer state (thread-safe).
void kobraGetState(KobraState &out);
// Milliseconds since the MQTT link last went down (0 while connected).
unsigned long kobraDownForMs();

// UI / web side (thread-safe).
String kobraStatusText();
String kobraHeapText();
const char *kobraModelName();               // "Kobra S1", "Kobra X" or "Kobra"
bool kobraHasCert();
// Validates and stores the PEM pair. Returns "" on success, else an error message.
String kobraSaveCert(const String &certPem, const String &keyPem);
void kobraForgetPairing();                   // next service() re-pairs
void kobraAppendLogHtml(String &html, int maxEntries = 8, int maxChars = 700);   // last raw MQTT messages, HTML-escaped

// Commands (UI side; queued and sent by the poll task while connected).
void kobraPrintControl(const char *action);   // "pause", "resume", "stop"
void kobraMove(int axis, int dir, float mm);  // axis 2 = X, 1 = Y, 3 = Z; dir +1 / -1
void kobraHome(bool zOnly);
// ACE Pro slot editing (box 0, slot 0..3). Same bodies as Anycubic's cloud orders
// (hass-anycubic_cloud) on the LAN topic multiColorBox, like setDry/setAutoFeed.
void kobraAceSetSlot(int slot, const char *type, uint32_t rgb);   // action setInfo
void kobraAceFeed(int slot, bool load);                           // feedFilament: 1 feed, 2 retract
void kobraStartPrint(const String &filename, bool usb, uint32_t size = 0);
// Printer file list (newest first). kobraRequestFiles() asks; kobraFiles() returns the
// latest answer: count, and filesVersion bumps every time a new list arrives.
void kobraRequestFiles(bool usb);
int kobraFiles(String *out, int max, uint32_t &version, String &error);
