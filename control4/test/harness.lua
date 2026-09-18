-- Offline harness: stubs the DriverWorks C4 API, loads driver.lua, and checks
-- ArtDmx bytes, channel mapping, colour maths and the fade engine.
--   lua5.3 test/harness.lua   (run from control4/)

local sent, notes, props, timers, events = {}, {}, {}, {}, {}
Properties = setmetatable({}, { __index = function(_, k) return props[k] end })

local Timer = {}
Timer.__index = Timer
function Timer:Cancel() self.dead = true end

C4 = {
  SendToNetwork = function(_, b, port, data) sent[#sent + 1] = { b = b, port = port, data = data } end,
  SendToProxy = function(_, b, cmd, t) notes[#notes + 1] = { b = b, cmd = cmd, t = t } end,
  UpdateProperty = function(_, k, v) props[k] = tostring(v) end,
  FireEvent = function(_, e) events[#events + 1] = e end,
  SetTimer = function(_, ms, fn, rep) local t = setmetatable({ ms = ms, fn = fn, rep = rep }, Timer); timers[#timers + 1] = t; return t end,
  NetDisconnect = function() end, CreateNetworkConnection = function() end, NetConnect = function() end,
}
local function fire(ms) for _, t in ipairs(timers) do if not t.dead and t.ms == ms then t.fn() end end end

local function set(k, v) props[k] = tostring(v) end
set("Debug", "Off"); set("Node IP", "2.0.0.10"); set("Art-Net Net", 0); set("Art-Net Sub-Net", 0); set("Art-Net Universe", 3)
set("Refresh Interval (s)", 2); set("Default Fade (ms)", 500); set("Default On Level", 100)
set("White LED Kelvin (RGBW)", 3000); set("RGBW White Extraction", "On")
for s = 1, 16 do set("Slot " .. s .. " Address", 0); set("Slot " .. s .. " Type", "Unused") end
set("Slot 1 Address", 1);  set("Slot 1 Type", "RGBW")
set("Slot 2 Address", 10); set("Slot 2 Type", "Dimmer")
set("Slot 3 Address", 20); set("Slot 3 Type", "RGB")

dofile("driver.lua")
local fails = 0
local function check(cond, msg) if cond then print("  ok   " .. msg) else fails = fails + 1; print("  FAIL " .. msg) end end

OnDriverInit(); OnDriverLateInit()
OnConnectionStatusChanged(6001, 6454, "ONLINE")

print("ArtPoll + first frame on ONLINE")
check(#sent == 2, "two packets sent")
check(sent[1].data:sub(1, 8) == "Art-Net\0" and sent[1].data:byte(9) == 0 and sent[1].data:byte(10) == 0x20, "ArtPoll opcode 0x2000")
local f = sent[2].data
check(#f == 18 + 512, "ArtDmx is 530 bytes")
check(f:byte(9) == 0 and f:byte(10) == 0x50, "OpDmx 0x5000 little-endian")
check(f:byte(11) == 0 and f:byte(12) == 14, "protocol version 14")
check(f:byte(15) == 3 and f:byte(16) == 0, "SubUni = sub 0 / universe 3, Net 0")
check(f:byte(17) == 2 and f:byte(18) == 0, "length 512 big-endian")

print("Dimmer slot 2 → SET_LEVEL 50, no fade")
set("Default Fade (ms)", 0)
sent = {}
ReceivedFromProxy(5002, "SET_LEVEL", { LEVEL = 50 })
f = sent[#sent].data
check(f:byte(18 + 10) == 128, "channel 10 = 128 (50 %)")
check(f:byte(18 + 11) == 0, "channel 11 untouched")

print("RGB slot 3 → full red via xy, level 100")
ReceivedFromProxy(5003, "SET_BRIGHTNESS_COLOR_TARGET", { LIGHT_BRIGHTNESS_TARGET = 100, LIGHT_COLOR_TARGET_X = 0.64, LIGHT_COLOR_TARGET_Y = 0.33, LIGHT_COLOR_TARGET_MODE = 0, RATE = 0 })
f = sent[#sent].data
local r, g, b = f:byte(18 + 20), f:byte(18 + 21), f:byte(18 + 22)
print("    rgb =", r, g, b)
check(r == 255 and g < 40 and b < 40, "red dominant on channels 20-22")

print("RGBW slot 1 → CCT 3000 K (native) → W only")
ReceivedFromProxy(5001, "SET_BRIGHTNESS_COLOR_TARGET", { LIGHT_BRIGHTNESS_TARGET = 100, LIGHT_COLOR_TARGET_MODE = 1, LIGHT_COLOR_TARGET_CCT = 3000, RATE = 0 })
f = sent[#sent].data
check(f:byte(19) == 0 and f:byte(20) == 0 and f:byte(21) == 0 and f:byte(22) == 255, "W=255, RGB=0")
local last = notes[#notes]
check(last.cmd == "LIGHT_COLOR_CHANGED" and last.t.LIGHT_COLOR_CURRENT_CCT == 3000 and last.t.LIGHT_COLOR_CURRENT_COLOR_MODE == 1, "reports CCT mode 3000 K back to proxy")

print("RGBW slot 1 → CCT 6500 K → W + cool tint")
ReceivedFromProxy(5001, "SET_COLOR_TARGET", { LIGHT_COLOR_TARGET_MODE = 1, LIGHT_COLOR_TARGET_CCT = 6500, RATE = 0 })
f = sent[#sent].data
r, g, b = f:byte(19), f:byte(20), f:byte(21)
print("    rgbw =", r, g, b, f:byte(22))
check(f:byte(22) == 255 and b > r and b > 0, "W full, blue tint added")

print("RGBW slot 1 → white via xy (D65) → extraction moves it to W")
ReceivedFromProxy(5001, "SET_COLOR_TARGET", { LIGHT_COLOR_TARGET_MODE = 0, LIGHT_COLOR_TARGET_X = 0.3127, LIGHT_COLOR_TARGET_Y = 0.3290, RATE = 0 })
f = sent[#sent].data
r, g, b = f:byte(19), f:byte(20), f:byte(21)
print("    rgbw =", r, g, b, f:byte(22))
check(f:byte(22) >= 240 and r < 20 and g < 20 and b < 20, "near-white lands on W")

print("Fade: slot 2 dimmer 50 → 0 over 400 ms (10 ticks)")
sent = {}; notes = {}
ReceivedFromProxy(5002, "RAMP_TO_LEVEL", { LEVEL = 0, TIME = 400 })
check(notes[1].cmd == "LIGHT_BRIGHTNESS_CHANGING" and notes[1].t.LIGHT_BRIGHTNESS_TARGET == 0 and notes[1].t.RATE == 400, "CHANGING notified with target/rate")
fire(40); fire(40); fire(40); fire(40); fire(40)
f = sent[#sent].data
print("    ch10 after 5 ticks =", f:byte(18 + 10))
check(f:byte(18 + 10) == 64, "half-way = 64")
fire(40); fire(40); fire(40); fire(40); fire(40)
f = sent[#sent].data
check(f:byte(18 + 10) == 0, "ends at 0")
check(notes[#notes].cmd == "LIGHT_BRIGHTNESS_CHANGED" and notes[#notes].t.LIGHT_BRIGHTNESS_CURRENT == 0, "CHANGED notified at end")
check(g_fadeTimer == nil, "fade timer released")

print("ON restores last level")
ReceivedFromProxy(5002, "ON", {})
f = sent[#sent].data
check(f:byte(18 + 10) == 128, "back to 50 % (128)")

print("Blackout + raw channel")
ExecuteCommand("Blackout", { State = "On" })
f = sent[#sent].data
local allZero = true
for i = 1, 512 do if f:byte(18 + i) ~= 0 then allZero = false end end
check(allZero, "every channel 0 while blackout")
ExecuteCommand("Blackout", { State = "Off" })
f = sent[#sent].data
check(f:byte(18 + 10) == 128, "look restored on blackout off")
ExecuteCommand("Set Channel", { Channel = 500, Value = 77 })
check(sent[#sent].data:byte(18 + 500) == 77, "raw channel 500 = 77")

print("Slot re-address clears old channels")
ReceivedFromProxy(5002, "SET_LEVEL", { LEVEL = 100 })
set("Slot 2 Address", 30); OnPropertyChanged("Slot 2 Address")
f = sent[#sent].data
check(f:byte(18 + 10) == 0 and f:byte(18 + 30) == 255, "ch10 cleared, ch30 lit")

print("ArtPollReply parsing")
local reply = "Art-Net\0" .. string.char(0x00, 0x21) .. string.char(2, 0, 0, 10) .. string.char(0x36, 0x19) .. string.char(0, 2) .. string.char(0, 0) .. string.char(0, 0xFF)
  .. string.rep("\0", 4) .. ("SONOR Node" .. string.rep("\0", 8)) .. ("SONOR Art-Net to DMX Node v0.3.0" .. string.rep("\0", 32))
ReceivedFromNetwork(6001, 6454, reply)
print("    status =", props["Node Status"])
check(props["Node Status"]:find("SONOR Node @ 2.0.0.10", 1, true) ~= nil, "node name + IP shown")
check(events[#events] == "Node Found", "Node Found fired")

print("Capabilities: dimmer slot has no colour, RGBW slot does")
local capD, capC
for _, n in ipairs(notes) do end
notes = {}
announceCapabilities(2); announceCapabilities(1)
check(notes[1].t.supports_color == "false" and notes[3].t.supports_color == "true", "supports_color per type")

print(fails == 0 and "\nALL PASSED" or ("\n" .. fails .. " FAILED"))
os.exit(fails == 0 and 0 or 1)
