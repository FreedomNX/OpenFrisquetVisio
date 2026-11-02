#pragma once

#include <Arduino.h>

// Fil d'Ariane en RTC_NOINIT : survit à un reset watchdog ou panic, pas à une coupure d'alimentation.
namespace CrashTrace {

enum Step : uint8_t { S_IDLE = 0, S_OTA, S_PORTAL, S_MQTT, S_MANAGER, S_RX_READ, S_RX_LOG, S_RX_DISPATCH, S_RX_WAIT, S_TX, S_NVS };

struct Snapshot {
  uint32_t wdtResets;    // resets WDT/PANIC cumulés depuis la mise sous tension
  uint8_t prevStep;      // dernière étape avant le reset précédent
  uint32_t prevStepAtMs; // uptime (ms) de cette étape dans l'exécution précédente
  uint32_t rxFrames;     // trames radio lues depuis ce démarrage
  uint32_t maxLoopMs;    // plus long tour de App::loop()
  uint32_t slowLoops;    // tours de App::loop() > 1 s
  uint32_t rxIrqs;       // réveils radio bruts (IRQ), valides ou non
  int32_t  rssiInst;     // RSSI instantané (dBm x1, 0 = inconnu)
};

void begin();
void mark(Step step);
void rxFrame();
void rxIrq();
void rssiSample(float dbm);
void lastFrame(const uint8_t* data, size_t len);  // dernière trame brute reçue
String lastFrameHex();
uint8_t lastFrameLen();
void loopDone(uint32_t durationMs);
Snapshot snapshot();

// Étape temporaire : l'étape précédente est restaurée en fin de bloc.
struct Scope {
  explicit Scope(Step step);
  ~Scope();
  uint8_t previous;
};

}  // namespace CrashTrace
