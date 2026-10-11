#pragma once

// Brochage et radio selon la carte cible (choisie via build_flags dans platformio.ini)

#if defined(BOARD_TTGO_LORA32)
  // TTGO / LilyGO LoRa32 V2.1 (ESP32 + SX1276)
  #include <Arduino.h>
  #define RADIO_CHIP  SX1276
  #define RADIO_SCK    GPIO_NUM_5
  #define RADIO_MISO   GPIO_NUM_19
  #define RADIO_MOSI   GPIO_NUM_27
  #define RADIO_NSS    GPIO_NUM_18
  #define RADIO_RST    GPIO_NUM_23
  #define RADIO_DIO0   GPIO_NUM_26
  #define RADIO_GPIO   GPIO_NUM_33   // DIO1
  #define DS18B20_PIN GPIO_NUM_13   // GPIO 33 est pris par DIO1 sur la TTGO
  #define LOGS_MAX_LINES 120        // RAM statique plus limitée que sur l'ESP32-S3
#else
  // Heltec WiFi LoRa 32 V3/V4 (ESP32-S3 + SX1262)
  #include <heltec.h>
  #define BOARD_HELTEC
  #define RADIO_CHIP  SX1262
  #define RADIO_NSS    GPIO_NUM_8
  #define RADIO_RST    GPIO_NUM_12
  #define RADIO_DIO0   GPIO_NUM_14   // DIO1
  #define RADIO_GPIO   GPIO_NUM_13   // BUSY
  #define DS18B20_PIN GPIO_NUM_33
  #define LOGS_MAX_LINES 300
#endif
