#pragma once

// TTGO / LilyGo LoRa32 V2.1, version SX1276 868 MHz (PR #20).
#if defined(BOARD_TTGO_LORA32)
#include <Arduino.h>
#define RADIO_SX1276
#define RADIO_NSS GPIO_NUM_18
#define RADIO_RST GPIO_NUM_23
#define RADIO_IRQ GPIO_NUM_26
#define RADIO_GPIO GPIO_NUM_33
#define RADIO_SCK GPIO_NUM_5
#define RADIO_MISO GPIO_NUM_19
#define RADIO_MOSI GPIO_NUM_27
// GPIO33 est DIO1 ; GPIO13 est aussi CS de la microSD, inutilisée ici.
#define DS18B20_PIN GPIO_NUM_13
// Garder une marge de DRAM sur ESP32 classique, y compris avec GCC 14.
#define LOGS_MAX_LINES 80
#else
#include <heltec.h>
#define BOARD_HELTEC
#define RADIO_NSS GPIO_NUM_8
#define RADIO_RST GPIO_NUM_12
#define RADIO_IRQ GPIO_NUM_14
#define RADIO_GPIO GPIO_NUM_13
#define DS18B20_PIN GPIO_NUM_33
#define LOGS_MAX_LINES 300
#endif
