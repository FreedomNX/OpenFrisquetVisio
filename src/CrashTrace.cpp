#include "CrashTrace.h"
#include <esp_attr.h>
#include <esp_system.h>

namespace {
constexpr uint32_t kMagic = 0x0CF71DB6;
RTC_NOINIT_ATTR uint32_t rtcMagic;
RTC_NOINIT_ATTR uint32_t rtcWdtResets;
RTC_NOINIT_ATTR volatile uint8_t rtcStep;
RTC_NOINIT_ATTR volatile uint32_t rtcStepAtMs;

uint8_t prevStep = 0;
uint32_t prevStepAtMs = 0;
uint32_t rxFrames = 0;
uint32_t maxLoopMs = 0;
uint32_t slowLoops = 0;
uint32_t rxIrqs = 0;
uint8_t  gLast[68];
uint8_t  gLastLen = 0;

void putHex(const uint8_t* d, size_t n, char* out) {
  static const char* H = "0123456789ABCDEF";
  for (size_t i = 0; i < n; i++) { *out++ = H[(d[i] >> 4) & 0x0F]; *out++ = H[d[i] & 0x0F]; }
  *out = 0;
}
int32_t  rssiInst = 0;
}  // namespace

namespace CrashTrace {

void begin() {
  const esp_reset_reason_t reason = esp_reset_reason();
  if (rtcMagic != kMagic || reason == ESP_RST_POWERON) {  // RTC_NOINIT est aléatoire à la mise sous tension
    rtcMagic = kMagic;
    rtcWdtResets = 0;
    rtcStep = S_IDLE;
    rtcStepAtMs = 0;
  }
  prevStep = rtcStep;
  prevStepAtMs = rtcStepAtMs;
  if (reason == ESP_RST_INT_WDT || reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT || reason == ESP_RST_PANIC) {
    ++rtcWdtResets;
  }
  rtcStep = S_IDLE;
  rtcStepAtMs = millis();
}

void mark(Step step) {
  rtcStep = step;
  rtcStepAtMs = millis();
}

void rxFrame() { ++rxFrames; }

void rxIrq() { ++rxIrqs; }

void lastFrame(const uint8_t* data, size_t len) {
  if (!data || len == 0) return;
  gLastLen = static_cast<uint8_t>(len > 68 ? 68 : len);
  memcpy(gLast, data, gLastLen);
}

String lastFrameHex() {
  char hex[137];
  putHex(gLast, gLastLen, hex);
  return String(hex);
}

uint8_t lastFrameLen() { return gLastLen; }

void rssiSample(float dbm) { rssiInst = (int32_t)dbm; }

void loopDone(uint32_t durationMs) {
  if (durationMs > maxLoopMs) maxLoopMs = durationMs;
  if (durationMs > 1000) ++slowLoops;
}

Snapshot snapshot() { return {rtcWdtResets, prevStep, prevStepAtMs, rxFrames, maxLoopMs, slowLoops, rxIrqs, rssiInst}; }

Scope::Scope(Step step) : previous(rtcStep) { mark(step); }
Scope::~Scope() { mark(static_cast<Step>(previous)); }

}  // namespace CrashTrace
