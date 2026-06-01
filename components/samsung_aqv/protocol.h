#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>

// Samsung AQV IR Protocol — pure encoding logic, no ESPHome dependencies.
// Models: AQV18NSCN, AQV09NSAX, Samsung AQV family (ARH-466 remote)
// 56 data bits per burst, Pronto hex encoding at 38kHz.

namespace esphome {
namespace samsung_aqv {

// Pronto timing constants (captured from ARH-466 remote)
constexpr uint16_t P_FREQ = 0x006D;
constexpr uint16_t P_HDR_SHORT = 0x006F;  // ON commands
constexpr uint16_t P_HDR_LONG = 0x00BC;   // OFF, fan_only
constexpr uint16_t P_HDR_SPACE = 0x015F;
constexpr uint16_t P_INTER_MARK = 0x0071;
constexpr uint16_t P_INTER_SPACE = 0x015E;
constexpr uint16_t P_MARK = 0x0011;
constexpr uint16_t P_SPACE_0 = 0x0018;
constexpr uint16_t P_SPACE_1 = 0x003E;
constexpr uint16_t P_TAIL = 0x0181;

enum Mode { MODE_COOL, MODE_HEAT, MODE_DRY, MODE_FAN_ONLY, MODE_HEAT_COOL };
enum Fan { FAN_AUTO, FAN_QUIET, FAN_LOW, FAN_MEDIUM, FAN_HIGH };
enum Swing { SWING_ON, SWING_OFF };  // ON = moving, OFF = stopped

inline Fan resolve_fan(Mode mode, Fan fan) {
  switch (mode) {
    case MODE_DRY:
      return FAN_AUTO;
    case MODE_FAN_ONLY:
      return (fan == FAN_AUTO || fan == FAN_QUIET) ? FAN_LOW : fan;
    case MODE_HEAT_COOL:
      return (fan == FAN_QUIET) ? FAN_AUTO : fan;
    default:
      return fan;
  }
}

inline uint8_t fan_bits(Fan fan) {
  // Returns 3-bit value in transmission order (bit41=MSB, bit43=LSB)
  switch (fan) {
    case FAN_LOW:
      return 0b010;
    case FAN_MEDIUM:
      return 0b001;
    case FAN_HIGH:
      return 0b101;
    default:
      return 0b000;
  }
}

inline uint8_t mode_bits(Mode mode) {
  // 3-bit value in transmission order (bit44=MSB, bit46=LSB)
  switch (mode) {
    case MODE_COOL:
      return 0b100;
    case MODE_HEAT:
      return 0b001;
    case MODE_DRY:
      return 0b010;
    case MODE_FAN_ONLY:
      return 0b110;
    case MODE_HEAT_COOL:
      return 0b011;
    default:
      return 0b100;
  }
}

inline uint8_t reverse_bits(uint8_t val, int n) {
  uint8_t r = 0;
  for (int i = 0; i < n; i++)
    r |= ((val >> i) & 1) << (n - 1 - i);
  return r;
}

inline uint8_t checksum(const uint8_t *bits, int count, int extra_ones = 0) {
  int ones = extra_ones;
  for (int i = 0; i < count; i++)
    ones += bits[i];
  return reverse_bits(33 - ones, 5);
}

// Burst 1: 56 bits. Fan speed encoded here (quiet flag at bit 41).
inline void build_burst1(uint8_t bits[56], Fan fan) {
  //                     byte0        byte1        byte2        byte3
  //                     01000000     01001001     11110000     00000000
  //                     byte4        byte5        byte6
  //                     00000000     00000000     00001111
  const uint8_t base[56] = {0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0,
                            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1};
  memcpy(bits, base, 56);
  if (fan == FAN_QUIET)
    bits[45] = 1;
  // Checksum: bits 12-16, over bits 17-55 + implicit trailing 1
  uint8_t chk = checksum(bits + 17, 39, 1);
  for (int i = 0; i < 5; i++)
    bits[12 + i] = (chk >> (4 - i)) & 1;
}

// Burst 2: 56 bits. Mode, temp, fan speed bits, swing, checksum.
inline void build_burst2(uint8_t bits[56], int temp, Mode mode, Fan fan, Swing swing) {
  memset(bits, 0, 56);
  // Prefix: bits 0-11 = 100000000100
  bits[0] = 1;
  bits[9] = 1;
  // Bits 17-19: 111
  bits[17] = 1;
  bits[18] = 1;
  bits[19] = 1;
  // Swing: bits 20, 22 (0=moving/on, 1=stopped/off)
  uint8_t sw = (swing == SWING_OFF) ? 1 : 0;
  bits[20] = sw;
  bits[21] = 1;
  bits[22] = sw;
  // Bits 23-24: 11
  bits[23] = 1;
  bits[24] = 1;
  // Temperature: bits 36-39 (LSB-first, val = temp-16)
  uint8_t tv = (uint8_t) (temp - 16);
  for (int i = 0; i < 4; i++)
    bits[36 + i] = (tv >> i) & 1;
  // Bit 40: 1
  bits[40] = 1;
  // Fan: bits 41-43 (MSB-first: bit41 is high bit of fan_bits)
  uint8_t fb = fan_bits(fan);
  bits[41] = (fb >> 2) & 1;
  bits[42] = (fb >> 1) & 1;
  bits[43] = fb & 1;
  // Mode: bits 44-46 (MSB-first: bit44 is high bit of mode_bits)
  uint8_t mb = mode_bits(mode);
  bits[44] = (mb >> 2) & 1;
  bits[45] = (mb >> 1) & 1;
  bits[46] = mb & 1;
  // Suffix: bits 47-51 = 00000, bits 52-55 = 1111
  bits[52] = 1;
  bits[53] = 1;
  bits[54] = 1;
  bits[55] = 1;
  // Checksum: bits 12-16, over bits 17-55 + implicit trailing 1
  uint8_t chk = checksum(bits + 17, 39, 1);
  for (int i = 0; i < 5; i++)
    bits[12 + i] = (chk >> (4 - i)) & 1;
}

// --- Pronto encoding ---

inline void append_bits_pronto(std::vector<uint16_t> &pairs, const uint8_t *bits, int count) {
  for (int i = 0; i < count; i++) {
    pairs.push_back(P_MARK);
    pairs.push_back(bits[i] ? P_SPACE_1 : P_SPACE_0);
  }
}

inline std::string pronto_string(uint16_t freq, const std::vector<uint16_t> &pairs) {
  std::string result;
  char buf[6];
  int n = pairs.size() / 2;
  snprintf(buf, sizeof(buf), "%04X", 0);
  result += buf;
  result += ' ';
  snprintf(buf, sizeof(buf), "%04X", freq);
  result += buf;
  result += ' ';
  snprintf(buf, sizeof(buf), "%04X", n);
  result += buf;
  result += ' ';
  snprintf(buf, sizeof(buf), "%04X", 0);
  result += buf;
  for (auto v : pairs) {
    result += ' ';
    snprintf(buf, sizeof(buf), "%04X", v);
    result += buf;
  }
  return result;
}

inline std::string encode_off() {
  const uint8_t b1[56] = {0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0,
                          0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1};
  const uint8_t b2[56] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0,
                          0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0};
  const uint8_t b3[56] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0,
                          0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1};

