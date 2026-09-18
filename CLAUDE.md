# STUDIO - ArtNet Node (v0.3.1)

> Current version: 0.3.1 · Repo: `sonor-artnet-node` · Pages: https://sonorltd.github.io/sonor-artnet-node/ (board picker, browser flashing, wiring schematics).
> Type: side-project / firmware (STUDIO class). Eight board variants (incl. two PoE) → one universe of DMX out. Enttec ODE stand-in.

Art-Net → DMX512 node: ArtPoll/ArtPollReply discovery, ArtDmx on UDP 6454, MAX485 line driver, self-hosted config
page (Net/Sub-Net/Universe, DHCP/static, WiFi creds), settings in EEPROM. **Read `README.md` first.**

## Spine
- Spine version: n/a — **exempt**. Firmware + one static Pages page. No Supabase, no shared shell.
  Registered in `workspace-apps.tsv` as `type=side-project, isolation=island-ok`.

## Layout
- `sonor-artnet-node/` — the sketch. `config.h` = board select (`SONOR_BOARD` 1–8; 7 = Olimex ESP32-POE, 8 = LilyGO T-Internet-POE, both LAN8720-class with clock out on GPIO17, DMX on 33/32), factory defaults, pin maps.
  `avr_node.h` = ATmega328P build (byte-counted; ENC28J60 variant is 93 % flash). `esp_node.h` = ESP32/ESP8266 build.
- `sonor-artnet-node/src/` — vendored libs. **Every .cpp/.c under src/ is guarded** with `#include "…/config.h"` +
  `#if SONOR_BOARD == …` because the IDE compiles all of src/ for every target. `src/Ethernet/utility/w5100.cpp`
  includes `"../Ethernet.h"` (relative) so a globally installed Ethernet lib is never pulled in.
  EthernetENC patch: stray `serialPrint()` removed from `Ethernet.cpp` (dragged in HardwareSerial → clashed with
  DMXSerial's USART vectors); `UIP_CONF_MAX_CONNECTIONS` 4→1, `UIP_UDP_BACKLOG` 2→4.
- `build.sh` — arduino-cli, all eight variants → `firmware/<board>/` (hex for AVR; bootloader/partitions/boot_app0/firmware + `manifest.json` for ESP32; firmware + manifest for ESP8266). `firmware/VERSION` is read by the page.
- **Theme**: Sonor luxury theme lifted from APP - Lighting Design (bg #090807/#171410/#221e17, cream #F4F1EC, gold #ad9978/#c8b48e, amber #f5d05c, DM Sans; Gilroy deliberately NOT shipped — licensed font, keep it off a public repo). Logo = the house mark from `data/sonor-header.js` WORDMARK_SVG paths 1–3, inline SVG; `web/favicon.svg` gold variant.
- `web/node-page.html` — the ONE source for the on-node page (ESP builds): `build.sh` turns it into `sonor-artnet-node/esp_page.h` (PROGMEM raw string, tokens `%NET%` etc. filled in `esp_node.h`), and the Pages site's *Node web page preview* tab fetches it and fills sample values into an iframe. Edit the template, run `build.sh`, commit both. AVR page is a separate, byte-counted string in `avr_node.h` (text wordmark, same palette, no SVG — flash is at 94 %).
- `index.html` — Pages site with four tabs (Build & flash / Node web page preview / Limits & notes / Control4 driver). Tab 4 (`#view-c4`) explains the driver and links `control4/sonor_artnet_dmx.c4z` — committed on purpose so Pages serves it; rebuild + commit the .c4z with every driver change. `BOARDS[]` drives the picker, flash panel, pin table, setup text and the SVG schematic; `PARTS`/`BOM` drive the shopping list (PoE toggle adds an 802.3af→5 V splitter to non-PoE wired boards; links are dated listings + search fallbacks)
  (`renderSchematic()` — generic: MCU block, MAX485, XLR block, optional modules, per-net bus columns). AVR flashing =
  `web/avrgirl-arduino.js` (Web Serial STK500v1; board `nano` = 57600 old bootloader, `nano (new bootloader)` = 115200,
  `uno`). ESP flashing = `<esp-web-install-button>` from unpkg esp-web-tools@10 + same-origin manifests.
- `control4/` — **Control4 DriverWorks driver** (`sonor_artnet_dmx.c4z`, built by `control4/build-c4z.sh`). 16 `light_v2` fixture proxies (Dimmer / RGB / RGBW, per-slot DMX address + type properties) → ArtDmx over UDP 6454 unicast to the node, from the controller itself. Colour wheel + CCT come from `supports_color` on the light_v2 proxy (OS 3.3+), no eDIDIO/gateway needed. 25 Hz software fade engine, 2 s keep-alive frame, ArtPoll → Node Found/Lost events. `control4/test/harness.lua` stubs the C4 API (`lua5.3 test/harness.lua` from `control4/`) — run before every build; **not yet run in Composer** (UDP handshake in `OnConnectionStatusChanged` is the first thing to check).
- Master Hub card: `sonor-master/index.html` `data-app-key="artnet-node"` → `../STUDIO - ArtNet Node/index.html`; hosted URL in appUrls.

## Rules
- No `Serial.*` on AVR — the UART is the DMX line. Debug with the D3 LED. ESP builds use Serial2 (ESP32) / Serial1 (ESP8266) for DMX.
- Any AVR change → re-run `build.sh 1` and check the byte count; 650 B of RAM headroom on the ENC28J60 build is the floor.
- ESP32 DMX break is done with `uart_set_line_inverse` (180 µs) + 20 µs MAB then async `uart_write_bytes`; don't swap for
  `uart_write_bytes_with_break` (break lands after the frame with no guaranteed MAB).
- AVR: IP changes need a power-cycle (no WDT reset — old-bootloader Nanos boot-loop). ESP: `ESP.restart()` after save.
- Version bump = `FW_VERSION` in `config.h`, this file, README, `index.html` fallback text, `bash build.sh`, Master Hub card + `app_versions`.
- `firmware/` binaries are committed on purpose (Pages serves them). ~3.6 MB total; keep it that way (no merged 4 MB images).

## Data flows
- Reads: Art-Net UDP 6454 (broadcast/unicast), HTTP on :80, mDNS (ESP).
- Writes: DMX512 out, EEPROM/flash config. No customer data.
