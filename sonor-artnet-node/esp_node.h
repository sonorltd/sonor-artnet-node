#pragma once
// ───────────────────────── ESP32 / ESP8266 build ─────────────────────────
//  WiFi (all ESP boards) and, on BOARD_ESP32_W5500 / BOARD_WT32_ETH01, wired Ethernet with WiFi fallback.
//  Plenty of RAM here, so this file is written for clarity rather than bytes.

#include <EEPROM.h>
#if defined(ESP32)
  #include <WiFi.h>
  #include <WiFiUdp.h>
  #include <WebServer.h>
  #include <ESPmDNS.h>
  #include "driver/uart.h"
  #if SONOR_BOARD == BOARD_ESP32_W5500 || SONOR_BOARD == BOARD_WT32_ETH01 || SONOR_BOARD == BOARD_OLIMEX_POE || SONOR_BOARD == BOARD_LILYGO_POE
    #define HAS_ETH 1
    #undef ETH_PHY_TYPE
    #undef ETH_PHY_ADDR
    #undef ETH_PHY_MDC
    #undef ETH_PHY_MDIO
    #undef ETH_PHY_POWER
    #undef ETH_CLK_MODE
    #if SONOR_BOARD == BOARD_WT32_ETH01
      #define ETH_PHY_TYPE  ETH_PHY_LAN8720
      #define ETH_PHY_ADDR  1
      #define ETH_PHY_MDC   23
      #define ETH_PHY_MDIO  18
      #define ETH_PHY_POWER 16
      #define ETH_CLK_MODE  ETH_CLOCK_GPIO0_IN
    #elif SONOR_BOARD == BOARD_OLIMEX_POE
      #define ETH_PHY_TYPE  ETH_PHY_LAN8720
      #define ETH_PHY_ADDR  0
      #define ETH_PHY_MDC   23
      #define ETH_PHY_MDIO  18
      #define ETH_PHY_POWER 12
      #define ETH_CLK_MODE  ETH_CLOCK_GPIO17_OUT
    #elif SONOR_BOARD == BOARD_LILYGO_POE
      #define ETH_PHY_TYPE  ETH_PHY_LAN8720
      #define ETH_PHY_ADDR  0
      #define ETH_PHY_MDC   23
      #define ETH_PHY_MDIO  18
      #define ETH_PHY_POWER 5
      #define ETH_CLK_MODE  ETH_CLOCK_GPIO17_OUT
    #endif
    #include <ETH.h>
  #endif
  typedef WebServer NodeWebServer;
#else
  #include <ESP8266WiFi.h>
  #include <WiFiUdp.h>
  #include <ESP8266WebServer.h>
  #include <ESP8266mDNS.h>
  typedef ESP8266WebServer NodeWebServer;
#endif
#ifndef HAS_ETH
  #define HAS_ETH 0
#endif

Config        cfg;
WiFiUDP       udp;
NodeWebServer web(80);
uint8_t       dmx[513];              // [0] = start code 0, [1..512] = channels
uint8_t       pkt[600];              // one Art-Net packet
uint8_t       reply[239];            // ArtPollReply
uint8_t       mac[6];
bool          apMode = false;        // WiFi hotspot fallback (no/invalid credentials)
bool          ethUp  = false;
bool          dhcpOk = false;
uint32_t      lastFrame = 0, frames = 0, restartAt = 0;
char          hostname[24];

#define SUBUNI ((cfg.subnet << 4) | cfg.universe)