  std::vector<uint16_t> pairs;
  pairs.push_back(P_HDR_LONG);
  pairs.push_back(P_HDR_SPACE);
  append_bits_pronto(pairs, b1, 56);
  pairs.push_back(P_MARK);
  pairs.push_back(P_INTER_SPACE);
  pairs.push_back(P_INTER_MARK);
  pairs.push_back(P_INTER_SPACE);
  append_bits_pronto(pairs, b2, 56);
  pairs.push_back(P_MARK);
  pairs.push_back(P_INTER_SPACE);
  pairs.push_back(P_INTER_MARK);
  pairs.push_back(P_INTER_SPACE);
  append_bits_pronto(pairs, b3, 56);
  pairs.push_back(P_MARK);
  pairs.push_back(P_TAIL);
  return pronto_string(P_FREQ, pairs);
}

inline std::string encode_on(int temp, Mode mode, Fan fan, Swing swing) {
  Fan actual_fan = resolve_fan(mode, fan);
  uint8_t b1[56], b2[56];
  build_burst1(b1, actual_fan);
  build_burst2(b2, temp, mode, actual_fan, swing);

  uint16_t hdr = (mode == MODE_FAN_ONLY) ? P_HDR_LONG : P_HDR_SHORT;
  std::vector<uint16_t> pairs;
  pairs.push_back(hdr);
  pairs.push_back(P_HDR_SPACE);
  append_bits_pronto(pairs, b1, 56);
  pairs.push_back(P_MARK);
  pairs.push_back(P_INTER_SPACE);
  pairs.push_back(P_INTER_MARK);
  pairs.push_back(P_INTER_SPACE);
  append_bits_pronto(pairs, b2, 56);
  pairs.push_back(P_MARK);
  pairs.push_back(P_TAIL);
  return pronto_string(P_FREQ, pairs);
}

