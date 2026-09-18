/*
 *  SONOR Art-Net Node  —  Ethernet / WiFi → DMX512 out            v0.3.0
 *
 *  A single-universe Art-Net node that stands in for an Enttec ODE:
 *    - Answers ArtPoll (discovery) and takes ArtDmx on UDP 6454
 *    - One universe of DMX512 out through a MAX485 driver
 *    - Its own web page (http://<node-ip>/) to change Net / Sub-Net / Universe,
 *      DHCP or static IP (and WiFi credentials on WiFi boards), with wiring +
 *      flashing notes. Settings persist in EEPROM/flash.
 *    - Holds last look if the source stops sending (like an ODE)
 *
 *  Boards (pick in config.h, or let the IDE board selection choose):
 *    Arduino Nano/Uno + ENC28J60 shield     Arduino Nano/Uno + W5500/W5100 shield
 *    ESP32 DevKit (WiFi)                    ESP32 DevKit + W5500 SPI module
 *    WT32-ETH01 (ESP32 + LAN8720)           Wemos D1 mini / NodeMCU (ESP8266, WiFi)
 *
 *  Prebuilt binaries + browser flashing (no software) + wiring diagrams:
 *    https://sonorltd.github.io/sonor-artnet-node/
 *
 *  Libraries are BUNDLED in ./src — nothing to install for the AVR builds.
 *  ESP32 / ESP8266 builds only need the board package (Boards Manager) — no extra libraries.
 *  (Do NOT also install EthernetENC from Library Manager — the stock version has a stray
 *   debug function that pulls in HardwareSerial and breaks the DMX interrupt vectors.)
 */

#include "config.h"

#if defined(ESP32) || defined(ESP8266)
  #include "esp_node.h"
#else
  #include "avr_node.h"
#endif

void setup() { nodeSetup(); }
void loop()  { nodeLoop();  }
