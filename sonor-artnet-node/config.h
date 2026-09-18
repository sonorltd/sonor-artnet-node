/*
 *  SONOR Art-Net Node — board selection + factory defaults
 *
 *  Pick a board by uncommenting ONE line below, or leave all commented and the
 *  sketch picks the sensible default for whatever board is selected in the IDE:
 *    AVR (Nano/Uno)  → BOARD_NANO_ENC28J60
 *    ESP32           → BOARD_ESP32_WIFI
 *    ESP8266         → BOARD_ESP8266_WIFI
 *  (The prebuilt binaries on the Pages site are compiled with -DSONOR_BOARD=<n>.)
 */
#pragma once
#include <stdint.h>

#define BOARD_NANO_ENC28J60   1   // Arduino Nano/Uno + ENC28J60 shield        (Ethernet)
#define BOARD_NANO_W5500      2   // Arduino Nano/Uno + W5500 / W5100 shield   (Ethernet)
#define BOARD_ESP32_WIFI      3   // ESP32 DevKit — WiFi only                  (WiFi)
#define BOARD_ESP32_W5500     4   // ESP32 DevKit + W5500 SPI module           (Ethernet, WiFi fallback)
#define BOARD_WT32_ETH01      5   // WT32-ETH01 (ESP32 + LAN8720 on board)     (Ethernet, WiFi fallback)
#define BOARD_ESP8266_WIFI    6   // Wemos D1 mini / NodeMCU (ESP8266)         (WiFi)

// #define SONOR_BOARD BOARD_NANO_ENC28J60
// #define SONOR_BOARD BOARD_NANO_W5500
// #define SONOR_BOARD BOARD_ESP32_WIFI
// #define SONOR_BOARD BOARD_ESP32_W5500
// #define SONOR_BOARD BOARD_WT32_ETH01
// #define SONOR_BOARD BOARD_ESP8266_WIFI

#ifndef SONOR_BOARD
  #if defined(ESP32)
    #define SONOR_BOARD BOARD_ESP32_WIFI
  #elif defined(ESP8266)
    #define SONOR_BOARD BOARD_ESP8266_WIFI
  #else
    #define SONOR_BOARD BOARD_NANO_ENC28J60
  #endif
#endif

#define FW_VERSION  "0.3.0"

// ───────────────────────── FACTORY DEFAULTS ─────────────────────────
// Everything here can be changed later from the node's own web page.
#define DEF_NET        0            // Art-Net Net       (0-127)
#define DEF_SUBNET     0            // Art-Net Sub-Net   (0-15)
#define DEF_UNIVERSE   0            // Art-Net Universe  (0-15)  → Port-Address 0 = "Universe 1" in most consoles
#define DEF_DHCP       1            // 1 = try DHCP first, 0 = static only
#define DEF_IP         2, 0, 0, 10  // Art-Net convention: 2.x.x.x / 255.0.0.0 (same as an ODE out of the box)
#define DEF_MASK       255, 0, 0, 0
#define DEF_GW         2, 0, 0, 1
#define DHCP_TIMEOUT_MS 5000

// ───────────────────────── PIN MAPS ─────────────────────────
#if SONOR_BOARD == BOARD_NANO_ENC28J60 || SONOR_BOARD == BOARD_NANO_W5500
  #define ETH_CS_PIN   10   // shield chip-select (D10 on nearly all Nano/Uno shields)
  #define DMX_DE_PIN   2    // MAX485 DE+RE. -1 if tied to 5V
  #define LED_PIN      3    // activity LED (D13 is SPI SCK — don't use it)
  // DMX TX is the hardware UART: D1

#elif SONOR_BOARD == BOARD_ESP32_WIFI
  #define DMX_TX_PIN   17   // UART2 TX → MAX485 DI
  #define DMX_DE_PIN   4    // MAX485 DE+RE
  #define LED_PIN      2    // on-board LED on most DevKits

#elif SONOR_BOARD == BOARD_ESP32_W5500
  #define DMX_TX_PIN   17
  #define DMX_DE_PIN   4
  #define LED_PIN      2
  #define W5500_CS     5    // VSPI: SCK 18, MISO 19, MOSI 23
  #define W5500_INT    35
  #define W5500_RST    -1
  #define W5500_SCK    18
  #define W5500_MISO   19
  #define W5500_MOSI   23

#elif SONOR_BOARD == BOARD_WT32_ETH01
  #define DMX_TX_PIN   17   // TXD2 pad on the WT32-ETH01
  #define DMX_DE_PIN   4    // IO4 pad
  #define LED_PIN      -1   // no free on-board LED
  // LAN8720: addr 1, MDC 23, MDIO 18, power 16, clock in on GPIO0

#elif SONOR_BOARD == BOARD_ESP8266_WIFI
  #define DMX_DE_PIN   5    // D1 (GPIO5) → MAX485 DE+RE
  #define LED_PIN      -1   // D4/GPIO2 (the on-board LED) is the DMX TX line, so no free LED
  // DMX TX is Serial1: GPIO2 / D4 (TX-only UART)
#endif

#define ARTNET_PORT   6454
#define OP_POLL       0x2000
#define OP_POLLREPLY  0x2100
#define OP_DMX        0x5000
#define CFG_MAGIC     0xA8

struct Config {
  uint8_t magic;
  uint8_t net, subnet, universe;
  uint8_t dhcp;
  uint8_t ip[4], mask[4], gw[4];
#if defined(ESP32) || defined(ESP8266)
  char    ssid[33];     // WiFi boards only (kept out of the AVR struct — every byte of RAM counts there)
  char    pass[65];
#endif
};