// ───────────────────────── config ─────────────────────────
static void loadConfig() {
  EEPROM.begin(256);
  EEPROM.get(0, cfg);
  if (cfg.magic != CFG_MAGIC) {
    const uint8_t ip[] = {DEF_IP}, mask[] = {DEF_MASK}, gw[] = {DEF_GW};
    memset(&cfg, 0, sizeof(cfg));
    cfg.magic = CFG_MAGIC; cfg.net = DEF_NET; cfg.subnet = DEF_SUBNET; cfg.universe = DEF_UNIVERSE; cfg.dhcp = DEF_DHCP;
    memcpy(cfg.ip, ip, 4); memcpy(cfg.mask, mask, 4); memcpy(cfg.gw, gw, 4);
    EEPROM.put(0, cfg); EEPROM.commit();
  }
  cfg.net &= 0x7F; cfg.subnet &= 0x0F; cfg.universe &= 0x0F; cfg.dhcp = cfg.dhcp ? 1 : 0;
  cfg.ssid[32] = 0; cfg.pass[64] = 0;
}
static void saveConfig() { EEPROM.put(0, cfg); EEPROM.commit(); }

// ───────────────────────── DMX out ─────────────────────────
#if defined(ESP32)
  #define DMX_UART UART_NUM_2
  static void dmxBegin() {
    Serial2.setTxBufferSize(1024);
    Serial2.begin(250000, SERIAL_8N2, -1, DMX_TX_PIN);
  }
  static void dmxSend() {
    uart_wait_tx_done(DMX_UART, pdMS_TO_TICKS(50));          // line idle before the break
    uart_set_line_inverse(DMX_UART, UART_SIGNAL_TXD_INV);    // BREAK  (≥ 88 µs; we do 180)
    delayMicroseconds(180);
    uart_set_line_inverse(DMX_UART, UART_SIGNAL_INV_DISABLE);
    delayMicroseconds(20);                                   // MAB    (≥ 12 µs)
    uart_write_bytes(DMX_UART, (const char*)dmx, sizeof(dmx)); // async — 513 bytes ≈ 22.6 ms on the wire
  }
#else
  static void dmxBegin() {
    Serial1.begin(250000, SERIAL_8N2);                       // Serial1 = GPIO2 / D4, TX only
  }
  static void dmxSend() {
    Serial1.flush();
    Serial1.updateBaudRate(90909);                           // one 0x00 at 8N2 @ 90909 = 99 µs low + 22 µs high
    Serial1.write((uint8_t)0);
    Serial1.flush();
    Serial1.updateBaudRate(250000);
    Serial1.write(dmx, sizeof(dmx));
  }
#endif

// ───────────────────────── network ─────────────────────────
static IPAddress ipOf(const uint8_t* a) { return IPAddress(a[0], a[1], a[2], a[3]); }
static IPAddress myIP() {
#if HAS_ETH
  if (ethUp) return ETH.localIP();
#endif
  return apMode ? WiFi.softAPIP() : WiFi.localIP();
}
static const char* linkName() {
#if HAS_ETH
  if (ethUp) return "Ethernet";
#endif
  return apMode ? "WiFi hotspot" : "WiFi";
}

static void startAP() {
  apMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(hostname);                                     // open network "SONOR-Node-xxxx", 192.168.4.1
}

static void netBegin() {
  snprintf(hostname, sizeof(hostname), "SONOR-Node-%02X%02X", mac[4], mac[5]);
#if HAS_ETH
  #if SONOR_BOARD == BOARD_ESP32_W5500
    ETH.begin(ETH_PHY_W5500, 1, W5500_CS, W5500_INT, W5500_RST, SPI3_HOST, W5500_SCK, W5500_MISO, W5500_MOSI);
  #else
    ETH.begin();
  #endif
  ETH.setHostname(hostname);
  if (!cfg.dhcp) ETH.config(ipOf(cfg.ip), ipOf(cfg.gw), ipOf(cfg.mask), ipOf(cfg.gw));
  uint32_t te = millis();
  while (millis() - te < 8000) {                              // wait for link + address
    if (ETH.linkUp() && ETH.localIP() != IPAddress(0, 0, 0, 0)) { ethUp = true; dhcpOk = cfg.dhcp; break; }
    delay(100);
  }
  if (ethUp) return;
#endif
  if (!cfg.ssid[0]) { startAP(); return; }
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(hostname);
  if (!cfg.dhcp) WiFi.config(ipOf(cfg.ip), ipOf(cfg.gw), ipOf(cfg.mask), ipOf(cfg.gw));
  WiFi.begin(cfg.ssid, cfg.pass);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(100);
  if (WiFi.status() == WL_CONNECTED) { dhcpOk = cfg.dhcp; return; }
  startAP();
}