// --- Decoding ---

struct DecodedState {
  bool valid;
  bool is_off;
  Mode mode;
  Fan fan;
  int temp;
  Swing swing;
};

// Core decode: given burst1 and burst2 bit arrays + header type, extract state.
// Used by both decode_pronto() and on_receive() — single source of truth.
inline DecodedState decode_from_bits(const uint8_t b1[56], const uint8_t b2[56], bool long_header) {
  DecodedState state{};

  // Validate checksum on burst 1 (same algorithm as burst 2)
  {
    int ones = 0;
    for (int i = 17; i < 56; i++)
      ones += b1[i];
    ones += 1;
    uint8_t expected_ck = reverse_bits(33 - (ones % 32), 5);
    uint8_t actual_ck = 0;
    for (int i = 0; i < 5; i++)
      actual_ck |= (b1[12 + i] << (4 - i));
    if (actual_ck != expected_ck)
      return state;
  }

  // Validate checksum on burst 2
  int ones = 0;
  for (int i = 17; i < 56; i++)
    ones += b2[i];
  ones += 1;  // implicit trailing bit
  uint8_t expected_ck = reverse_bits(33 - (ones % 32), 5);
  uint8_t actual_ck = 0;
  for (int i = 0; i < 5; i++)
    actual_ck |= (b2[12 + i] << (4 - i));
  if (actual_ck != expected_ck)
    return state;

  state.valid = true;

  // Mode: bits 44-46 MSB-first
  int mode_raw = (b2[44] << 2) | (b2[45] << 1) | b2[46];

  if (long_header && mode_raw != 0b110) {
    state.is_off = true;
    return state;
  }

  state.is_off = false;
  switch (mode_raw) {
    case 0b100:
      state.mode = MODE_COOL;
      break;
    case 0b001:
      state.mode = MODE_HEAT;
      break;
    case 0b010:
      state.mode = MODE_DRY;
      break;
    case 0b110:
      state.mode = MODE_FAN_ONLY;
      break;
    case 0b011:
      state.mode = MODE_HEAT_COOL;
      break;
    default:
      state.valid = false;
      return state;
  }

  // Temperature: bits 36-39 LSB-first
  state.temp = (b2[36] | (b2[37] << 1) | (b2[38] << 2) | (b2[39] << 3)) + 16;

  // Fan: burst 1 bit 45 = quiet flag, burst 2 bits 41-43 MSB-first
  bool quiet = (b1[45] == 1);
  int fan_raw = (b2[41] << 2) | (b2[42] << 1) | b2[43];
  if (quiet) {
    state.fan = FAN_QUIET;
  } else {
    switch (fan_raw) {
      case 0b000:
        state.fan = FAN_AUTO;
        break;
      case 0b010:
        state.fan = FAN_LOW;
        break;
      case 0b001:
        state.fan = FAN_MEDIUM;
        break;
      case 0b101:
        state.fan = FAN_HIGH;
        break;
      default:
        state.fan = FAN_AUTO;
        break;
    }
  }

  // Swing: bit 20 (0=moving/on, 1=stopped/off)
  state.swing = (b2[20] == 1) ? SWING_OFF : SWING_ON;
  return state;
}

// Decode from Pronto hex string
inline DecodedState decode_pronto(const std::string &pronto) {
  DecodedState state{};
  std::vector<uint16_t> codes;
  std::istringstream ss(pronto);
  std::string token;
  while (ss >> token) {
    codes.push_back((uint16_t) strtoul(token.c_str(), nullptr, 16));
  }
  if (codes.size() < 4)
    return state;

  size_t idx = 4;  // skip Pronto header (4 words)
  if (idx + 1 >= codes.size())
    return state;

  uint16_t hdr_mark = codes[idx];
  bool long_header = (hdr_mark == P_HDR_LONG);
  idx += 2;  // skip header mark+space

  // Decode burst 1 (56 bit pairs)
  uint8_t b1[56];
  for (int i = 0; i < 56; i++) {
    if (idx + 1 >= codes.size())
      return state;
    idx++;  // skip mark
    b1[i] = (codes[idx] == P_SPACE_1) ? 1 : 0;
    idx++;
  }
  // Skip trailing mark + inter-burst space + inter-burst mark + inter-burst space
  idx += 4;

  // Decode burst 2 (56 bit pairs)
  uint8_t b2[56];
  for (int i = 0; i < 56; i++) {
    if (idx + 1 >= codes.size())
      return state;
    idx++;
    b2[i] = (codes[idx] == P_SPACE_1) ? 1 : 0;
    idx++;
  }

  return decode_from_bits(b1, b2, long_header);
}

