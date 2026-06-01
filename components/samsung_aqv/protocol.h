#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Samsung AQV IR Protocol — pure C++17 encoding/decoding logic.
// No ESPHome dependencies. Compiles standalone for tests and CLI tools.
//
// Protocol: 38 kHz pulse-distance, 56 bits per burst.
//   ON command  = 2 bursts (burst1: fan/header, burst2: mode/temp/swing)
//   OFF command = 3 bursts (fixed payload)
//
// Models: AQV18NSCN, AQV09NSAX, Samsung AQV family (ARH-466 remote)

namespace esphome {
namespace samsung_aqv {

// ─── Pronto timing constants (captured from ARH-466 remote) ─────────────────

constexpr uint16_t P_FREQ = 0x006D;        // 38 kHz carrier
constexpr uint16_t P_HDR_SHORT = 0x006F;   // Header mark (~2920 µs) — all commands
constexpr uint16_t P_HDR_SPACE = 0x015F;   // Header space (~9230 µs)
constexpr uint16_t P_INTER_MARK = 0x0071;  // Inter-burst mark (~2970 µs)
constexpr uint16_t P_INTER_GAP = 0x0048;   // Inter-burst gap (~1900 µs)
constexpr uint16_t P_MARK = 0x0011;        // Bit mark (~450 µs)
constexpr uint16_t P_SPACE_0 = 0x0018;     // Logic 0 space (~630 µs)
constexpr uint16_t P_SPACE_1 = 0x003E;     // Logic 1 space (~1630 µs)
constexpr uint16_t P_TAIL = 0x0181;        // Final tail space (~10124 µs)

// ─── Enums ──────────────────────────────────────────────────────────────────

enum Mode { MODE_COOL, MODE_HEAT, MODE_DRY, MODE_FAN_ONLY, MODE_HEAT_COOL };
enum Fan { FAN_AUTO, FAN_QUIET, FAN_LOW, FAN_MEDIUM, FAN_HIGH };
enum Swing { SWING_ON, SWING_OFF };  // ON = vanes moving, OFF = vanes stopped

// ─── Fan fallback logic ─────────────────────────────────────────────────────
// Corrects invalid mode/fan combinations per Samsung manual (DB98-28490A):
//   Dry       → always auto
//   Fan_only  → no auto/quiet (falls back to low)
//   Heat_cool → no quiet (falls back to auto)

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

// ─── Bit-level helpers ──────────────────────────────────────────────────────

// Fan speed → 3-bit value for burst2 bits 41-43 (MSB-first)
inline uint8_t fan_bits(Fan fan) {
  switch (fan) {
    case FAN_LOW:
      return 0b010;
    case FAN_MEDIUM:
      return 0b001;
    case FAN_HIGH:
      return 0b101;
    default:
      return 0b000;  // auto and quiet both encode as 000
  }
}

// Mode → 3-bit value for burst2 bits 44-46 (MSB-first)
inline uint8_t mode_bits(Mode mode) {
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

// Reverse n bits (used for checksum encoding)
inline uint8_t reverse_bits(uint8_t val, int n) {
  uint8_t r = 0;
  for (int i = 0; i < n; i++)
    r |= ((val >> i) & 1) << (n - 1 - i);
  return r;
}

// Checksum: reverse_5bit(33 - count_ones(data_bits + extra_ones))
// Stored MSB-first at bits 12-16 of each burst.
inline uint8_t checksum(const uint8_t *bits, int count, int extra_ones = 0) {
  int ones = extra_ones;
  for (int i = 0; i < count; i++)
    ones += bits[i];
  return reverse_bits(33 - ones, 5);
}

// ─── Burst builders ─────────────────────────────────────────────────────────

// Burst 1 (56 bits): device header + fan speed.
// Only the quiet flag (bit 45) and checksum change between fan modes.
inline void build_burst1(uint8_t bits[56], Fan fan) {
  const uint8_t base[56] = {0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0,
                            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1};
  memcpy(bits, base, 56);
  if (fan == FAN_QUIET)
    bits[45] = 1;  // Quiet flag differentiates quiet from auto (both have fan_bits=000)
  uint8_t chk = checksum(bits + 17, 39, 1);
  for (int i = 0; i < 5; i++)
    bits[12 + i] = (chk >> (4 - i)) & 1;
}

// Burst 2 (56 bits): mode, temperature, fan speed bits, swing, checksum.
// Bit layout:
//   [0-11]  prefix (100000000100)
//   [12-16] checksum (5 bits, MSB-first)
//   [17-19] constant (111)
//   [20,22] swing (0=moving, 1=stopped), [21] always 1
//   [23-35] constant
//   [36-39] temperature (LSB-first, value = temp - 16)
//   [40]    constant (1)
//   [41-43] fan speed (MSB-first)
//   [44-46] mode (MSB-first)
//   [47-51] constant (00000)
//   [52-55] constant (1111)
inline void build_burst2(uint8_t bits[56], int temp, Mode mode, Fan fan, Swing swing) {
  memset(bits, 0, 56);
  bits[0] = 1;
  bits[9] = 1;
  bits[17] = 1;
  bits[18] = 1;
  bits[19] = 1;
  uint8_t sw = (swing == SWING_OFF) ? 1 : 0;
  bits[20] = sw;
  bits[21] = 1;
  bits[22] = sw;
  bits[23] = 1;
  bits[24] = 1;
  // Temperature: 4 bits LSB-first
  uint8_t tv = (uint8_t) (temp - 16);
  for (int i = 0; i < 4; i++)
    bits[36 + i] = (tv >> i) & 1;
  bits[40] = 1;
  // Fan speed: 3 bits MSB-first
  uint8_t fb = fan_bits(fan);
  bits[41] = (fb >> 2) & 1;
  bits[42] = (fb >> 1) & 1;
  bits[43] = fb & 1;
  // Mode: 3 bits MSB-first
  uint8_t mb = mode_bits(mode);
  bits[44] = (mb >> 2) & 1;
  bits[45] = (mb >> 1) & 1;
  bits[46] = mb & 1;
  // Suffix
  bits[52] = 1;
  bits[53] = 1;
  bits[54] = 1;
  bits[55] = 1;
  // Checksum over bits 17-55 + 1 implicit trailing bit
  uint8_t chk = checksum(bits + 17, 39, 1);
  for (int i = 0; i < 5; i++)
    bits[12 + i] = (chk >> (4 - i)) & 1;
}

// ─── Pronto encoding ────────────────────────────────────────────────────────

// Append 56 data bits as mark/space pairs to the Pronto pair vector.
inline void append_bits_pronto(std::vector<uint16_t> &pairs, const uint8_t *bits, int count) {
  for (int i = 0; i < count; i++) {
    pairs.push_back(P_MARK);
    pairs.push_back(bits[i] ? P_SPACE_1 : P_SPACE_0);
  }
}

// Format a Pronto hex string from frequency code and timing pairs.
inline std::string pronto_string(uint16_t freq, const std::vector<uint16_t> &pairs) {
  std::string result;
  char buf[6];
  uint16_t n = static_cast<uint16_t>(pairs.size() / 2);
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

// ─── Encode commands ────────────────────────────────────────────────────────

// Encode OFF command (3 fixed bursts, no parameters).
inline std::string encode_off() {
  const uint8_t b1[56] = {0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0,
                          0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1};
  const uint8_t b2[56] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0,
                          0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0};
  const uint8_t b3[56] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0,
                          0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1};

  std::vector<uint16_t> pairs;
  pairs.push_back(P_HDR_SHORT);
  pairs.push_back(P_HDR_SPACE);
  append_bits_pronto(pairs, b1, 56);
  pairs.push_back(P_MARK);
  pairs.push_back(P_INTER_GAP);
  pairs.push_back(P_INTER_MARK);
  pairs.push_back(P_HDR_SPACE);
  append_bits_pronto(pairs, b2, 56);
  pairs.push_back(P_MARK);
  pairs.push_back(P_INTER_GAP);
  pairs.push_back(P_INTER_MARK);
  pairs.push_back(P_HDR_SPACE);
  append_bits_pronto(pairs, b3, 56);
  pairs.push_back(P_MARK);
  pairs.push_back(P_TAIL);
  return pronto_string(P_FREQ, pairs);
}

// Encode ON command. Fan fallback is applied automatically.
inline std::string encode_on(int temp, Mode mode, Fan fan, Swing swing) {
  Fan actual_fan = resolve_fan(mode, fan);
  uint8_t b1[56], b2[56];
  build_burst1(b1, actual_fan);
  build_burst2(b2, temp, mode, actual_fan, swing);

  std::vector<uint16_t> pairs;
  pairs.push_back(P_HDR_SHORT);
  pairs.push_back(P_HDR_SPACE);
  append_bits_pronto(pairs, b1, 56);
  pairs.push_back(P_MARK);
  pairs.push_back(P_INTER_GAP);
  pairs.push_back(P_INTER_MARK);
  pairs.push_back(P_HDR_SPACE);
  append_bits_pronto(pairs, b2, 56);
  pairs.push_back(P_MARK);
  pairs.push_back(P_TAIL);
  return pronto_string(P_FREQ, pairs);
}

// ─── Decoding ───────────────────────────────────────────────────────────────

struct DecodedState {
  bool valid;
  bool is_off;
  Mode mode;
  Fan fan;
  int temp;
  Swing swing;
};

// Decode mode/fan/temp/swing from burst1 + burst2 bit arrays.
// Validates checksums on both bursts. Returns {valid=false} on failure.
// Used by ESPHome on_receive() and by test decode helpers.
inline DecodedState decode_from_bits(const uint8_t b1[56], const uint8_t b2[56]) {
  DecodedState state{};

  // Validate burst 1 checksum
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

  // Validate burst 2 checksum
  int ones = 0;
  for (int i = 17; i < 56; i++)
    ones += b2[i];
  ones += 1;
  uint8_t expected_ck = reverse_bits(33 - (ones % 32), 5);
  uint8_t actual_ck = 0;
  for (int i = 0; i < 5; i++)
    actual_ck |= (b2[12 + i] << (4 - i));
  if (actual_ck != expected_ck)
    return state;

  state.valid = true;
  state.is_off = false;

  // Mode: bits 44-46 MSB-first
  int mode_raw = (b2[44] << 2) | (b2[45] << 1) | b2[46];
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

  // Temperature: bits 36-39 LSB-first, value = raw + 16
  state.temp = (b2[36] | (b2[37] << 1) | (b2[38] << 2) | (b2[39] << 3)) + 16;

  // Fan: quiet flag in burst1 bit 45, speed bits in burst2 bits 41-43
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

  // Swing: bit 20 (0 = moving/on, 1 = stopped/off)
  state.swing = (b2[20] == 1) ? SWING_OFF : SWING_ON;
  return state;
}

}  // namespace samsung_aqv
}  // namespace esphome
