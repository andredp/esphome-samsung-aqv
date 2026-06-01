# TODO

## Backlog

- [ ] Test more modes with real remote — Heat, Dry, Fan_only, Heat_cool (only Cool verified so far)
- [ ] Verify fan_only long header with real remote capture
- [ ] Add OFF command detection (3-burst signal)
- [ ] Handle receiver self-echo (ignore signals from own transmitter)
- [ ] Add CI (GitHub Actions) — compile against ESPHome framework + run test suite
- [ ] Consider removing `<sstream>` from protocol.h (only used by `decode_pronto`, adds ~4KB flash)

## Done

- [x] Protocol engine (protocol.h) — encode/decode/checksum, 10524 assertions passing
- [x] ESPHome component registration (climate.py) — uses `climate_ir_with_receiver_schema`
- [x] Transmission working — native ProntoProtocol, confirmed on real AC
- [x] Fan fallback logic — dry→auto, fan_only+auto/quiet→low, heat_cool+quiet→auto
- [x] Validated against 17 captured remote signals
- [x] Cross-referenced with Arduino-HeatpumpIR byte layout
- [x] Mode/fan/temp matrix validated against Samsung manual (DB98-28490A)
- [x] Deployed to CBU blaster (LibreTiny), climate entity working in HA
- [x] Removed SmartIR integration from HA
- [x] Fix IR receiver (`on_receive`) — decoding working at 40% tolerance
- [x] Inter-burst gap fixed (P_INTER_GAP = 0x0048, ~1900µs)
- [x] Debug text_sensor for unmatched IR signals
- [x] Athom ESP8266 blaster — deployed and working (Tasmota→ESPHome migration)
- [x] Remove debug logging from `on_receive` (replaced with optional text_sensor)