// Convert Pronto hex to microsecond timings (for testing decode_from_timings).
// Returns flat timing values starting with header mark, then header space, then data mark/space pairs.
inline std::vector<int32_t> pronto_to_timings(const std::string &pronto) {
  std::vector<uint16_t> codes;
  std::istringstream ss(pronto);
  std::string token;
  while (ss >> token)
    codes.push_back((uint16_t) strtoul(token.c_str(), nullptr, 16));
  if (codes.size() < 4)
    return {};

  // Pronto tick period in µs: freq_code * 0.241246
  double period = codes[1] * 0.241246;
  std::vector<int32_t> timings;
  // First pair is header mark+space; receiver sees space first (mark triggers capture)
  // So we output: +space, then alternating -mark/+space for data
  // Actually ESPHome buffer: space(hdr), mark, space, mark, space...
  // Buffer starts at index 4 (skip pronto header words)
  for (size_t i = 4; i < codes.size(); i++) {
    int32_t us = (int32_t) (codes[i] * period + 0.5);
    // Even indices (relative to pairs) are marks (negative), odd are spaces (positive)
    // In Pronto: pairs are mark,space,mark,space...
    // First element (i=4) is header mark
    timings.push_back(us);
  }
  return timings;
}

// Decode from Pronto-converted timings (for testing the timing-based decode path).
// NOT equivalent to on_receive() — real IR reception has different inter-burst structure
// due to receiver hardware absorbing the trailing mark + msg_space into the last bit's space.
// Timings: header_mark, header_space, [mark, space]*56, trailing_mark, inter_space, inter_mark, inter_space, ...
// Uses ±25% tolerance.
inline DecodedState decode_from_timings(const int32_t *timings, size_t count) {
  // Timing constants in µs (derived from Pronto values at 38kHz)
  constexpr int32_t T_HDR_SHORT = 2920;        // ON header mark
  constexpr int32_t T_HDR_LONG = 4950;         // OFF/fan_only header mark
  constexpr int32_t T_HDR_SPACE = 9000;        // Header/inter-burst space
  constexpr int32_t T_BIT_MARK = 450;          // Data bit mark
  constexpr int32_t T_INTER_MARK = 3000;       // Inter-burst mark
  constexpr int32_t T_SPACE_THRESHOLD = 1100;  // Above = logic 1, below = logic 0

  DecodedState state{};
  size_t idx = 0;

  auto in_range = [](int32_t val, int32_t expected) -> bool {
    return val >= expected * 3 / 4 && val <= expected * 5 / 4;
  };

  // Header mark: short (~2920) = ON, long (~4950) = OFF/fan_only
  if (idx >= count)
    return state;
  if (!in_range(timings[idx], T_HDR_SHORT) && !in_range(timings[idx], T_HDR_LONG))
    return state;
  bool long_header = (timings[idx] > 4000);
  idx++;

  // Header space
  if (idx >= count || !in_range(timings[idx], T_HDR_SPACE))
    return state;
  idx++;

  // Decode burst 1: 56 bits
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

  // Verify burst 1 prefix
  if (b1[1] != 1 || b1[9] != 1)
    return state;

  // Inter-burst: trailing_mark + inter_space + inter_mark + inter_space
  if (idx >= count || !in_range(timings[idx], T_BIT_MARK))
    return state;
  idx++;
  if (idx >= count || !in_range(timings[idx], T_HDR_SPACE))
    return state;
  idx++;
  if (idx >= count || !in_range(timings[idx], T_INTER_MARK))
    return state;
  idx++;
  if (idx >= count || !in_range(timings[idx], T_HDR_SPACE))
    return state;
  idx++;

  // Decode burst 2: 56 bits
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

  // OFF commands have 3 bursts, but only the first 2 are needed for identification:
  // long_header + mode != fan_only → OFF. Burst 3 is intentionally ignored.
  return decode_from_bits(b1, b2, long_header);
}

}  // namespace samsung_aqv
}  // namespace esphome
