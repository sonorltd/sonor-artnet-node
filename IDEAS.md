# IDEAS — STUDIO - ArtNet Node

- [ ] Two universes on ESP32 (UART1 + UART2, two MAX485s) — the RAM and pins are there.
- [ ] sACN (E1.31) receive alongside Art-Net on the ESP builds.
- [ ] RDM on the DMX line (needs the RX path + DE toggling; ESP32 only realistically).
- [ ] HTP/LTP merge of two Art-Net sources.
- [ ] ESP32-S3 / C3 variants (different bootloader offset 0x0 in the manifest).
- [ ] OTA update from the Pages site for the ESP boards (ArduinoOTA / HTTP update pointing at firmware/<board>/firmware.bin).
- [ ] 3D-printable enclosure with XLR + RJ45 cut-outs (CAD folder).
- [ ] Control4 driver notes: which Art-Net drivers see the ArtPollReply cleanly.
- [ ] Nano: a second bootloader-detect attempt in the browser flasher (try 57600, then 115200 automatically).
