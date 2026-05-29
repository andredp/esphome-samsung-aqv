# Samsung AQV IR Protocol (Remote ARH-466)

## Overview

- **AC Models:** AQV18NSCN (Living Room), AQV09NSAX (Main Bedroom) — same protocol
- **Remote:** ARH-466 (compatible with entire Samsung AQV family)
- **Carrier frequency:** 38 kHz (Pronto freq code: 0x006D)
- **Encoding:** Pulse-distance (Samsung AC variant)
- **Bits are LSB-first** within each byte (bit 0 transmitted first)

### Timing (Pronto units → microseconds)

| Element | Pronto | ~µs | Notes |
|---------|--------|-----|-------|
| Bit mark | 0x0011 | 447 | All data bits |
| Space 0 | 0x0018 | 631 | Short space = logic 0 |
| Space 1 | 0x003E | 1630 | Long space = logic 1 |
| Header mark (ON) | 0x006F | 2920 | First burst of ON commands |
| Header mark (OFF/fan_only) | 0x00BC | 4944 | First burst of OFF and fan_only commands |
| Header space | 0x015F | 9230 | After header mark |
| Inter-burst mark | 0x0071 | 2970 | Between bursts (2nd, 3rd) |
| Inter-burst space | 0x015E | 9204 | Between bursts |
| Tail space | 0x0181 | 10124 | Final space after last burst |

**IMPORTANT:** This AC is very timing-sensitive. Idealized/rounded raw values (e.g., 490/590/1550/3000) do NOT work. Codes must be sent via ESPHome's `transmit_pronto` using Pronto hex format, which preserves correct timing through the Pronto timebase conversion.

## Message Structure

### OFF command: 3 bursts (21 bytes / 174 Pronto pairs)
- Burst 1: 7 bytes (header/device ID) — uses long header mark (0x00BC)
- Burst 2: 7 bytes (power off state)
- Burst 3: 7 bytes (checksum/confirmation)
- Each burst ends with a trailing mark + inter-burst space (or tail space for last burst)

### ON commands: 2 bursts (14 bytes / 118 Pronto pairs)
- Burst 1: 7 bytes (header + fan speed) — uses short header mark (0x006F)
- Burst 2: 7 bytes (mode, temperature, swing, checksum)
- Exception: fan_only mode uses long header mark (0x00BC)

## Burst 1 (Header) - 56 bits

Constant for all ON commands with same fan speed:

| Fan Speed | Burst 1 bits (raw) |
|-----------|-------------------|
| Auto fan  | `01000000 01001001 11110000 00000000 00000000 00000000 00001111` |
| Quiet fan | `01000000 01000001 11110000 00000000 00000000 00000100 00001111` |

MSB hex (Auto):  `40 49 F0 00 00 00 0F`
MSB hex (Quiet): `40 41 F0 00 00 04 0F`

**Fan speed is encoded in Burst 1, not Burst 2.**

## Burst 2 (Settings) - 56 bits

Format (bit positions in raw stream):
```
Pos 0-11:  100000000100  (constant prefix)
Pos 12-16: CCCCC         (5-bit checksum, LSB-first)
Pos 17-19: 111           (constant)
Pos 20:    S             (swing: 0=moving, 1=stopped)
Pos 21:    1             (constant)
Pos 22:    S             (swing: same as bit 20)
Pos 23-35: 1100000000000 (constant)
Pos 36-39: TTTT          (temperature, LSB-first, value = temp-16)
Pos 40:    1             (constant)
Pos 41-43: FFF           (fan speed bits)
Pos 44-46: MMM           (mode bits)
Pos 47-54: 00000011      (constant suffix)
Pos 55:    1             (end bit)
```

### Temperature Encoding (bits 36-39, LSB-first)

Value = temp - 16, bits reversed (LSB transmitted first):

| Temp | temp-16 | Binary | LSB-first |
|------|---------|--------|-----------|
| 16°C | 0       | 0000   | 0000 |
| 17°C | 1       | 0001   | 1000 |
| 18°C | 2       | 0010   | 0100 |
| 19°C | 3       | 0011   | 1100 |
| 20°C | 4       | 0100   | 0010 |
| 21°C | 5       | 0101   | 1010 |
| 22°C | 6       | 0110   | 0110 |
| 23°C | 7       | 0111   | 1110 |
| 24°C | 8       | 1000   | 0001 |
| 25°C | 9       | 1001   | 1001 |
| 26°C | 10      | 1010   | 0101 |
| 27°C | 11      | 1011   | 1101 |
| 28°C | 12      | 1100   | 0011 |
| 29°C | 13      | 1101   | 1011 |
| 30°C | 14      | 1110   | 0111 |

