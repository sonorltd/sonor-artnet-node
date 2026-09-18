--[[
  Sonor Art-Net DMX Node — Control4 DriverWorks driver

  Talks Art-Net (ArtDmx on UDP 6454) straight from the controller to a SONOR
  Art-Net Node (or any Art-Net node: Enttec ODE, etc.). Sixteen LIGHT_V2 proxies
  ("Fixture 1..16") each map to a DMX start address + fixture type set in the
  driver's properties, so every fixture shows up in Navigator as a dimmer WITH
  the OS 3.3 colour wheel / CCT slider, works in Lighting Scenes, and keypad
  LEDs track the real level.

  How the colour wheel gets here: OS 3.3+ puts colour in the light_v2 proxy
  itself. Any driver whose proxies advertise supports_color gets the wheel —
  it is not tied to any particular gateway hardware.

  Fixture types (channel order, from the slot's start address):
    Dimmer  1ch  [L]
    RGB     3ch  [R G B]         CCT picks are rendered as a blackbody RGB mix
    RGBW    4ch  [R G B W]       CCT picks drive W, tinted with RGB away from the
                                 W LED's own temperature ("White LED Kelvin");
                                 colour picks optionally move the common part
                                 of RGB onto W ("RGBW White Extraction")

  DMX fixtures don't fade themselves, so this driver runs a 25 Hz software
  fade engine and sends a full 512-channel ArtDmx frame whenever anything is
  moving, plus a slow keep-alive ("Refresh Interval") so a node that reboots
  picks the look back up. The SONOR node holds the last look if frames stop.

  NOTE: written against the DriverWorks LIGHT_V2 API (OS 3.3+) and tested
  offline with a C4 stub, not yet in Composer — check the Lua output on first
  install; the UDP connection handshake (OnConnectionStatusChanged) is the
  most likely thing to need a tweak on your OS build.
]]

DRIVER_VERSION = "0.1.0"
NET_BINDING    = 6001
ARTNET_PORT    = 6454
FIRST_PROXY    = 5001
NUM_SLOTS      = 16
TICK_MS        = 40              -- fade engine period (25 Hz)
POLL_TIMEOUT_S = 10              -- Node Lost after this many seconds without an ArtPollReply

CH_COUNT = { Unused = 0, Dimmer = 1, RGB = 3, RGBW = 4 }

g_online    = false
g_debug     = false
g_dmx       = {}                 -- [1..512] current wire values 0-255
g_slots     = {}                 -- see newSlot()
g_seq       = 0
g_blackout  = false
g_fadeTimer = nil
g_keepTimer = nil
g_pollTimer = nil
g_nodeSeen  = false
g_lastReply = 0
g_identify  = nil                -- { slot, step, timer }

-------------------------------------------------------------------------------- utils
local function dbg(...)
  if g_debug then print("[ArtNet] " .. table.concat({ ... }, " ")) end
end

local function trim(s) return (tostring(s or ""):gsub("^%s+", ""):gsub("%s+$", "")) end
local function clamp(v, lo, hi) if v < lo then return lo elseif v > hi then return hi end return v end
local function round(v) return math.floor(v + 0.5) end
local function prop(name, default) local v = tonumber(Properties[name]); if v == nil then return default end return v end

local function slotOf(idBinding) return idBinding - FIRST_PROXY + 1 end
local function bindingOf(slot) return FIRST_PROXY + slot - 1 end

function newSlot()
  return {
    addr = 0, kind = "Unused",
    power = false, level = 0, lastOn = 100,      -- target intensity (0-100) + last non-zero level for ON
    mode = 0, x = 0.4369, y = 0.4041, cct = 3000, -- target colour (mode 0 = xy, 1 = CCT)
    cur = { 0, 0, 0, 0 }, tgt = { 0, 0, 0, 0 },   -- channel values 0-255 on the wire / where we're heading
    curLevel = 0,
    fade = nil,                                   -- { t0, dur, from = {..}, fromLevel }
  }
end

-------------------------------------------------------------------------------- colour maths
local function srgbToLinear(c)
  c = c / 255
  if c <= 0.04045 then return c / 12.92 end
  return ((c + 0.055) / 1.055) ^ 2.4
end

local function linearToSrgb(c)
  if c <= 0.0031308 then c = c * 12.92 else c = 1.055 * (c ^ (1 / 2.4)) - 0.055 end
  return clamp(round(c * 255), 0, 255)
end

function rgbToXy(r, g, b)
  local R, G, B = srgbToLinear(r), srgbToLinear(g), srgbToLinear(b)
  local X = R * 0.4124 + G * 0.3576 + B * 0.1805
  local Y = R * 0.2126 + G * 0.7152 + B * 0.0722
  local Z = R * 0.0193 + G * 0.1192 + B * 0.9505
  local sum = X + Y + Z
  if sum == 0 then return 0.3127, 0.3290 end
  return X / sum, Y / sum
end

-- CIE xy → sRGB 0-255, normalised so the brightest channel is full (intensity comes from the level)
function xyToRgb(x, y)
  if y == 0 then return 255, 255, 255 end
  local Y = 1.0
  local X = (Y / y) * x
  local Z = (Y / y) * (1 - x - y)
  local R = X * 3.2406 + Y * -1.5372 + Z * -0.4986
  local G = X * -0.9689 + Y * 1.8758 + Z * 0.0415
  local B = X * 0.0557 + Y * -0.2040 + Z * 1.0570
  local m = math.max(R, G, B, 1e-6)
  R, G, B = math.max(0, R / m), math.max(0, G / m), math.max(0, B / m)
  return linearToSrgb(R), linearToSrgb(G), linearToSrgb(B)
end

-- Blackbody Kelvin → RGB 0-255 (Tanner Helland fit), normalised to a full brightest channel
function kelvinToRgb(k)
  local t = clamp(k, 1000, 40000) / 100
  local r, g, b
  if t <= 66 then r = 255 else r = 329.698727446 * ((t - 60) ^ -0.1332047592) end
  if t <= 66 then g = 99.4708025861 * math.log(t) - 161.1195681661 else g = 288.1221695283 * ((t - 60) ^ -0.0755148492) end
  if t >= 66 then b = 255 elseif t <= 19 then b = 0 else b = 138.5177312231 * math.log(t - 10) - 305.0447927307 end
  r, g, b = clamp(r, 0, 255), clamp(g, 0, 255), clamp(b, 0, 255)
  local m = math.max(r, g, b, 1)
  return round(r * 255 / m), round(g * 255 / m), round(b * 255 / m)
end

-- xy of a blackbody colour, for reporting CCT picks back to Navigator on RGB fixtures
function kelvinToXy(k)
  return rgbToXy(kelvinToRgb(k))
end

-------------------------------------------------------------------------------- fixture → channel values
-- Returns the slot's target channel list (0-255 each) for its current power / level / colour.
function computeTarget(z)
  local n = CH_COUNT[z.kind] or 0
  local out = { 0, 0, 0, 0 }
  if n == 0 or not z.power or z.level <= 0 then return out end
  local L = clamp(z.level, 0, 100) / 100

  if z.kind == "Dimmer" then
    out[1] = round(255 * L)

  elseif z.kind == "RGB" then
    local r, g, b
    if z.mode == 1 then r, g, b = kelvinToRgb(z.cct) else r, g, b = xyToRgb(z.x, z.y) end
    out[1], out[2], out[3] = round(r * L), round(g * L), round(b * L)

  elseif z.kind == "RGBW" then
    local r, g, b, w
    if z.mode == 1 then
      -- W LED carries the white; RGB adds only the tint the W LED can't make itself
      local nativeK = prop("White LED Kelvin (RGBW)", 3000)
      local tr, tg, tb = kelvinToRgb(z.cct)
      local nr, ng, nb = kelvinToRgb(nativeK)
      if math.abs(z.cct - nativeK) < 150 then
        r, g, b = 0, 0, 0
      else
        r, g, b = math.max(0, tr - nr), math.max(0, tg - ng), math.max(0, tb - nb)
        local m = math.max(r, g, b, 1)
        -- tint strength grows with distance from the native temperature, capped at 60 % of W
        local strength = clamp(math.abs(z.cct - nativeK) / 3000, 0, 1) * 0.6
        r, g, b = r / m * 255 * strength, g / m * 255 * strength, b / m * 255 * strength
      end
      w = 255
    else
      r, g, b = xyToRgb(z.x, z.y)
      w = 0
      if Properties["RGBW White Extraction"] ~= "Off" then
        w = math.min(r, g, b)
        r, g, b = r - w, g - w, b - w
      end
    end
    out[1], out[2], out[3], out[4] = round(r * L), round(g * L), round(b * L), round(w * L)
  end
  return out
end

-------------------------------------------------------------------------------- Art-Net packets
local function u8(v) return string.char(clamp(math.floor(v), 0, 255)) end

function buildArtDmx()
  g_seq = g_seq + 1
  if g_seq > 255 then g_seq = 1 end
  local net = prop("Art-Net Net", 0)
  local subuni = prop("Art-Net Sub-Net", 0) * 16 + prop("Art-Net Universe", 0)
  local data = {}
  for i = 1, 512 do data[i] = u8(g_blackout and 0 or g_dmx[i]) end
  return "Art-Net\0" .. u8(0x00) .. u8(0x50)         -- OpDmx, little-endian
    .. u8(0) .. u8(14)                              -- protocol version 14
    .. u8(g_seq) .. u8(0)                           -- sequence, physical
    .. u8(subuni) .. u8(net)                        -- SubUni, Net
    .. u8(2) .. u8(0)                               -- length 512 (hi, lo)
    .. table.concat(data)
end

function buildArtPoll()
  return "Art-Net\0" .. u8(0x00) .. u8(0x20) .. u8(0) .. u8(14) .. u8(0) .. u8(0)
end

function sendFrame()
  if not g_online then return end
  C4:SendToNetwork(NET_BINDING, ARTNET_PORT, buildArtDmx())
end

function sendPoll()
  if not g_online then return end
  dbg("ArtPoll →")
  C4:SendToNetwork(NET_BINDING, ARTNET_PORT, buildArtPoll())
end

-- write a slot's current channel values into the DMX buffer
function writeSlot(z)
  local n = CH_COUNT[z.kind] or 0
  if n == 0 or z.addr < 1 then return end
  for i = 1, n do
    local ch = z.addr + i - 1
    if ch <= 512 then g_dmx[ch] = clamp(round(z.cur[i]), 0, 255) end
  end
end

-------------------------------------------------------------------------------- fade engine
function startFade(s, durMs)
  local z = g_slots[s]
  z.tgt = computeTarget(z)
  local tgtLevel = (z.power and z.level) or 0
  durMs = math.max(0, tonumber(durMs) or 0)
  if durMs < TICK_MS then
    z.cur = { z.tgt[1], z.tgt[2], z.tgt[3], z.tgt[4] }
    z.curLevel = tgtLevel
    z.fade = nil
    writeSlot(z)
    notifySlot(s, false)
    sendFrame()
    return
  end
  z.fade = { elapsed = 0, dur = durMs, from = { z.cur[1], z.cur[2], z.cur[3], z.cur[4] }, fromLevel = z.curLevel, toLevel = tgtLevel }
  notifySlot(s, true, durMs)
  if not g_fadeTimer then
    g_fadeTimer = C4:SetTimer(TICK_MS, function() fadeTick() end, true)
  end
end

function fadeTick()
  local active = false
  for s = 1, NUM_SLOTS do
    local z = g_slots[s]
    if z.fade then
      local f = z.fade
      f.elapsed = f.elapsed + TICK_MS               -- tick-counted: Director has no ms wall clock
      local p = clamp(f.elapsed / f.dur, 0, 1)
      for i = 1, 4 do z.cur[i] = f.from[i] + (z.tgt[i] - f.from[i]) * p end
      z.curLevel = f.fromLevel + (f.toLevel - f.fromLevel) * p
      writeSlot(z)
      if p >= 1 then
        z.fade = nil
        z.curLevel = f.toLevel
        notifySlot(s, false)
      else
        active = true
      end
    end
  end
  sendFrame()
  if not active and g_fadeTimer then
    g_fadeTimer:Cancel()
    g_fadeTimer = nil
  end
end

-------------------------------------------------------------------------------- proxy notifications
function notifySlot(s, changing, rate)
  local z = g_slots[s]
  local b = bindingOf(s)
  local tgtLevel = (z.power and z.level) or 0
  if changing then
    C4:SendToProxy(b, "LIGHT_BRIGHTNESS_CHANGING", { LIGHT_BRIGHTNESS_TARGET = tgtLevel, RATE = rate or 0 }, "NOTIFY")
  else
    C4:SendToProxy(b, "LIGHT_LEVEL_CHANGED", { LEVEL = tgtLevel }, "NOTIFY")
    C4:SendToProxy(b, "LIGHT_BRIGHTNESS_CHANGED", { LIGHT_BRIGHTNESS_CURRENT = tgtLevel, LIGHT_BRIGHTNESS_TARGET = tgtLevel }, "NOTIFY")
  end
  if z.kind == "RGB" or z.kind == "RGBW" then
    local x, y = z.x, z.y
    if z.mode == 1 then x, y = kelvinToXy(z.cct) end
    local t = {
      LIGHT_COLOR_CURRENT_X = x, LIGHT_COLOR_CURRENT_Y = y, LIGHT_COLOR_CURRENT_COLOR_MODE = z.mode,
      LIGHT_COLOR_TARGET_X = x, LIGHT_COLOR_TARGET_Y = y, LIGHT_COLOR_TARGET_COLOR_MODE = z.mode,
    }
    if z.mode == 1 then t.LIGHT_COLOR_CURRENT_CCT = z.cct; t.LIGHT_COLOR_TARGET_CCT = z.cct end
    C4:SendToProxy(b, changing and "LIGHT_COLOR_CHANGING" or "LIGHT_COLOR_CHANGED", t, "NOTIFY")
  end
end

function announceCapabilities(s)
  local z = g_slots[s]
  local colour = (z.kind == "RGB" or z.kind == "RGBW") and "true" or "false"
  C4:SendToProxy(bindingOf(s), "DYNAMIC_CAPABILITIES_CHANGED", {
    dimmer = "true", supports_target = "true",
    supports_color = colour, supports_color_correlated_temperature = colour,
    color_correlated_temperature_min = "1800", color_correlated_temperature_max = "6500",
  }, "NOTIFY")
  C4:SendToProxy(bindingOf(s), "ONLINE_CHANGED", { STATE = (z.kind ~= "Unused") and "true" or "false" }, "NOTIFY")
end

-------------------------------------------------------------------------------- lifecycle
function OnDriverInit()
  g_debug = (Properties["Debug"] == "On")
  for i = 1, 512 do g_dmx[i] = 0 end
  for s = 1, NUM_SLOTS do g_slots[s] = newSlot() end
end

function OnDriverLateInit()
  C4:UpdateProperty("Driver Version", DRIVER_VERSION)
  for s = 1, NUM_SLOTS do readSlotProps(s); announceCapabilities(s) end
  connect()
end

function OnDriverDestroyed()
  if g_fadeTimer then g_fadeTimer:Cancel() end
  if g_keepTimer then g_keepTimer:Cancel() end
  if g_pollTimer then g_pollTimer:Cancel() end
end

function readSlotProps(s)
  local z = g_slots[s]
  z.addr = prop("Slot " .. s .. " Address", 0)
  z.kind = Properties["Slot " .. s .. " Type"] or "Unused"
  if not CH_COUNT[z.kind] then z.kind = "Unused" end
end

function OnPropertyChanged(strProperty)
  if strProperty == "Debug" then
    g_debug = (Properties["Debug"] == "On")
  elseif strProperty == "Node IP" then
    connect()
  elseif strProperty == "Refresh Interval (s)" then
    startKeepAlive()
  else
    local s = strProperty:match("^Slot (%d+) ")
    if s then
      s = tonumber(s)
      local z = g_slots[s]
      -- clear the old channels before the slot moves, so nothing is left lit at the old address
      for i = 1, 4 do z.cur[i] = 0 end
      writeSlot(z)
      readSlotProps(s)
      announceCapabilities(s)
      startFade(s, 0)
    end
  end
end

function connect()
  local ip = trim(Properties["Node IP"])
  g_online = false
  g_nodeSeen = false
  if ip == "" then C4:UpdateProperty("Node Status", "Set Node IP") return end
  C4:NetDisconnect(NET_BINDING, ARTNET_PORT)
  C4:CreateNetworkConnection(NET_BINDING, ip)
  C4:NetConnect(NET_BINDING, ARTNET_PORT)
  C4:UpdateProperty("Node Status", "Opening UDP to " .. ip)
end

function OnConnectionStatusChanged(idBinding, nPort, strStatus)
  if idBinding ~= NET_BINDING then return end
  dbg("connection", strStatus)
  if strStatus == "ONLINE" then
    g_online = true
    C4:UpdateProperty("Node Status", "UDP open — polling")
    sendPoll()
    sendFrame()
    startKeepAlive()
    if g_pollTimer then g_pollTimer:Cancel() end
    g_pollTimer = C4:SetTimer(5000, function() pollTick() end, true)
  else
    g_online = false
    C4:UpdateProperty("Node Status", "UDP closed (" .. tostring(strStatus) .. ")")
  end
end

function startKeepAlive()
  if g_keepTimer then g_keepTimer:Cancel(); g_keepTimer = nil end
  local secs = prop("Refresh Interval (s)", 2)
  if secs > 0 and g_online then
    g_keepTimer = C4:SetTimer(secs * 1000, function() if not g_fadeTimer then sendFrame() end end, true)
  end
end

function pollTick()
  if g_nodeSeen and (os.time() - g_lastReply) > POLL_TIMEOUT_S then
    g_nodeSeen = false
    C4:UpdateProperty("Node Status", "Node not answering ArtPoll")
    C4:FireEvent("Node Lost")
  end
  sendPoll()
end

-------------------------------------------------------------------------------- node → driver (ArtPollReply)
function ReceivedFromNetwork(idBinding, nPort, strData)
  if idBinding ~= NET_BINDING then return end
  if #strData < 26 or strData:sub(1, 8) ~= "Art-Net\0" then return end
  local op = strData:byte(9) + strData:byte(10) * 256
  if op == 0x2100 then
    local ip = string.format("%d.%d.%d.%d", strData:byte(11), strData:byte(12), strData:byte(13), strData:byte(14))
    local short = (strData:sub(27, 44):match("^[^%z]*")) or ""
    local long = (#strData >= 108 and strData:sub(45, 108):match("^[^%z]*")) or ""
    local net, sub = strData:byte(19) or 0, strData:byte(20) or 0
    dbg("ArtPollReply ←", ip, short, long)
    g_lastReply = os.time()
    C4:UpdateProperty("Node Status", string.format("%s @ %s (Net %d / Sub-Net %d) — %s", short ~= "" and short or "Node", ip, net, sub, long))
    if not g_nodeSeen then
      g_nodeSeen = true
      C4:FireEvent("Node Found")
      sendFrame()                                   -- a freshly booted node starts all-zero
    end
  end
end

-------------------------------------------------------------------------------- proxy (Navigator / programming) → DMX
function ReceivedFromProxy(idBinding, strCommand, tParams)
  tParams = tParams or {}
  if idBinding < FIRST_PROXY or idBinding >= FIRST_PROXY + NUM_SLOTS then return end
  local s = idBinding - FIRST_PROXY + 1
  local z = g_slots[s]
  local fade = prop("Default Fade (ms)", 500)
  dbg("proxy", tostring(s), strCommand)

  if strCommand == "ON" then
    setLevel(s, z.lastOn > 0 and z.lastOn or prop("Default On Level", 100), fade)
  elseif strCommand == "OFF" then
    setLevel(s, 0, fade)
  elseif strCommand == "TOGGLE" then
    if z.power then setLevel(s, 0, fade) else setLevel(s, z.lastOn > 0 and z.lastOn or prop("Default On Level", 100), fade) end
  elseif strCommand == "SET_LEVEL" then
    setLevel(s, tonumber(tParams.LEVEL) or 0, fade)
  elseif strCommand == "RAMP_TO_LEVEL" then
    setLevel(s, tonumber(tParams.LEVEL) or 0, tonumber(tParams.TIME) or fade)
  elseif strCommand == "SET_BRIGHTNESS_TARGET" then
    setLevel(s, tonumber(tParams.LIGHT_BRIGHTNESS_TARGET) or 0, tonumber(tParams.RATE) or fade)
  elseif strCommand == "SET_COLOR_TARGET" then
    setColour(s, tParams)
    startFade(s, tonumber(tParams.RATE) or fade)
  elseif strCommand == "SET_BRIGHTNESS_COLOR_TARGET" then
    setColour(s, tParams)
    if tParams.LIGHT_BRIGHTNESS_TARGET then
      applyLevel(z, tonumber(tParams.LIGHT_BRIGHTNESS_TARGET) or 0)
    end
    startFade(s, tonumber(tParams.RATE) or fade)
  elseif strCommand == "GET_LIGHT_LEVEL" or strCommand == "GET_STATE" or strCommand == "GET_COLOR" then
    notifySlot(s, false)
  elseif strCommand == "BUTTON_ACTION" then
    local bid, act = tonumber(tParams.BUTTON_ID), tonumber(tParams.ACTION)
    if act == 1 then
      if bid == 0 then setLevel(s, z.lastOn > 0 and z.lastOn or 100, fade)
      elseif bid == 1 then setLevel(s, 0, fade)
      else if z.power then setLevel(s, 0, fade) else setLevel(s, z.lastOn > 0 and z.lastOn or 100, fade) end end
    end
  end
end

function applyLevel(z, level)
  level = clamp(round(level), 0, 100)
  if level > 0 then z.power = true; z.level = level; z.lastOn = level
  else z.power = false; z.level = 0 end
end

function setLevel(s, level, fadeMs)
  applyLevel(g_slots[s], level)
  startFade(s, fadeMs)
end

function setColour(s, t)
  local z = g_slots[s]
  local mode = tonumber(t.LIGHT_COLOR_TARGET_MODE) or tonumber(t.LIGHT_COLOR_TARGET_COLOR_MODE) or 0
  if mode == 1 and t.LIGHT_COLOR_TARGET_CCT then
    z.mode = 1
    z.cct = clamp(tonumber(t.LIGHT_COLOR_TARGET_CCT) or 3000, 1800, 6500)
    z.x, z.y = kelvinToXy(z.cct)
  else
    local x, y = tonumber(t.LIGHT_COLOR_TARGET_X), tonumber(t.LIGHT_COLOR_TARGET_Y)
    if x and y then z.mode = 0; z.x, z.y = x, y end
  end
end

-------------------------------------------------------------------------------- Composer commands + actions
function ExecuteCommand(strCommand, tParams)
  tParams = tParams or {}
  dbg("cmd", strCommand)
  if strCommand == "LUA_ACTION" then
    if tParams.ACTION == "Reconnect" then connect()
    elseif tParams.ACTION == "Poll" then sendPoll()
    elseif tParams.ACTION == "Resend" then sendFrame()
    elseif tParams.ACTION == "AllOff" then for s = 1, NUM_SLOTS do setLevel(s, 0, 0) end
    end
  elseif strCommand == "Set Channel" then
    local ch = tonumber(tParams.Channel)
    if ch and ch >= 1 and ch <= 512 then g_dmx[ch] = clamp(round(tonumber(tParams.Value) or 0), 0, 255); sendFrame() end
  elseif strCommand == "Set Fixture Colour" then
    local x, y = rgbToXy(tonumber(tParams.Red) or 0, tonumber(tParams.Green) or 0, tonumber(tParams.Blue) or 0)
    eachFixture(tonumber(tParams.Fixture), function(s)
      local z = g_slots[s]
      z.mode, z.x, z.y = 0, x, y
      if not z.power then applyLevel(z, z.lastOn > 0 and z.lastOn or 100) end
      startFade(s, tonumber(tParams["Fade ms"]) or prop("Default Fade (ms)", 500))
    end)
  elseif strCommand == "Set Fixture Level" then
    eachFixture(tonumber(tParams.Fixture), function(s)
      setLevel(s, tonumber(tParams.Level) or 0, tonumber(tParams["Fade ms"]) or prop("Default Fade (ms)", 500))
    end)
  elseif strCommand == "Blackout" then
    g_blackout = (tParams.State == "On")
    sendFrame()
    C4:FireEvent(g_blackout and "Blackout On" or "Blackout Off")
  elseif strCommand == "Identify Fixture" then
    identify(tonumber(tParams.Fixture))
  end
end

function eachFixture(which, fn)
  if which and which >= 1 and which <= NUM_SLOTS then fn(which) return end
  for s = 1, NUM_SLOTS do if g_slots[s].kind ~= "Unused" then fn(s) end end
end

-- flash a fixture full white three times (600 ms period) then put its real look back
function identify(s)
  if not s or not g_slots[s] or g_slots[s].kind == "Unused" or g_identify then return end
  local z = g_slots[s]
  local n = CH_COUNT[z.kind]
  local saved = { z.cur[1], z.cur[2], z.cur[3], z.cur[4] }
  g_identify = { slot = s, step = 0 }
  g_identify.timer = C4:SetTimer(300, function()
    g_identify.step = g_identify.step + 1
    local on = (g_identify.step % 2 == 1)
    for i = 1, n do z.cur[i] = on and 255 or 0 end
    if g_identify.step >= 6 then
      z.cur = saved
      writeSlot(z); sendFrame()
      g_identify.timer:Cancel()
      g_identify = nil
      return
    end
    writeSlot(z); sendFrame()
  end, true)
end
