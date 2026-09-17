# SONOR Art-Net Node — Arduino Nano + ENC28J60 → DMX512

A single-universe Art-Net → DMX output node that behaves like an Enttec ODE: it appears in
ArtPoll discovery, takes ArtDmx on UDP 6454 and drives one DMX line on the Nano's TX pin.
It hosts its own config page at **http://\<node-ip\>/** — change Net / Sub-Net / Universe, DHCP or
static IP, and read the wiring + flashing notes from a phone on the same network. Settings live in EEPROM.

Docs + downloads: https://sonorltd.github.io/sonor-artnet-node/ · Repo: https://github.com/sonorltd/sonor-artnet-node

Everything needed is in this folder — the two libraries are bundled under `src/`, so there is
nothing to install from Library Manager. Open `sonor-artnet-node.ino` in the Arduino IDE and upload.

## What you need on the bench

| Part | Notes |
|---|---|
| Arduino Nano (ATmega328P) | Clone or genuine. Old or new bootloader both fine. |
| Nano Ethernet shield, ENC28J60 | The one already on site. CS is D10 on almost all of them. |
| MAX485 / SN75176 / MAX3485 module | Any 5 V RS-485 breakout (the little blue module with A/B/DI/DE/RE/RO pins). |
| 3-pin or 5-pin XLR (female) | DMX out. |
| Optional LED + 330 Ω | Activity blink on D3. |

## Wiring

```
Arduino Nano             MAX485 module              XLR (DMX out, female)
────────────             ─────────────              ─────────────────────
5V   ───────────────►  VCC
GND  ───────────────►  GND  ──────────────────────► pin 1  (shield / GND)
D1 (TX) ────────────►  DI
D2   ───────────────►  DE  ┐ (tie DE and RE together)
                       RE  ┘
                       A   ──────────────────────► pin 3  (Data +)
                       B   ──────────────────────► pin 2  (Data −)
                       RO  (leave unconnected)
D3   ──► 330Ω ──► LED ──► GND        (optional activity LED)
```

Notes:

* The ENC28J60 shield uses D10 (CS), D11, D12, D13 (SPI). Don't put anything else on those.
  The built-in LED is on D13 and will flicker with SPI traffic — that's normal, ignore it.
* If you'd rather not use D2, tie DE and RE straight to 5 V and set `DMX_DE_PIN -1` in the sketch.
* Most MAX485 breakouts already have a 120 Ω resistor across A–B. That's fine for a driver at the
  head of the line; terminate the far end of the DMX run as usual.
* D0/D1 are shared with the USB upload. Uploading with the MAX485 connected is fine (DI is an input);
  if an upload ever fails, unplug the DI wire, upload, plug it back.

## Flashing — Arduino IDE (easiest)

1. Arduino IDE → File → Open → `sonor-artnet-node.ino`.
2. Tools → Board → **Arduino Nano**. Tools → Processor → **ATmega328P (Old Bootloader)** for clones,
   **ATmega328P** for a recent genuine Nano. (If the upload fails with "not in sync", swap this.)
3. Tools → Port → the Nano's serial port.
4. Upload. Done — the node is running as soon as the upload finishes.

Do **not** install EthernetENC from Library Manager alongside this: the bundled copy has a one-line
patch (a stray debug function removed) that the stock library needs to co-exist with DMXSerial.

## Flashing — prebuilt hex (no IDE needed)

`build/sonor-artnet-node.ino.hex` is compiled from this exact source (v0.2.0). With avrdude (bundled inside
the Arduino IDE, or `brew install avrdude`):

```bash
# old-bootloader clone (most Nanos)
avrdude -c arduino -p atmega328p -P /dev/cu.usbserial-XXXX -b 57600  -D -U flash:w:build/sonor-artnet-node.ino.hex:i
# new bootloader (genuine Nano since 2018)
avrdude -c arduino -p atmega328p -P /dev/cu.usbserial-XXXX -b 115200 -D -U flash:w:build/sonor-artnet-node.ino.hex:i
```

