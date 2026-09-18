#!/usr/bin/env bash
# Build sonor_artnet_dmx.c4z (a .c4z is just a zip with driver.xml at the root).
# Runs the offline harness first when lua is on the PATH (lua5.3 / lua5.1 / lua).
set -euo pipefail
cd "$(dirname "$0")"
OUT="sonor_artnet_dmx.c4z"
LUA="$(command -v lua5.3 || command -v lua5.1 || command -v lua || true)"
if [ -n "$LUA" ]; then "$LUA" test/harness.lua >/dev/null && echo "harness: all passed" || { echo "harness FAILED — not building"; exit 1; }
else echo "harness: skipped (no lua on PATH — brew install lua)"; fi
rm -f "$OUT"
zip -q -r "$OUT" driver.xml driver.lua www
echo "built $OUT ($(du -h "$OUT" | cut -f1)) — Composer › Driver › Add or Update Driver, then File › Add Driver › search 'Sonor Art-Net'"
