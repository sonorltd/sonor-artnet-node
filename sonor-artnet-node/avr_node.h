#pragma once
// ───────────────────────── AVR build: Arduino Nano / Uno (ATmega328P) ─────────────────────────
#include <EEPROM.h>
#if SONOR_BOARD == BOARD_NANO_W5500
  #include "src/Ethernet/Ethernet.h"          // W5500 / W5100 shields — stock Arduino Ethernet lib (vendored)
#else
  #include "src/EthernetENC/EthernetENC.h"    // ENC28J60 shields — patched copy, see CLAUDE.md
#endif
#include "src/DMXSerial/DMXSerial.h"


Config cfg;

static const uint8_t MAC[] = { 0x02, 0x53, 0x4F, 0x4E, 0x4F, 0x52 };   // locally-administered "SONOR"

EthernetUDP    udp;
EthernetServer web(80);
uint8_t  hdr[18];                    // Art-Net header
uint8_t  chunk[32];                  // DMX data read buffer (small — RAM is tight on a 328P)
char     line[96];                   // HTTP request line
const uint8_t zeros[16] = {0};
bool     dhcpOk = false;
bool     rebootNeeded = false;

const char ARTNET_ID[]   PROGMEM = "Art-Net";
const char SHORT_NAME[]  PROGMEM = "SONOR Node";
const char LONG_NAME[]   PROGMEM = "SONOR Art-Net to DMX Node (Nano) v" FW_VERSION;
const char NODE_REPORT[] PROGMEM = "#0001 [0000] SONOR Node OK - config at http://<ip>/";

#define SUBUNI ((cfg.subnet << 4) | cfg.universe)

// ───────────────────────────── EEPROM config ─────────────────────────────
static void loadConfig() {
  EEPROM.get(0, cfg);
  if (cfg.magic != CFG_MAGIC) {
    const uint8_t ip[] = {DEF_IP}, mask[] = {DEF_MASK}, gw[] = {DEF_GW};
    cfg.magic = CFG_MAGIC; cfg.net = DEF_NET; cfg.subnet = DEF_SUBNET; cfg.universe = DEF_UNIVERSE; cfg.dhcp = DEF_DHCP;
    memcpy(cfg.ip, ip, 4); memcpy(cfg.mask, mask, 4); memcpy(cfg.gw, gw, 4);
    EEPROM.put(0, cfg);
  }
  cfg.net &= 0x7F; cfg.subnet &= 0x0F; cfg.universe &= 0x0F; cfg.dhcp = cfg.dhcp ? 1 : 0;
}

// ───────────────────────────── UDP helpers ─────────────────────────────
static void udpZeros(uint8_t n) {
  while (n) { uint8_t k = n > 16 ? 16 : n; udp.write(zeros, k); n -= k; }
}
static void udpStrP(const char* p, uint8_t fieldLen) {
  uint8_t n = 0; char c;
  while ((c = pgm_read_byte(p++)) && n < fieldLen - 1) { udp.write((uint8_t)c); n++; }
  udpZeros(fieldLen - n);
}
static void udpIP(IPAddress ip) { for (uint8_t i = 0; i < 4; i++) udp.write(ip[i]); }

// ── ArtPollReply (239 bytes) — sent unicast to whoever polled us ──
static void sendPollReply(IPAddress to, uint16_t toPort) {
  IPAddress me = Ethernet.localIP();
  udp.beginPacket(to, toPort ? toPort : ARTNET_PORT);
  udpStrP(ARTNET_ID, 8);                         // 0-7   "Art-Net\0"
  udp.write((uint8_t)(OP_POLLREPLY & 0xFF)); udp.write((uint8_t)(OP_POLLREPLY >> 8)); // 8-9
  udpIP(me);                                     // 10-13 IP
  udp.write((uint8_t)(ARTNET_PORT & 0xFF)); udp.write((uint8_t)(ARTNET_PORT >> 8));   // 14-15
  udp.write((uint8_t)0); udp.write((uint8_t)2);  // 16-17 VersInfo 0.2
  udp.write(cfg.net);                            // 18    NetSwitch
  udp.write(cfg.subnet);                         // 19    SubSwitch
  udp.write((uint8_t)0x00); udp.write((uint8_t)0xFF); // 20-21 OEM (generic)
  udp.write((uint8_t)0);                         // 22    UBEA
  udp.write((uint8_t)0xD0);                      // 23    Status1
  udp.write((uint8_t)0x7F); udp.write((uint8_t)0x7F); // 24-25 ESTA (prototyping)
  udpStrP(SHORT_NAME, 18);                       // 26-43
  udpStrP(LONG_NAME, 64);                        // 44-107
  udpStrP(NODE_REPORT, 64);                      // 108-171
  udp.write((uint8_t)0); udp.write((uint8_t)1);  // 172-173 NumPorts = 1
  udp.write((uint8_t)0x80); udpZeros(3);         // 174-177 PortTypes: DMX512 output
  udpZeros(4);                                   // 178-181 GoodInput
  udp.write((uint8_t)0x80); udpZeros(3);         // 182-185 GoodOutput
  udpZeros(4);                                   // 186-189 SwIn
  udp.write((uint8_t)SUBUNI); udpZeros(3);       // 190-193 SwOut
  udpZeros(6);                                   // 194-199 SwVideo, SwMacro, SwRemote, spare
  udp.write((uint8_t)0x00);                      // 200   Style = StNode
  udp.write(MAC, 6);                             // 201-206 MAC
  udpIP(me);                                     // 207-210 BindIp
  udp.write((uint8_t)1);                         // 211   BindIndex
  udp.write((uint8_t)(0x08 | (dhcpOk ? 0x02 : 0) | 0x04)); // 212 Status2: 15-bit addr, DHCP used, DHCP capable
  udpZeros(26);                                  // 213-238 filler
  udp.endPacket();
}

