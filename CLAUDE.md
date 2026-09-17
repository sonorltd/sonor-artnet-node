# STUDIO - ArtNet Node (v0.2.0)

> Current version: 0.2.0 · Repo: `sonor-artnet-node` · Pages: https://sonorltd.github.io/sonor-artnet-node/ (docs + hex download).
> Type: side-project / firmware (STUDIO class). Arduino Nano (ATmega328P) + ENC28J60 shield + MAX485 → one universe of DMX out.

Ethernet → DMX512 node that stands in for an Enttec ODE: ArtPoll/ArtPollReply discovery, ArtDmx on UDP 6454,
DMX on the hardware UART via DMXSerial, and a self-hosted config page (http://<node-ip>/) for Net/Sub-Net/Universe
and DHCP/static IP, persisted in EEPROM. **Read `README.md` first.**

## Spine
- Spine version: n/a — **exempt**. Firmware + one static Pages doc page. No Supabase, no shared shell.
  Registered in `workspace-apps.tsv` as `type=side-project, isolation=island-ok`.

## Layout
- `sonor-artnet-node/sonor-artnet-node.ino` — the whole firmware (sketch folder must keep the .ino name for the Arduino IDE). Factory defaults are the `DEF_*` defines at the top; runtime config lives in EEPROM (struct `Config`, magic `0xA7`).
- `sonor-artnet-node/src/EthernetENC/` — vendored EthernetENC 2.0.5, **patched**: stray `serialPrint()` removed from `Ethernet.cpp` (it dragged in HardwareSerial and collided with DMXSerial's USART vectors); `UIP_CONF_MAX_CONNECTIONS` 4→1, `UIP_UDP_BACKLOG` 2→4 in `utility/uipethernet-conf.h`.
- `sonor-artnet-node/src/DMXSerial/` — vendored DMXSerial 1.5.3, unmodified.
- `sonor-artnet-node/build/sonor-artnet-node.ino.hex` — prebuilt for Nano/atmega328 (same hex for old and new bootloader; only the upload baud differs).
- `index.html` (repo root) — the Pages doc page (dark, #4bb9d3 STUDIO accent). Keep in step with README + the on-node page.

## Rules
- Never `#include <EthernetENC.h>` from Library Manager — always the bundled `src/` copy.
- No `Serial.*` anywhere: the UART is the DMX line. Debug with the D3 LED.
- Flash is ~93% full. Any new feature needs a byte-count check (`arduino-cli compile --fqbn arduino:avr:nano:cpu=atmega328old`).
- Universe changes apply live; IP changes require a power-cycle (no WDT reset — old-bootloader Nanos boot-loop on it).
- Version bumps: `FW_VERSION` in the .ino, the `> Current version` line here, README, index.html, and the Master Hub card.

## Data flows
- Reads: Art-Net UDP 6454 (broadcast or unicast), HTTP GET on :80.
- Writes: DMX512 on D1/TX, EEPROM bytes 0-16. No customer data.
