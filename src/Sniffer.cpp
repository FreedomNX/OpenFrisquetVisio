#include "Sniffer.h"

namespace {
constexpr uint8_t  kMaxPackets = 24;
constexpr size_t   kMaxLen     = 72;
constexpr uint32_t kMaxSeconds = 900;

struct Entry {
  uint32_t ms;
  uint8_t  len;
  float    rssi;
  uint8_t  data[kMaxLen];
};

Entry    gPackets[kMaxPackets];
uint8_t  gCount   = 0;
uint8_t  gHead    = 0;
bool     gActive  = false;
uint32_t gDeadline = 0;
uint32_t gTotal   = 0;
float    gMhz     = 868.96f;

String toHex(const uint8_t* d, size_t n) {
  static const char* H = "0123456789ABCDEF";
  String s;
  s.reserve(n * 2);
  for (size_t i = 0; i < n; i++) {
    s += H[(d[i] >> 4) & 0x0F];
    s += H[d[i] & 0x0F];
  }
  return s;
}
}  // namespace

namespace Sniffer {

void start(uint32_t seconds, uint8_t syncByte, uint8_t syncLen, float mhz) {
  if (seconds == 0) seconds = 60;
  if (seconds > kMaxSeconds) seconds = kMaxSeconds;
  if (syncLen < 1) syncLen = 1;
  if (syncLen > 8) syncLen = 8;
  gMhz = mhz;
  gCount = 0;
  gHead = 0;
  gTotal = 0;
  gActive = true;
  gDeadline = millis() + seconds * 1000UL;
  // La configuration radio (fréquence, sync word, armement) est faite par
  // l'appelant, qui possède l'objet Radio.
  (void)syncByte;
  (void)syncLen;
}

void stop() { gActive = false; }

bool active() { return gActive; }

void loop() {
  if (gActive && static_cast<int32_t>(millis() - gDeadline) >= 0) gActive = false;
}

void onPacket(const uint8_t* data, size_t len) {
  if (!gActive || len == 0) return;
  Entry& e = gPackets[gHead];
  e.ms = millis();
  e.len = static_cast<uint8_t>(len > kMaxLen ? kMaxLen : len);
  e.rssi = 0;
  memcpy(e.data, data, e.len);
  gHead = (gHead + 1) % kMaxPackets;
  if (gCount < kMaxPackets) gCount++;
  gTotal++;
}

String report() {
  String j = "{\"ok\":true,\"active\":";
  j += gActive ? "true" : "false";
  j += ",\"f\":";
  j += String(gMhz, 3);
  j += ",\"total\":";
  j += String(gTotal);
  j += ",\"packets\":[";
  const uint8_t first = (gHead + kMaxPackets - gCount) % kMaxPackets;
  for (uint8_t i = 0; i < gCount; i++) {
    const Entry& e = gPackets[(first + i) % kMaxPackets];
    if (i) j += ",";
    j += "{\"ms\":";
    j += String(e.ms);
    j += ",\"len\":";
    j += String(e.len);
    j += ",\"hex\":\"";
    j += toHex(e.data, e.len);
    j += "\"}";
  }
  j += "]}";
  return j;
}

}  // namespace Sniffer
