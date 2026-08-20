# Project Context

Last reviewed: 2026-08-17

- ESPHome external component for Samsung AQV split ACs using the ARH-466-compatible 56-bit IR protocol at 38 kHz.
- Primary tested models are AQV18NSCN and AQV09NSAX; tested platforms are BK7231N/LibreTiny, ESP8266, and ESP32.
- `components/samsung_aqv/protocol.h` is the header-only source of truth for encode/decode/checksum; ESPHome glue belongs in the adjacent component files.
- Transmission uses ESPHome `ProntoProtocol`; reception decodes the physical remote and updates climate state. Preserve the documented fan fallbacks for invalid mode/fan combinations.
- Native test commands are documented in `AGENTS.md`; also run the ESPHome compile matrix when changing registration or platform behavior.
- Device YAMLs live outside this repository and are flashed through the ESPHome dashboard.