// ───────────────────────── Art-Net ─────────────────────────
static void buildPollReply() {
  memset(reply, 0, sizeof(reply));
  memcpy(reply, "Art-Net", 8);
  reply[8] = OP_POLLREPLY & 0xFF; reply[9] = OP_POLLREPLY >> 8;
  IPAddress ip = myIP(); for (uint8_t i = 0; i < 4; i++) reply[10 + i] = ip[i];
  reply[14] = ARTNET_PORT & 0xFF; reply[15] = ARTNET_PORT >> 8;
  reply[16] = 0; reply[17] = 3;                              // VersInfo 0.3
  reply[18] = cfg.net; reply[19] = cfg.subnet;
  reply[20] = 0x00; reply[21] = 0xFF;                        // OEM (generic)
  reply[23] = 0xD0;                                          // Status1
  reply[24] = 0x7F; reply[25] = 0x7F;                        // ESTA (prototyping)
  strncpy((char*)reply + 26, "SONOR Node", 17);
  snprintf((char*)reply + 44, 63, "SONOR Art-Net to DMX Node v" FW_VERSION " (%s)", linkName());
  snprintf((char*)reply + 108, 63, "#0001 [%04lu] OK - config at http://%s/", (unsigned long)(frames & 0xFFFF), ip.toString().c_str());
  reply[172] = 0; reply[173] = 1;                            // NumPorts = 1
  reply[174] = 0x80;                                         // PortTypes: DMX512 output
  reply[182] = 0x80;                                         // GoodOutput
  reply[190] = SUBUNI;                                       // SwOut
  reply[200] = 0x00;                                         // Style = StNode
  memcpy(reply + 201, mac, 6);
  for (uint8_t i = 0; i < 4; i++) reply[207 + i] = ip[i];    // BindIp
  reply[211] = 1;
  reply[212] = 0x08 | (dhcpOk ? 0x02 : 0) | 0x04;            // Status2
}

static void handleArtnet() {
  int n = udp.parsePacket();
  if (n < 12) return;
  int got = udp.read(pkt, sizeof(pkt));
  if (got < 12 || memcmp(pkt, "Art-Net", 8) != 0) return;
  uint16_t op = pkt[8] | (pkt[9] << 8);
  if (op == OP_DMX && got >= 18) {
    if (pkt[15] != cfg.net || pkt[14] != SUBUNI) return;
    uint16_t len = (pkt[16] << 8) | pkt[17];
    if (len > 512) len = 512;
    if (len > (uint16_t)(got - 18)) len = got - 18;
    memcpy(dmx + 1, pkt + 18, len);
    frames++;
    if (LED_PIN >= 0) digitalWrite(LED_PIN, !digitalRead(LED_PIN));
  } else if (op == OP_POLL) {
    buildPollReply();
    udp.beginPacket(udp.remoteIP(), udp.remotePort() ? udp.remotePort() : ARTNET_PORT);
    udp.write(reply, sizeof(reply));
    udp.endPacket();
  }
}

// ───────────────────────── web config page ─────────────────────────
#include "esp_page.h"   // generated by build.sh from web/node-page.html — edit THAT, not this

#if SONOR_BOARD == BOARD_ESP32_WIFI
  #define BOARD_NAME "ESP32 DevKit (WiFi)"
  #define WIRING "ESP32            MAX485 module        XLR (DMX out)\n5V/VIN --------> VCC\nGND -----------> GND -------------->  pin 1 (shield)\nGPIO17 (TX2) --> DI\nGPIO4 ---------> DE + RE (tied)\n                 A   -------------->  pin 3 (Data+)\n                 B   -------------->  pin 2 (Data-)\nGPIO2 = on-board LED (activity)"