// ── ArtDmx → DMX out ──
static void handleDmx(uint16_t packetLen) {
  if (hdr[15] != cfg.net || hdr[14] != SUBUNI) return;      // not our universe
  uint16_t len = ((uint16_t)hdr[16] << 8) | hdr[17];
  if (len > 512) len = 512;
  if (len > packetLen - 18) len = packetLen - 18;
  uint16_t ch = 1;
  while (len) {
    uint8_t k = len > sizeof(chunk) ? sizeof(chunk) : len;
    int got = udp.read(chunk, k);
    if (got <= 0) break;
    for (uint8_t i = 0; i < got; i++) DMXSerial.write(ch++, chunk[i]);
    len -= got;
  }
  if (LED_PIN >= 0) digitalWrite(LED_PIN, !digitalRead(LED_PIN));
}

// ───────────────────────────── Web config page ─────────────────────────────
const char HTML_HEAD[] PROGMEM =
  "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n"
  "<!DOCTYPE html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
  "<title>SONOR Art-Net Node</title><style>"
  "body{font:15px/1.5 'DM Sans',system-ui,-apple-system,Segoe UI,Helvetica,Arial,sans-serif;background:#0d0b07;color:#F4F1EC;margin:0;padding:20px}"
  ".w{font-weight:800;letter-spacing:.22em;text-transform:uppercase;font-size:14px}.w i{font-style:normal;color:#8f8574;letter-spacing:.3em;font-size:11px;margin-left:12px;padding-left:12px;border-left:1px solid #3c3330}"
  "h1{font-size:24px;margin:18px 0 2px;color:#c8b48e}h2{font-size:10px;text-transform:uppercase;letter-spacing:.14em;color:#ad9978;margin:22px 0 8px}"
  ".c{max-width:560px;margin:auto}.k{color:#8f8574}label{display:block;margin:10px 0 4px;color:#8f8574;font-size:10px;letter-spacing:.09em;text-transform:uppercase}"
  "input,select{width:100%;box-sizing:border-box;padding:8px 10px;border:1px solid #2a2520;border-radius:7px;background:#221e17;color:#F4F1EC;font-size:15px}"
  ".r{display:grid;grid-template-columns:1fr 1fr 1fr;gap:10px}button{margin-top:16px;padding:12px 24px;border:0;border-radius:2px;background:#ad9978;color:#151310;font-weight:800;font-size:11.5px;letter-spacing:.11em;text-transform:uppercase}"
  "pre{background:#171410;border:1px solid #2a2520;border-radius:8px;padding:12px;font-size:12px;overflow:auto}"
  ".ok{background:#1b2a1e;border:1px solid #2e7d4f;color:#7fc98a;padding:10px 14px;border-radius:8px;margin-bottom:16px}"
  "</style></head><body><div class=c><div class=w>Sonor<i>Art-Net Node</i></div><h1>Art-Net Node</h1><div class=k>Ethernet &rarr; DMX512 &middot; firmware v" FW_VERSION "</div>";

const char HTML_FORM[] PROGMEM =
  "<h2>Art-Net</h2><form action=/save><div class=r>"
  "<div><label>Net (0-127)</label><input name=n type=number min=0 max=127 value=";
