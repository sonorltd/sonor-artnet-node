# SONOR Art-Net Node — Ethernet / WiFi → DMX512

A single-universe Art-Net node that stands in for an Enttec ODE. It answers ArtPoll discovery, takes ArtDmx on
UDP 6454, drives one DMX line through a MAX485, and hosts its own config page at **http://\<node-ip\>/** for
Net / Sub-Net / Universe, DHCP or static IP (and WiFi credentials on WiFi boards). Settings persist in EEPROM.

**Flash it from the browser, no software:** https://sonorltd.github.io/sonor-artnet-node/ — pick the board,
plug it in by USB, click *Connect & flash* (desktop Chrome / Edge). Wiring schematics, pin tables and setup
notes are on the same page.

## Boards

| # | Board | Link | DMX TX | DE/RE | LED | Notes |
|---|---|---|---|---|---|---|
| 1 | Arduino Nano / Uno + **ENC28J60** shield | Ethernet | D1 | D2 | D3 | The original target. Flash 93 % full — at its ceiling. |
| 2 | Arduino Nano / Uno + **W5500 / W5100** shield | Ethernet | D1 | D2 | D3 | More headroom than the ENC28J60. |
| 3 | **ESP32** DevKit | WiFi | GPIO17 | GPIO4 | GPIO2 (on-board) | Hotspot for first-time setup. |
| 4 | ESP32 DevKit + **W5500** SPI module | Ethernet, WiFi fallback | GPIO17 | GPIO4 | GPIO2 | W5500 on VSPI: SCK 18 / MISO 19 / MOSI 23 / CS 5 / INT 35, 3.3 V. |
| 5 | **WT32-ETH01** (ESP32 + LAN8720) | Ethernet, WiFi fallback | IO17 | IO4 | — | RJ45 on board; flash via USB-serial with IO0 → GND at power-up. |
| 6 | Wemos D1 mini / NodeMCU (**ESP8266**) | WiFi | D4 (GPIO2) | D1 (GPIO5) | — | Hotspot for first-time setup. |
| 7 | **Olimex ESP32-POE / POE-ISO** | PoE Ethernet, WiFi fallback | GPIO33 | GPIO32 | — | 802.3af from the switch. LAN8710 addr 0, power 12, clock out GPIO17. Use a 3.3 V MAX3485. |
| 8 | **LilyGO T-Internet-POE** | PoE Ethernet, WiFi fallback | GPIO33 | GPIO32 | — | 802.3af from the switch. LAN8720 addr 0, power 5, clock out GPIO17. Use a 3.3 V MAX3485. |

All boards: MAX485 VCC → 5 V (MAX3485 → 3V3 on the PoE boards), GND → GND, A → XLR pin 3, B → XLR pin 2, GND → XLR pin 1. DE and RE tied together.

**Shopping list:** the Pages site has a per-board parts list with a PoE / non-PoE toggle and Amazon UK + AliExpress links. Non-PoE Ethernet boards get PoE via a £10 active 802.3af → 5 V USB splitter; the Olimex and LilyGO boards take it natively.

## Config page (on the node)

Browse to the node's IP — `http://2.0.0.10/` with no DHCP, whatever the router handed out otherwise, or
`sonor-node-xxxx.local` on the ESP boards. WiFi boards with no credentials open a hotspot **SONOR-Node-xxxx**
(open) at `http://192.168.4.1/`.

* **Art-Net** — Net 0–127, Sub-Net 0–15, Universe 0–15. Port-Address 0/0/0 is "Universe 1" in most consoles, Universe 0 in QLC+ / MA.
* **Network** — DHCP (with static fallback) or static IP / mask / gateway. WiFi SSID + password on WiFi-capable boards.
* **Status** — link type, IP, MAC, frames received. Wiring + flashing notes.
* Factory reset: Net 127 / Sub-Net 15 / Universe 15 + Save (or re-flash).

On the AVR boards a network change needs a power-cycle; the ESP boards restart themselves.

## Repo layout

```
index.html                  Pages site: board picker, browser flashing, SVG wiring schematics, setup
firmware/<board>/           prebuilt binaries + esp-web-tools manifests (output of build.sh)
web/avrgirl-arduino.js      browser-side AVR flasher (Web Serial, STK500v1)
build.sh                    compiles all 6 variants with arduino-cli into firmware/
sonor-artnet-node/          the Arduino sketch
  sonor-artnet-node.ino       thin entry: picks avr_node.h or esp_node.h
  config.h                    board selection, factory defaults, pin maps
  avr_node.h                  ATmega328P build (EthernetENC or Ethernet + DMXSerial), byte-counted
  esp_node.h                  ESP32 / ESP8266 build (WiFi / ETH, WebServer, mDNS, UART DMX with hardware break)
  src/EthernetENC/            vendored + patched (see CLAUDE.md)
  src/Ethernet/               vendored Arduino Ethernet 2.0.2
  src/DMXSerial/              vendored DMXSerial 1.5.3
```

## Building yourself

Arduino IDE: open `sonor-artnet-node/sonor-artnet-node.ino`, pick the board in Tools, and — for anything but the
defaults (1 on AVR, 3 on ESP32, 6 on ESP8266) — uncomment the matching `#define SONOR_BOARD …` line in
`config.h`. AVR builds need no libraries (bundled). ESP builds need only the `esp32` (3.x) or `esp8266` board
package. Do **not** install EthernetENC from Library Manager alongside this: the bundled copy carries a
one-line patch the stock library needs to link with DMXSerial.

Command line: `bash build.sh` (or `bash build.sh 3` for one board) regenerates `firmware/` for all eight variants.

## Prebuilt hex / bin without the browser

```bash
# Nano, old bootloader (most clones)     — or -b 115200 for a new-bootloader genuine Nano
avrdude -c arduino -p atmega328p -P /dev/cu.usbserial-XXXX -b 57600 -D -U flash:w:firmware/nano-enc28j60/sonor-artnet-node.hex:i
# ESP32
esptool.py --chip esp32 --port /dev/cu.usbserial-XXXX write_flash 0x1000 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 firmware.bin
# ESP8266
esptool.py --chip esp8266 --port /dev/cu.usbserial-XXXX write_flash 0x0 firmware/esp8266-wifi/firmware.bin
```

## Behaviour

* Boots, tries DHCP for 5 s (Ethernet) / joins WiFi for 15 s, else falls back to `2.0.0.10 / 255.0.0.0` (wired) or the setup hotspot (WiFi).
* Answers ArtPoll as **"SONOR Node"** with one output port; ArtDmx by broadcast or unicast.
* DMX line runs continuously at ~40 Hz; the last look is held if the source stops.
* Single universe on every board. The ESP32 has the UARTs for two — on the IDEAS list.

## Bundled libraries

EthernetENC 2.0.5 (Juraj Andrassy — patched: stray `serialPrint()` removed, `UIP_CONF_MAX_CONNECTIONS` 4→1,
`UIP_UDP_BACKLOG` 2→4), Arduino Ethernet 2.0.2, DMXSerial 1.5.3 (Matthias Hertel), avrgirl-arduino 5.0.1
(Suz Hinton, browser build), ESP Web Tools (ESPHome, loaded from unpkg at runtime).