## Config page (on the node)

Browse to the node's IP (e.g. `http://2.0.0.10/` or whatever DHCP gave it). The page has:

* **Art-Net** — Net (0-127), Sub-Net (0-15), Universe (0-15). Applies immediately on Save.
* **Network** — DHCP (with static fallback) or Static, plus IP / mask / gateway. Applies on next power-cycle.
* **Status** — current IP, MAC, port address. **Wiring** and **Flashing** notes so nobody needs this README on site.
* Factory reset: set Net = 127, Sub-Net = 15, Universe = 15 and Save (or just re-flash).

## Factory defaults (top of the .ino)

| Define | Default | Meaning |
|---|---|---|
| `DEF_NET` / `DEF_SUBNET` / `DEF_UNIVERSE` | 0 / 0 / 0 | Port-Address 0. Most consoles show this as "Universe 1"; QLC+ and MA call it Universe 0. |
| `DEF_DHCP` | 1 | Try DHCP first. |
| `DEF_IP` / `DEF_MASK` / `DEF_GW` | 2.0.0.10 / 255.0.0.0 / 2.0.0.1 | Standard Art-Net range, same as an ODE out of the box. |
| `ETH_CS_PIN` | 10 | Change to 8 if the shield's CS is on D8 (rare). |
| `DMX_DE_PIN` | 2 | MAX485 DE+RE pin. `-1` if wired to 5 V. |
| `LED_PIN` | 3 | Activity LED. `-1` to disable. |
| `DHCP_TIMEOUT_MS` | 5000 | Wait for DHCP this long, then fall back. |

Defaults only apply to a blank EEPROM (first flash). After that the web page's saved values win.

## Network behaviour

* Boots, tries DHCP for 5 s. If the house network hands out an address it uses that.
* No DHCP (direct cable to a laptop, isolated lighting switch) → 2.0.0.10 / 255.0.0.0. Set your
  laptop to e.g. 2.0.0.1 / 255.0.0.0 and it will talk straight away.
* Answers ArtPoll, so it shows up in QLC+, Enttec DMX-Workshop, Resolume, MA, Control4 Art-Net drivers etc.
  as **"SONOR Node"** with one output port.
* Accepts ArtDmx by broadcast (2.255.255.255 / 192.168.x.255) or unicast to its IP.
* Holds the last look if packets stop — it doesn't black out on loss of source.

## Finding the node's IP when on DHCP

Use any Art-Net tool's discovery (QLC+ → Inputs/Outputs → Art-Net plugin, or DMX-Workshop → Node List),
or check the router's DHCP lease table for MAC `02:53:4F:4E:4F:52`.

## Quick test

1. Power the Nano via USB, plug in Ethernet. The D3 LED comes on during boot, goes off once the network is up.
2. In QLC+: Inputs/Outputs → Universe 1 → tick the Art-Net output on your laptop's interface,
   set the target to the node's IP (or leave broadcast). Add a fixture, push a fader.
3. The D3 LED toggles on every Art-Net frame received for our universe. If it isn't toggling, the packets
   aren't reaching it (wrong universe or wrong subnet); if it toggles but nothing lights, look at the DMX wiring.

## Resources

* Flash ≈ 28.7 KB of 30 KB, RAM ≈ 1.4 KB of 2 KB. Single universe only — the 328P doesn't have the RAM for two.
* Refresh rate follows the source; DMX line output is continuous at ~44 Hz regardless.

## Bundled libraries

* `src/EthernetENC` — Juraj Andrassy, EthernetENC 2.0.5 (MIT/LGPL per its LICENSE). Patched: stray `serialPrint()` removed
  from `Ethernet.cpp`; `UIP_CONF_MAX_CONNECTIONS` 4→1 and `UIP_UDP_BACKLOG` 2→4 in `utility/uipethernet-conf.h`.
* `src/DMXSerial` — Matthias Hertel, DMXSerial 1.5.3 (BSD). Unmodified.
