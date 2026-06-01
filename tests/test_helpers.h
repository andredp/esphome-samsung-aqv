// Test-only utilities for timing-based decode verification.
// NOT used in production — real IR reception uses ESPHome's RemoteReceiveData API.
#pragma once

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "../components/samsung_aqv/protocol.h"

namespace esphome {
namespace samsung_aqv {

// Convert Pronto hex to microsecond timings for testing decode_from_timings.
inline std::vector<int32_t> pronto_to_timings(const std::string &pronto) {
  std::vector<uint16_t> codes;
  std::istringstream ss(pronto);
  std::string token;
  while (ss >> token)
    codes.push_back((uint16_t) strtoul(token.c_str(), nullptr, 16));
  if (codes.size() < 4)
    return {};

  double period = codes[1] * 0.241246;
  std::vector<int32_t> timings;
  for (size_t i = 4; i < codes.size(); i++) {
    timings.push_back((int32_t) (codes[i] * period + 0.5));
  }
  return timings;
}

// Decode from Pronto-converted timings. Uses ±25% tolerance.
// NOT equivalent to on_receive() — real IR reception has different inter-burst structure.
inline DecodedState decode_from_timings(const int32_t *timings, size_t count) {
  constexpr int32_t T_HDR_SHORT = 2920;
  constexpr int32_t T_HDR_LONG = 4950;
  constexpr int32_t T_HDR_SPACE = 9000;
  constexpr int32_t T_BIT_MARK = 450;
  constexpr int32_t T_INTER_MARK = 3000;
  constexpr int32_t T_SPACE_THRESHOLD = 1100;

  DecodedState state{};
  size_t idx = 0;

  auto in_range = [](int32_t val, int32_t expected) -> bool {
    return val >= expected * 3 / 4 && val <= expected * 5 / 4;
  };

  if (idx >= count)
    return state;
  if (!in_range(timings[idx], T_HDR_SHORT) && !in_range(timings[idx], T_HDR_LONG))
    return state;
  idx++;

  if (idx >= count || !in_range(timings[idx], T_HDR_SPACE))
    return state;
  idx++;

  uint8_t b1[56] = {};
  for (int i = 0; i < 56; i++) {
    if (idx >= count || !in_range(timings[idx], T_BIT_MARK))
      return state;
    idx++;
    if (idx >= count)
      return state;
    int32_t space = timings[idx];
    if (space > T_SPACE_THRESHOLD) {
      b1[i] = 1;
      idx++;
    } else if (space > 200) {
      b1[i] = 0;
      idx++;
    } else
      return state;
  }

  if (b1[1] != 1 || b1[9] != 1)
    return state;

  // Inter-burst: trailing_mark + gap (~1900µs) + inter_mark (~3000µs) + hdr_space (~9000µs)
  if (idx >= count || !in_range(timings[idx], T_BIT_MARK))
    return state;
  idx++;
  if (idx >= count || timings[idx] < 500)  // gap is ~1900µs, just check it's a valid space
    return state;
  idx++;
  if (idx >= count || !in_range(timings[idx], T_INTER_MARK))
    return state;
  idx++;
  if (idx >= count || !in_range(timings[idx], T_HDR_SPACE))
    return state;
  idx++;

  uint8_t b2[56] = {};
  for (int i = 0; i < 56; i++) {
    if (idx >= count || !in_range(timings[idx], T_BIT_MARK))
      return state;
    idx++;
    if (idx >= count)
      return state;
    int32_t space = timings[idx];
    if (space > T_SPACE_THRESHOLD) {
      b2[i] = 1;
      idx++;
    } else if (space > 200) {
      b2[i] = 0;
      idx++;
    } else
      return state;
  }

  // OFF has 3 bursts; only first 2 needed for identification.
  return decode_from_bits(b1, b2);
}

// Decode from Pronto hex string (test/CLI only — device receives raw timings).
inline DecodedState decode_pronto(const std::string &pronto) {
  DecodedState state{};
  std::vector<uint16_t> codes;
  std::istringstream ss(pronto);
  std::string token;
  while (ss >> token)
    codes.push_back((uint16_t) strtoul(token.c_str(), nullptr, 16));
  if (codes.size() < 4)
    return state;

  size_t idx = 4;
  if (idx + 1 >= codes.size())
    return state;

  // OFF = 3 bursts (pair count > 150), ON = 2 bursts (~116 pairs)
  if (codes[2] > 0x0090) {
    state.valid = true;
    state.is_off = true;
    return state;
  }

  idx += 2;  // skip header mark+space

  uint8_t b1[56];
  for (int i = 0; i < 56; i++) {
    if (idx + 1 >= codes.size())
      return state;
    idx++;  // skip mark
    b1[i] = (codes[idx] > 0x002B) ? 1 : 0;
    idx++;
  }
  idx += 4;  // trailing mark + inter-burst gap + inter-burst mark + header space

  uint8_t b2[56];
  for (int i = 0; i < 56; i++) {
    if (idx + 1 >= codes.size())
      return state;
    idx++;
    b2[i] = (codes[idx] > 0x002B) ? 1 : 0;
    idx++;
  }

  return decode_from_bits(b1, b2);
}

}  // namespace samsung_aqv
}  // namespace esphome
