# esphome-samsung-aqv

ESPHome external component for Samsung AQV air conditioners via IR.
Replaces SmartIR with native protocol encoding, IR receiver support, and fan mode fallbacks.

## Supported Models

Samsung AQV family (ARH-466 remote): AQV18NSCN, AQV09NSAX, AQV12PSBN, and similar.

## Features

- Full climate control: cool, heat, dry, fan_only, heat_cool
- Fan speeds: auto, quiet, low, medium, high
- Swing: on (moving) / off (stopped)
- Temperature: 16–30°C
- IR receiver: detects physical remote commands and syncs HA state
- Fan fallbacks: invalid combos silently corrected (dry→auto, fan_only quiet→low, etc.)

## Usage

```yaml
external_components:
  - source: github://andredp/esphome-samsung-aqv
    components: [samsung_aqv]

climate:
  - platform: samsung_aqv
    name: "Living Room AC"
    receiver_id: ir_receiver  # optional
```

Requires `remote_transmitter` (and optionally `remote_receiver`) configured on the device.

## Repository Structure

```
components/samsung_aqv/
├── __init__.py        # Empty, required by ESPHome component loader
├── climate.py         # ESPHome component registration (schema, code generation)
├── samsung_aqv.h      # ClimateIR subclass declaration
├── samsung_aqv.cpp    # ESPHome integration: transmit_state(), on_receive(), fan fallbacks
└── protocol.h         # Pure C++ protocol logic (no ESPHome deps, testable standalone)

tests/
├── test_protocol.py   # Test harness: 924 tests (encode, decode roundtrip, fan fallbacks)
├── encode_cli.cpp     # CLI wrapper around protocol.h encode functions
├── decode_cli.cpp     # CLI wrapper around protocol.h decode functions
└── samsung_aqv18nscn.json  # Ground truth: all 457 Pronto codes from SmartIR

docs/
├── protocol.md        # Protocol reverse-engineering notes (bit layout, checksum, timing)
├── captured_signals.md # 17 Pronto codes captured from physical ARH-466 remote
└── generate_samsung_ir.py  # DEPRECATED Python generator (kept for reference)
```

## File Purposes

### `protocol.h` — Protocol Engine

Standalone C++ header with zero ESPHome dependencies. Contains:
- `encode_on(temp, mode, fan, swing)` → Pronto hex string
- `encode_off()` → Pronto hex string
- `decode_pronto(string)` → `DecodedState{mode, fan, temp, swing}`
- `resolve_fan(mode, fan)` → corrected fan for invalid combos
- Burst building, checksum calculation, bit layout constants

This is the single source of truth for the protocol. Tests compile against it directly.

### `samsung_aqv.h` / `samsung_aqv.cpp` — ESPHome Glue

Subclass of `ClimateIR` that bridges ESPHome's climate API to `protocol.h`:
- `transmit_state()` — called when HA changes climate state; encodes and sends IR
- `on_receive()` — called when IR receiver detects a signal; decodes and updates HA state
- `resolve_fan_()` — maps ESPHome fan modes through the fallback logic

### `climate.py` — Component Registration

Tells ESPHome how to configure the component in YAML. Registers the platform,
defines the schema (inherits from `climate_ir`), and generates C++ code.

## Running Tests

```bash
cd tests && python3 test_protocol.py
```

Compiles `encode_cli.cpp` and `decode_cli.cpp`, then runs 924 assertions:
- 457 encode tests against the JSON lookup table
- 457 decode roundtrip tests (encode → decode → verify params)
- 10 fan fallback tests

## Protocol Summary

- 38 kHz carrier, pulse-distance encoding, LSB-first
- ON: 2 bursts × 56 bits (short header 0x006F, fan_only uses long 0x00BC)
- OFF: 3 bursts × 56 bits (long header 0x00BC, fixed payload)
- Checksum: `reverse_5bit(33 - count_ones(bits[17:55] + 1))` at bits 12–16
- Quiet fan: burst1 bit 45 (not bit 41 as some sources claim)

See `docs/protocol.md` for full bit layout documentation.
