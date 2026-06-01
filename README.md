# esphome-samsung-aqv

ESPHome external component for Samsung AQV air conditioners via IR.

Provides native climate control with IR receiver support, fan mode fallbacks, and full Pronto protocol encoding — no SmartIR or other HACS integrations needed.

## Why not ESPHome's built-in `heatpumpir` component?

ESPHome includes a [`heatpumpir`](https://esphome.io/components/climate/climate_ir.html#heatpumpir) climate component that wraps the [Arduino-HeatpumpIR](https://github.com/ToniA/arduino-heatpumpir) library. It supports `samsung_aqv` as a protocol option, but has significant limitations:

| | ESPHome `heatpumpir` (samsung_aqv) | This component |
|---|---|---|
| Max temperature | 27°C (library bug) | 30°C (correct per manual) |
| Fan speeds | 3 (low/med/high) + auto | 4 + auto: quiet, low, medium, high |
| Quiet/Silence mode | ❌ | ✅ |
| Fan fallbacks | Partial (auto→auto in dry) | Full (dry, fan_only, heat_cool combos) |
| IR receiver | ❌ Transmit only | ✅ Decodes remote, syncs HA state |
| Platform support | Arduino only (ESP8266/ESP32) | Arduino + LibreTiny (BK7231N, etc.) |
| Protocol source | Arduino-HeatpumpIR C++ library | Self-contained header (protocol.h) |
| Checksum | Simplified formula | Full reverse-engineered, validated against 457 codes |
| Tested against real remote | ❌ | ✅ 17 captured signals verified bit-for-bit |

If you're on ESP8266/ESP32 and don't need quiet mode, accurate temp range, or IR receiver, `heatpumpir` works fine. This component exists because we needed all of the above on a BK7231N (Tuya CBU) blaster.

## Supported models

Samsung AQV family with ARH-466 compatible remote:
- AQV18NSCN, AQV09NSAX, AQV12PSBN, and similar
- Any Samsung split AC using the 56-bit pulse-distance protocol at 38 kHz

## Features

- **Full climate modes:** cool, heat, dry, fan_only, heat_cool (auto)
- **Fan speeds:** auto, quiet (silence), low, medium, high
- **Swing:** vertical on (moving) / off (stopped)
- **Temperature:** 16–30°C in 1°C steps
- **IR receiver:** detects physical remote commands and updates HA state
- **Fan fallbacks:** invalid mode/fan combos silently corrected:
  - Dry → forces auto fan
  - Fan only + auto/quiet → falls back to low
  - Heat cool + quiet → falls back to auto

## Installation

Add to your ESPHome device YAML:

```yaml
external_components:
  - source: github://andredp/esphome-samsung-aqv
    components: [samsung_aqv]
    refresh: 1d

remote_transmitter:
  pin: GPIO4  # your IR LED pin
  carrier_duty_percent: 50%

remote_receiver:
  id: ir_receiver
  pin:
    number: GPIO5  # your IR receiver pin
    inverted: true
    mode: INPUT_PULLUP
  tolerance: 40%

climate:
  - platform: samsung_aqv
    name: "Living Room AC"
    receiver_id: ir_receiver
```

The `receiver_id` is optional — omit it if you don't have an IR receiver and only need transmission.

### Minimal example (transmit only)

```yaml
external_components:
  - source: github://andredp/esphome-samsung-aqv
    components: [samsung_aqv]
    refresh: 1d

remote_transmitter:
  pin: GPIO4
  carrier_duty_percent: 50%

climate:
  - platform: samsung_aqv
    name: "Bedroom AC"
```

## Tested hardware

| Device | Module | Platform | Notes |
|--------|--------|----------|-------|
| Tuya S11 IR blaster | CBU (BK7231N) | LibreTiny | TX: P7, RX: P8 |
| Athom IR blaster | ESP8266 | ESP8266 | TX: GPIO4, RX: GPIO5 |

Should work on any ESPHome-compatible board with an IR LED (and optionally IR receiver).

## How it works

1. **Transmission:** When you change the climate state in HA, `transmit_state()` encodes the settings into a Pronto hex string via `protocol.h`, then transmits using ESPHome's native `ProntoProtocol`.

2. **Reception:** When the physical remote is used, `on_receive()` decodes the raw IR signal, validates the checksum, extracts mode/fan/temp/swing, and updates the HA entity state.

3. **Fan fallbacks:** Before encoding, `resolve_fan()` corrects invalid mode/fan combinations based on the AC's actual capabilities (validated against the Samsung manual).

## Repository structure

```
components/samsung_aqv/
├── protocol.h       — Pure C++ protocol engine (no ESPHome deps, testable standalone)
├── samsung_aqv.h    — ClimateIR subclass declaration
├── samsung_aqv.cpp  — ESPHome glue: TX via ProntoProtocol, RX bit decoding
├── climate.py       — ESPHome component registration (CONFIG_SCHEMA + to_code)
└── __init__.py      — Empty (ESPHome package requirement)

tests/
├── test_protocol.cpp      — Native C++ tests (doctest, 10000+ assertions)
├── test_vectors.h         — 540 Pronto test vectors (compile-time)
├── test_helpers.h         — Timing-based decode helpers for tests
├── doctest.h              — Single-header test framework
├── test_esphome.yaml      — ESPHome compile test (ESP8266)
├── test_esphome_esp32.yaml    — ESPHome compile test (ESP32)
└── test_esphome_bk7231n.yaml  — ESPHome compile test (BK7231N)

tools/
├── encode_cli.cpp  — CLI encoder (for manual debugging)
└── decode_cli.cpp  — CLI decoder

docs/
├── protocol.md         — Full protocol reverse-engineering notes
└── captured_signals.md — 17 captured codes from ARH-466 remote
```

## Running tests

```bash
g++ -std=c++17 -O2 -o tests/test_protocol tests/test_protocol.cpp
tests/test_protocol
```

## Development

```bash
# Install pre-commit hook (clang-format + cppcheck)
ln -sf ../../.hooks/pre-commit .git/hooks/pre-commit
```

Requires `clang-format` and optionally `cppcheck` installed locally.

## Protocol summary

- 38 kHz carrier, pulse-distance encoding
- ON command: 2 bursts × 56 bits
- OFF command: 3 bursts × 56 bits (fixed payload)
- Checksum: `reverse_5bit(33 - count_ones(bits[17:55] + 1))` at bits 12–16
- Quiet fan flag: burst 1 bit 45 (not bit 41 as some sources incorrectly state)

Full protocol documentation in [`docs/protocol.md`](docs/protocol.md).

## License

MIT