### Mode Encoding (bits 44-46)

| Mode     | Bits 44-46 |
|----------|-----------|
| Cool     | 1 0 0     |
| Heat     | 0 1 0     |
| Dry      | 0 0 1     |
| Fan only | 1 1 0     |
| Auto     | 0 1 1     |

### Fan Speed

Fan speed is split across Burst 1 and Burst 2.

**Burst 1:** Only bit 45 changes — set to 1 for quiet fan, 0 for all others.

**Burst 2 (bits 41-43):**

| Fan Speed | Bits 41,42,43 |
|-----------|--------------|
| Auto      | 0, 0, 0 |
| Quiet     | 0, 0, 0 (burst 1 bit 45 differentiates from auto) |
| Low       | 0, 1, 0 |
| Medium    | 0, 0, 1 |
| High      | 1, 0, 1 |

Notes:
- Quiet (Silence mode) is only available in Cool and Heat modes
- Fan-only mode supports 3 speeds only: low, medium, high (no auto, no quiet)
- Auto (heat_cool) mode supports 4 speeds: auto, low, medium, high (no quiet)
- Dry mode forces auto fan (no manual fan selection)
- SmartIR fan mode names: `auto`, `quiet`, `low`, `medium`, `high`

### Swing/Flap (bits 20, 22 in Burst 2)

| Swing | Bit 20 | Bit 22 |
|-------|--------|--------|
| Moving (on) | 0 | 0 |
| Stopped (off) | 1 | 1 |

### Checksum

Both bursts use the same checksum formula:
- Checksum = 5 bits at positions 12-16, LSB-first
- Formula: `reverse_5bit(33 - count_ones(bits_after_checksum))`
- Verified: `checksum_reversed_value + ones_after_checksum = 33` for every code

## Captured Codes (Verified Working)

### OFF
```
Burst 1: 40 4D F0 00 00 00 03
Burst 2: 80 41 F0 00 0C 0B 00
Burst 3: 80 44 FF 80 0A 82 03
```

### Cool, Auto Fan, Flap Moving
| Temp | Burst 1 (hex) | Burst 2 (hex) |
|------|---------------|---------------|
| 20°C | 40 49 F0 00 00 00 0F | 80 4C F5 80 02 88 0F |
| 21°C | 40 49 F0 00 00 00 0F | 80 44 F5 80 0A 88 0F |
| 22°C | 40 49 F0 00 00 00 0F | 80 44 F5 80 06 88 0F |
| 23°C | 40 49 F0 00 00 00 0F | 80 48 F5 80 0E 88 0F |
| 24°C | 40 49 F0 00 00 00 0F | 80 4C F5 80 01 88 0F |
| 25°C | 40 49 F0 00 00 00 0F | 80 44 F5 80 09 88 0F |

### Heat, 24°C, Auto Fan, Flap Moving
```
Burst 1: 40 49 F0 00 00 00 0F
Burst 2: 80 4C F5 80 01 82 0F
```

### Dry, 24°C, Auto Fan, Flap Moving
```
Burst 1: 40 49 F0 00 00 00 0F
Burst 2: 80 4C F5 80 01 84 0F
```

### Cool, 24°C, Quiet Fan, Flap Moving
```
Burst 1: 40 41 F0 00 00 04 0F
Burst 2: 80 4C F5 80 01 88 0F
```

### Fan Only, Max Fan, Flap Moving
```
Burst 1: 40 49 F0 00 00 00 0F
Burst 2: 80 40 F5 80 01 DC 0F
```

## IR Blaster Hardware

- **Device:** Tuya S11 Universal IR+RF WiFi Remote Control
- **Module:** CBU (BK7231N) — soldered onto main PCB
- **Firmware:** ESPHome via LibreTiny (flashed with tuya-cloudcutter)
- **ESPHome board:** `cbu`

### GPIO Pinout (confirmed via testing)

| Pin | Function |
|-----|----------|
| P7 | IR Transmitter (confirmed working via webcam) |
| P8 | IR Receiver (confirmed working — captures remote codes) |
| P9 | Status LED |
| P23 | Button |

**Note:** The S11 ESPHome device page documents the same pinout. The IRC03 page has different pins (P9=Button, P24=LED) — this device is NOT an IRC03 despite similar appearance. Confirmed by CBU module label on PCB.

### ESPHome Services