const char HTML_F2[] PROGMEM = "></div><div><label>Sub-Net (0-15)</label><input name=s type=number min=0 max=15 value=";
const char HTML_F3[] PROGMEM = "></div><div><label>Universe (0-15)</label><input name=u type=number min=0 max=15 value=";
const char HTML_F4[] PROGMEM =
  "></div></div><div class=k style='font-size:12px;margin-top:6px'>Port-Address 0/0/0 is shown as \"Universe 1\" by most consoles; QLC+ and MA call it Universe 0.</div>"
  "<h2>Network</h2><label>Addressing</label><select name=d><option value=1";
const char HTML_F5[] PROGMEM = ">DHCP (fall back to static below if none)</option><option value=0";
const char HTML_F6[] PROGMEM = ">Static</option></select><label>Static IP</label><input name=a value=";
const char HTML_F7[] PROGMEM = "><label>Subnet mask</label><input name=m value=";
const char HTML_F8[] PROGMEM = "><label>Gateway</label><input name=g value=";
const char HTML_F9[] PROGMEM = "><button>Save</button></form>";

const char HTML_INFO[] PROGMEM =
  "<h2>Status</h2><pre>";
const char HTML_HELP[] PROGMEM =
  "</pre><h2>Wiring</h2><pre>"
  "Nano            MAX485 module        XLR (DMX out)\n"
  "5V   ---------> VCC\n"
  "GND  ---------> GND -------------->  pin 1 (shield)\n"
  "D1 (TX) ------> DI\n"
  "D2   ---------> DE + RE (tied)\n"
  "                A   -------------->  pin 3 (Data+)\n"
  "                B   -------------->  pin 2 (Data-)\n"
  "D3 -> 330R -> LED -> GND   (activity, optional)\n"
  "Shield uses D10-D13 (SPI). Leave them alone.</pre>"
  "<h2>Flashing</h2><pre>"
  "Flash from the browser (no software) or Arduino IDE:\n"
  "https://sonorltd.github.io/sonor-artnet-node/\n"
  "IDE: Board Arduino Nano, Processor ATmega328P (Old Bootloader) for clones.\n"
  "Reset to factory: re-flash, or set Net=127 Sub=15 Univ=15 and Save.</pre>"
  "<h2>Notes</h2><div class=k style='font-size:13px'>Answers ArtPoll as \"SONOR Node\". Accepts ArtDmx by broadcast or unicast. "
  "Holds the last look if the source stops. Art-Net / universe changes apply immediately; "
  "IP changes apply after a power-cycle. Source: github.com/sonorltd/sonor-artnet-node</div>"
  "</div></body></html>";

static void wp(EthernetClient& c, const char* p) {          // print PROGMEM string in chunks
  char buf[32]; uint8_t n;
  while ((n = strlen_P(p)) > 0) {
    uint8_t k = n > sizeof(buf) ? sizeof(buf) : n;
    memcpy_P(buf, p, k); c.write((uint8_t*)buf, k); p += k;
  }
}
static void wip(EthernetClient& c, const uint8_t* ip) {
  for (uint8_t i = 0; i < 4; i++) { c.print(ip[i]); if (i < 3) c.print('.'); }
}

static bool parseIP(const char* s, uint8_t* out) {
  for (uint8_t i = 0; i < 4; i++) {
    if (*s < '0' || *s > '9') return false;
    uint16_t v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s++ - '0'); if (v > 255) return false; }
    out[i] = v;
    if (i < 3) { if (*s != '.') return false; s++; }
  }
  return true;
}

// parse "/save?n=0&s=0&u=1&d=1&a=2.0.0.10&m=255.0.0.0&g=2.0.0.1"
static bool applyQuery(char* q) {
  Config n = cfg;
  char* tok = strtok(q, "&");
  while (tok) {
    char* eq = strchr(tok, '=');
    if (eq && eq[1]) {
      *eq = 0; const char* v = eq + 1; int iv = atoi(v);
      switch (tok[0]) {
        case 'n': n.net = constrain(iv, 0, 127); break;
        case 's': n.subnet = constrain(iv, 0, 15); break;
        case 'u': n.universe = constrain(iv, 0, 15); break;
        case 'd': n.dhcp = iv ? 1 : 0; break;
        case 'a': if (!parseIP(v, n.ip))   return false; break;
        case 'm': if (!parseIP(v, n.mask)) return false; break;
        case 'g': if (!parseIP(v, n.gw))   return false; break;
      }
    }
    tok = strtok(NULL, "&");
  }
  if (n.net == 127 && n.subnet == 15 && n.universe == 15) {   // factory reset combo
    EEPROM.write(0, 0); loadConfig(); rebootNeeded = true; return true;
  }
  rebootNeeded = n.dhcp != cfg.dhcp || memcmp(n.ip, cfg.ip, 4) || memcmp(n.mask, cfg.mask, 4) || memcmp(n.gw, cfg.gw, 4);
  cfg = n;
  EEPROM.put(0, cfg);
  return true;
}