#elif SONOR_BOARD == BOARD_ESP32_W5500
  #define BOARD_NAME "ESP32 + W5500 (Ethernet, WiFi fallback)"
  #define WIRING "ESP32            MAX485 module        XLR (DMX out)\n5V/VIN --------> VCC\nGND -----------> GND -------------->  pin 1 (shield)\nGPIO17 (TX2) --> DI\nGPIO4 ---------> DE + RE (tied)\n                 A   -------------->  pin 3 (Data+)\n                 B   -------------->  pin 2 (Data-)\n\nESP32            W5500 module\n3V3 -----------> VCC   GND -> GND\nGPIO18 --------> SCK\nGPIO19 --------> MISO\nGPIO23 --------> MOSI\nGPIO5 ---------> CS\nGPIO35 --------> INT\nGPIO2 = on-board LED (activity)"
#elif SONOR_BOARD == BOARD_WT32_ETH01
  #define BOARD_NAME "WT32-ETH01 (Ethernet, WiFi fallback)"
  #define WIRING "WT32-ETH01       MAX485 module        XLR (DMX out)\n5V ------------> VCC\nGND -----------> GND -------------->  pin 1 (shield)\nIO17 (TXD2) ---> DI\nIO4 -----------> DE + RE (tied)\n                 A   -------------->  pin 3 (Data+)\n                 B   -------------->  pin 2 (Data-)\nRJ45 on board. Power the module with 5V (not 3.3V)."
#elif SONOR_BOARD == BOARD_OLIMEX_POE
  #define BOARD_NAME "Olimex ESP32-POE (PoE Ethernet, WiFi fallback)"
  #define WIRING "ESP32-POE        MAX485 module        XLR (DMX out)\n5V (EXT) ------> VCC\nGND -----------> GND -------------->  pin 1 (shield)\nGPIO33 --------> DI\nGPIO32 --------> DE + RE (tied)\n                 A   -------------->  pin 3 (Data+)\n                 B   -------------->  pin 2 (Data-)\nPowered by PoE (802.3af) from the switch - no PSU. USB for flashing."
#elif SONOR_BOARD == BOARD_LILYGO_POE
  #define BOARD_NAME "LilyGO T-Internet-POE (PoE Ethernet, WiFi fallback)"
  #define WIRING "T-Internet-POE   MAX485 module        XLR (DMX out)\n5V ------------> VCC\nGND -----------> GND -------------->  pin 1 (shield)\nGPIO33 --------> DI\nGPIO32 --------> DE + RE (tied)\n                 A   -------------->  pin 3 (Data+)\n                 B   -------------->  pin 2 (Data-)\nPowered by PoE (802.3af) from the switch - no PSU. Flash via the LilyGO USB downloader board."
#else
  #define BOARD_NAME "ESP8266 (WiFi)"
  #define WIRING "D1 mini / NodeMCU   MAX485 module     XLR (DMX out)\n5V ---------------> VCC\nGND --------------> GND ----------->  pin 1 (shield)\nD4 (GPIO2, TX1) --> DI\nD1 (GPIO5) -------> DE + RE (tied)\n                    A   ----------->  pin 3 (Data+)\n                    B   ----------->  pin 2 (Data-)\nD4 doubles as the on-board LED - it flickers with DMX, that's normal."
#endif

