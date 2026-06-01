# AGENTS.md

## Project overview

ESPHome external component for Samsung AQV air conditioners via IR (ARH-466 remote, 38 kHz Pronto).
Replaces SmartIR with a native ClimateIR implementation. Handles protocol encoding/decoding,
IR transmission/reception, and fan mode fallbacks.

- **AC models:** AQV18NSCN, AQV09NSAX (same protocol, entire Samsung AQV family)
- **Platforms:** BK7231N (LibreTiny), ESP8266, ESP32
- **Repo:** github.com/andredp/esphome-samsung-aqv

## Architecture

```
components/samsung_aqv/
├── protocol.h       — Pure C++ protocol engine (no ESPHome deps)
├── samsung_aqv.h    — ClimateIR subclass declaration
├── samsung_aqv.cpp  — ESPHome glue: enum mapping, TX via ProntoProtocol, RX via decode_from_bits
├── climate.py       — ESPHome component registration
└── __init__.py      — Empty (ESPHome requirement)

tests/
├── test_protocol.cpp — Native C++ tests (doctest, 10000+ assertions)
├── test_vectors.h    — 540 Pronto test vectors (compile-time)
├── test_helpers.h    — Timing-based decode helpers for tests
├── doctest.h         — Single-header test framework
├── test_esphome.yaml      — ESPHome compile test (ESP8266)
├── test_esphome_esp32.yaml — ESPHome compile test (ESP32)
└── test_esphome_bk7231n.yaml — ESPHome compile test (BK7231N)

tools/
├── encode_cli.cpp — CLI wrapper for encode (compiles against protocol.h)
└── decode_cli.cpp — CLI wrapper for decode

docs/
├── protocol.md      — Protocol reverse-engineering notes
└── captured_signals.md — 17 captured Pronto codes from real remote
```

## Key design rules

- **No duplicate protocol logic.** All encoding/decoding lives in `protocol.h`. The `.cpp` is a thin adapter.
- **protocol.h is header-only** so test CLIs compile without ESPHome (`g++ -std=c++17`).
- **Namespace:** `esphome::samsung_aqv` for both protocol.h and the component.
- **Transmission:** Uses ESPHome's native `remote_base::ProntoProtocol` via `transmit_<>()`. Never roll custom Pronto-to-timing conversion.
- **Fan fallbacks:** Resolved in `resolve_fan()` before encoding. Invalid combos never reach the encoder.

## Build and test

```bash
# Compile and run native C++ tests (10000+ assertions)
g++ -std=c++17 -O2 -o tests/test_protocol tests/test_protocol.cpp
tests/test_protocol

# Compile test CLIs (for manual debugging)
g++ -std=c++17 -O2 -o tools/encode_cli tools/encode_cli.cpp
g++ -std=c++17 -O2 -o tools/decode_cli tools/decode_cli.cpp
```

Tests validate encode/decode roundtrip against 540 Pronto vectors, timing-based decode, fan fallback logic, and edge cases.

## ESPHome compilation

This component is consumed as an external_components source. To test compilation:

```yaml
external_components:
  - source: github://andredp/esphome-samsung-aqv
    components: [samsung_aqv]
```

Target ESPHome version: 2025.5+. Uses `climate_ir_with_receiver_schema()` and `new_climate_ir()` APIs.
If the minimum version is raised, update the CI matrix in `.github/workflows/ci.yml`.

## Protocol quick reference

- 56 data bits per burst. ON = 2 bursts, OFF = 3 bursts.
- Checksum: `reverse_5bit(33 - count_ones(bits[17:55] + 1 implicit))` stored MSB-first at bits 12–16.
- Temperature: bits 36-39 LSB-first, value = temp - 16 (range 16–30°C).
- Mode: bits 44-46 MSB-first (cool=100, heat=001, dry=010, fan_only=110, heat_cool=011).
- Fan: bits 41-43 MSB-first (auto=000, low=010, medium=001, high=101). Quiet = burst1 bit 45.
- Swing: burst2 bits 20,22 (0=moving, 1=stopped), bit 21 always 1.
- All commands use short header mark (0x006F). Long header (0x00BC) not used by ARH-466.

## Fan mode constraints (from manual)

| Mode      | Allowed fans                        | Temp    |
|-----------|-------------------------------------|---------|
| Cool      | auto, quiet, low, medium, high      | 16–30°C |
| Heat      | auto, quiet, low, medium, high      | 16–30°C |
| Dry       | auto only (forced)                  | 16–30°C |
| Fan only  | low, medium, high (no auto/quiet)   | N/A     |
| Heat cool | auto, low, medium, high (no quiet)  | 16–30°C |

## Code style

- C++17 (ESPHome/PlatformIO default). No exceptions or RTTI (embedded constraint).
- ESPHome logging: `ESP_LOGD(TAG, ...)` for debug, `ESP_LOGV` for verbose.
- Keep protocol.h free of ESPHome includes — stdlib only.
- Python: minimal, follows ESPHome component conventions.

## Receiver notes

- ESPHome's `remote_receiver` buffer starts with the header mark (positive value), followed by header space (negative).
- `on_receive()` matches the header mark (~2920) then header space (~9000).
- Inter-burst in buffer: trailing_mark(~450) + short_space(~1900) + burst2_hdr_mark(~2920) + burst2_hdr_space(~9000).
- Bit spaces: ~1630 µs = logic 1, ~630 µs = logic 0. Mark: ~450 µs.
- `receiver_id` must be specified in device YAML for `on_receive()` to be called.
- `dump: raw` in YAML enables raw signal logging for debugging.

## Deployment

Device YAMLs are separate from this repo. The component is imported via `external_components` from GitHub. OTA flash via ESPHome dashboard.