static void servePage(EthernetClient& c, bool saved) {
  wp(c, HTML_HEAD);
  if (saved) {
    wp(c, PSTR("<div class=ok>Saved."));
    if (rebootNeeded) wp(c, PSTR(" Network settings change on next power-cycle — unplug and re-plug the node."));
    wp(c, PSTR("</div>"));
  }
  wp(c, HTML_FORM); c.print(cfg.net);
  wp(c, HTML_F2);   c.print(cfg.subnet);
  wp(c, HTML_F3);   c.print(cfg.universe);
  wp(c, HTML_F4);   if (cfg.dhcp)  wp(c, PSTR(" selected"));
  wp(c, HTML_F5);   if (!cfg.dhcp) wp(c, PSTR(" selected"));
  wp(c, HTML_F6);   wip(c, cfg.ip);
  wp(c, HTML_F7);   wip(c, cfg.mask);
  wp(c, HTML_F8);   wip(c, cfg.gw);
  wp(c, HTML_F9);
  wp(c, HTML_INFO);
  wp(c, PSTR("IP        : ")); c.print(Ethernet.localIP()); wp(c, dhcpOk ? PSTR("  (DHCP)\n") : PSTR("  (static)\n"));
  wp(c, PSTR("MAC       : 02:53:4F:4E:4F:52\nArt-Net   : port 6454, Net ")); c.print(cfg.net);
  wp(c, PSTR(" / Sub-Net ")); c.print(cfg.subnet); wp(c, PSTR(" / Universe ")); c.print(cfg.universe);
  wp(c, PSTR("\nDMX out   : 512 ch continuous, TX pin D1\n"));
  wp(c, HTML_HELP);
}

static void handleWeb() {
  EthernetClient c = web.available();
  if (!c) return;
  uint8_t n = 0; unsigned long t0 = millis();
  while (c.connected() && millis() - t0 < 500) {           // read the request line
    if (!c.available()) continue;
    char ch = c.read();
    if (ch == '\n' || ch == '\r') break;
    if (n < sizeof(line) - 1) line[n++] = ch;
  }
  line[n] = 0;
  while (c.available()) c.read();                           // drain headers (we don't need them)

  bool saved = false;
  char* p = strstr(line, "GET /save?");
  if (p) {
    char* q = p + 10; char* sp = strchr(q, ' '); if (sp) *sp = 0;
    saved = applyQuery(q);
  }
  servePage(c, saved);
  c.flush();
  delay(5);
  c.stop();
}

// ───────────────────────────── setup / loop ─────────────────────────────
void nodeSetup() {
  if (LED_PIN >= 0) { pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, HIGH); }
  loadConfig();

  // DMX first so the line is live (all zeros) while we wait for the network
  DMXSerial.init(DMXController, DMX_DE_PIN);
  DMXSerial.maxChannel(512);

  Ethernet.init(ETH_CS_PIN);
  if (cfg.dhcp) dhcpOk = Ethernet.begin(MAC, DHCP_TIMEOUT_MS, 2000) != 0;
  if (!dhcpOk) {
    IPAddress ip(cfg.ip), mask(cfg.mask), gw(cfg.gw);
    Ethernet.begin(MAC, ip, gw, gw, mask);
  }

  udp.begin(ARTNET_PORT);
  web.begin();
  if (LED_PIN >= 0) digitalWrite(LED_PIN, LOW);
}

void nodeLoop() {
  if (dhcpOk) Ethernet.maintain();               // DHCP lease renewal

  int n = udp.parsePacket();
  if (n >= 12 && udp.read(hdr, sizeof(hdr)) >= 12) {
    bool ok = true;
    for (uint8_t i = 0; i < 8; i++) if (hdr[i] != pgm_read_byte(ARTNET_ID + i)) { ok = false; break; }
    if (ok) {
      uint16_t op = hdr[8] | ((uint16_t)hdr[9] << 8);
      if (op == OP_DMX && n >= 18)  handleDmx((uint16_t)n);
      else if (op == OP_POLL)       sendPollReply(udp.remoteIP(), udp.remotePort());
    }
  }

  handleWeb();
}