| Service | Variable | Type | Purpose |
|---------|----------|------|---------|
| `send_raw_command` | `command` | `int[]` | Send raw IR timing array |
| `send_ir_command` | `command` | `string` | Send Pronto hex string via `transmit_pronto` |

## SmartIR Integration Status

### Working (2026-04-13)
- **Device code:** 9999
- **Controller:** ESPHome
- **Encoding label:** `Raw` (required — ESPHome controller rejects `Pronto`)
- **Actual format:** Pronto hex strings stored as JSON-escaped strings
- **controller_data:** `ir_blaster_send_ir_command`
- **ESPHome service:** `send_ir_command` accepts `command: string` → `transmit_pronto`

### Key Implementation Details
- SmartIR's ESPHome controller does `json.loads(command)` on the stored value
- Codes stored as `json.dumps(pronto_string)` → double-quoted in JSON
- `json.loads` returns a plain string → passed as `{'command': "0000 006D..."}` to ESPHome
- JSON nesting order: `commands[mode][fan][swing][temp]` (SmartIR's expected lookup path)
- `commandsEncoding` must be `"Raw"` even though actual data is Pronto (ESPHome controller validation)

### Files
- `generate_samsung_ir.py` — Code generator (Pronto hex output with correct timing constants)
- `samsung_aqv18nscn.json` — Generated SmartIR JSON (deploy as 9999.json)
- `ir-blaster.yaml` — ESPHome config with `send_raw_command` and `send_ir_command` services

## Operating Reference (from manual DB98-28490A)

### Mode / Fan / Temperature Matrix

| Mode | Fan Speeds | Temp Range | Notes |
|------|-----------|------------|-------|
| Auto (heat_cool) | Auto, Low, Medium, High | 16–30°C | AC decides cool/heat based on room temp |
| Cool | Auto, Low, Medium, High, Quiet | 16–30°C | Quiet = Silence mode (reduced noise, reduced performance) |
| Heat | Auto, Low, Medium, High, Quiet | 16–30°C | Fan may not run for 3–5 min at startup (prevents cold air) |
| Dry | Auto only | 16–30°C | Fan speed adjusts automatically |
| Fan only | Low, Medium, High | N/A (auto) | Temperature set automatically by AC |

### Special Functions (not in SmartIR — require separate IR commands)

| Function | Available Modes | Duration | Notes |
|----------|----------------|----------|-------|
| Turbo | Auto, Cool, Heat | 30 min | Max fan/temp, then reverts. In Dry/Fan → switches to Auto |
| Good Sleep | Cool, Heat | 30min–12hrs | Timed program with 3 stages (fall asleep → sound sleep → wake up). Recommended 25–27°C cool, 21–23°C heat |

### Operating Limits

| Operation | Outdoor Temp (AQV18/24) | Indoor Temp | Indoor Humidity |
|-----------|------------------------|-------------|-----------------|
| Heating | -15°C to 24°C | 27°C or less | — |
| Cooling | -10°C to 43°C | 16°C to 32°C | 80% or less |
| Dehumidifying | -10°C to 43°C | 16°C to 32°C | — |

### Operational Notes
- Heating capacity drops 70–80% at outdoor temps near 0°C
- Cooling capacity drops if indoor temp exceeds 32°C
- Defrost cycle triggers automatically at low outdoor temp + high humidity (3–14 min)
- At very low outdoor temps, indoor fan may not reach Medium/Low in cool mode (frost protection)
- Silence mode (quiet) reduces fan speed → reduced performance in both cool and heat
- Without remote: ON/OFF button on unit auto-selects Cool (≥24°C room) or Heat (<24°C room)

## References

- [r1tch/irtrans-samsung-ac](https://github.com/r1tch/irtrans-samsung-ac) - Samsung AC protocol generator
- [IRremoteESP8266 #505](https://github.com/crankyoldgit/IRremoteESP8266/issues/505) - Samsung AC protocol decoding
- [IRremoteESP8266 #1538](https://github.com/crankyoldgit/IRremoteESP8266/issues/1538) - Detailed protocol analysis with spreadsheet
- [ESPHome S11 device page](https://devices.esphome.io/devices/tuya-generic-s11-ir-rf-remote-control/) - Pinout and config
- [ESPHome IRC03 device page](https://devices.esphome.io/devices/Tuya-Generic-IRC03-IR-Blaster) - Similar device, different pinout
- Samsung AQV User Manual DB98-28490A (FORTE_Inv_IB) — `20071208152358750_FORTEInvIB_28490A_E.pdf`