static String ipStr(const uint8_t* a) { return ipOf(a).toString(); }
static String macStr() {
  char b[18]; snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]); return b;
}
static void sendPage(const String& msg) {
  String p = FPSTR(PAGE);
  p.replace("%BOARD%", BOARD_NAME); p.replace("%FW%", FW_VERSION); p.replace("%MSG%", msg);
  p.replace("%NET%", String(cfg.net)); p.replace("%SUB%", String(cfg.subnet)); p.replace("%UNI%", String(cfg.universe));
  p.replace("%SSID%", cfg.ssid); p.replace("%PASS%", cfg.pass);
  p.replace("%WIFINOTE%", HAS_ETH ? "Only used if the Ethernet cable is unplugged. Leave blank to disable WiFi fallback."
                                  : "Leave blank and the node opens its own hotspot (SONOR-Node-xxxx, 192.168.4.1) so you can set it up.");
  p.replace("%D1%", cfg.dhcp ? "selected" : ""); p.replace("%D0%", cfg.dhcp ? "" : "selected");
  p.replace("%IP%", ipStr(cfg.ip)); p.replace("%MASK%", ipStr(cfg.mask)); p.replace("%GW%", ipStr(cfg.gw));
  p.replace("%LINK%", linkName()); p.replace("%CURIP%", myIP().toString());
  p.replace("%HOW%", apMode ? "hotspot" : (dhcpOk ? "DHCP" : "static")); p.replace("%HOST%", hostname);
  p.replace("%MAC%", macStr()); p.replace("%FRAMES%", String(frames)); p.replace("%WIRING%", WIRING);
  web.send(200, "text/html", p);
}
static bool parseIP(const String& s, uint8_t* out) { IPAddress ip; if (!ip.fromString(s)) return false; for (uint8_t i = 0; i < 4; i++) out[i] = ip[i]; return true; }

static void onRoot() { sendPage(""); }
static void onSave() {
  Config n = cfg;
  n.net = constrain(web.arg("n").toInt(), 0, 127);
  n.subnet = constrain(web.arg("s").toInt(), 0, 15);
  n.universe = constrain(web.arg("u").toInt(), 0, 15);
  n.dhcp = web.arg("d").toInt() ? 1 : 0;
  strlcpy(n.ssid, web.arg("w").c_str(), sizeof(n.ssid));
  strlcpy(n.pass, web.arg("p").c_str(), sizeof(n.pass));
  bool ok = parseIP(web.arg("a"), n.ip) && parseIP(web.arg("m"), n.mask) && parseIP(web.arg("g"), n.gw);
  if (!ok) { sendPage("<div class=ok style='background:#3a1e1e;border-color:#7d2e2e'>Bad IP address — nothing saved.</div>"); return; }
  if (n.net == 127 && n.subnet == 15 && n.universe == 15) { cfg.magic = 0; saveConfig(); }   // factory reset
  else { cfg = n; saveConfig(); }
  sendPage("<div class=ok>Saved. Restarting in 2 s — reconnect to the node's new address if you changed the network.</div>");
  restartAt = millis() + 2000;
}
static void onRestart() { sendPage("<div class=ok>Restarting…</div>"); restartAt = millis() + 500; }

// ───────────────────────── setup / loop ─────────────────────────
void nodeSetup() {
  if (LED_PIN >= 0) { pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, HIGH); }
  if (DMX_DE_PIN >= 0) { pinMode(DMX_DE_PIN, OUTPUT); digitalWrite(DMX_DE_PIN, HIGH); }
  loadConfig();
  memset(dmx, 0, sizeof(dmx));
  dmxBegin();
  WiFi.macAddress(mac);
  netBegin();
  udp.begin(ARTNET_PORT);
  MDNS.begin(hostname);
  web.on("/", onRoot);
  web.on("/save", HTTP_POST, onSave);
  web.on("/save", HTTP_GET, onSave);
  web.on("/restart", HTTP_POST, onRestart);
  web.begin();
  if (LED_PIN >= 0) digitalWrite(LED_PIN, LOW);
}

void nodeLoop() {
  handleArtnet();
  web.handleClient();
#if defined(ESP8266)
  MDNS.update();
#endif
  if (millis() - lastFrame >= 25) { lastFrame = millis(); dmxSend(); }   // ~40 Hz, holds last look
  if (restartAt && millis() > restartAt) ESP.restart();
}
