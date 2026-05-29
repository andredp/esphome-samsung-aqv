#include "samsung_aqv.h"
#include "esphome/core/log.h"

namespace esphome {
namespace samsung_aqv {

static const char *const TAG = "samsung_aqv.climate";

// --- Fan fallback mapping ---
climate::ClimateFanMode SamsungAqvClimate::resolve_fan_(climate::ClimateMode mode, climate::ClimateFanMode fan) {
  switch (mode) {
    case climate::CLIMATE_MODE_DRY:
      return climate::CLIMATE_FAN_AUTO;
    case climate::CLIMATE_MODE_FAN_ONLY:
      if (fan == climate::CLIMATE_FAN_AUTO || fan == climate::CLIMATE_FAN_QUIET)
        return climate::CLIMATE_FAN_LOW;
      return fan;
    case climate::CLIMATE_MODE_HEAT_COOL:
      if (fan == climate::CLIMATE_FAN_QUIET)
        return climate::CLIMATE_FAN_AUTO;
      return fan;
    default:
      return fan;
  }
}

uint8_t SamsungAqvClimate::fan_bits_(climate::ClimateFanMode fan) {
  switch (fan) {
    case climate::CLIMATE_FAN_LOW:    return 0b010;
    case climate::CLIMATE_FAN_MEDIUM: return 0b001;
    case climate::CLIMATE_FAN_HIGH:   return 0b101;
    default:                          return 0b000;  // auto & quiet
  }
}

uint8_t SamsungAqvClimate::mode_bits_(climate::ClimateMode mode) {
  switch (mode) {
    case climate::CLIMATE_MODE_COOL:      return 0b001;
    case climate::CLIMATE_MODE_HEAT:      return 0b010;
    case climate::CLIMATE_MODE_DRY:       return 0b100;
    case climate::CLIMATE_MODE_FAN_ONLY:  return 0b110;
    case climate::CLIMATE_MODE_HEAT_COOL: return 0b011;
    default:                              return 0b001;
  }
}

uint8_t SamsungAqvClimate::reverse_bits_(uint8_t val, int n) {
  uint8_t r = 0;
  for (int i = 0; i < n; i++)
    r |= ((val >> i) & 1) << (n - 1 - i);
  return r;
}

uint8_t SamsungAqvClimate::checksum_(const uint8_t *bits, int count) {
  int ones = 0;
  for (int i = 0; i < count; i++) ones += bits[i];
  return reverse_bits_(33 - ones, 5);
}

void SamsungAqvClimate::build_burst1_(uint8_t *bits, climate::ClimateFanMode fan) {
  const uint8_t base[56] = {
    0,1,0,0,0,0,0,0, 0,1,0,0,1,0,0,1, 1,1,1,1,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0
  };
  memcpy(bits, base, 56);
  if (fan == climate::CLIMATE_FAN_QUIET) bits[41] = 1;
  // Checksum: bits 12-16 over bits 17-55
  uint8_t chk = this->checksum_(bits + 17, 39);
  for (int i = 0; i < 5; i++) bits[12 + i] = (chk >> (4 - i)) & 1;
}

void SamsungAqvClimate::build_burst2_(uint8_t *bits, float temp, climate::ClimateMode mode,
                                       climate::ClimateFanMode fan, climate::ClimateSwingMode swing) {
  memset(bits, 0, 56);
  // Prefix: 100000000100
  bits[0] = 1; bits[9] = 1; bits[11] = 1;  // wait, let me be explicit
  // Actually set each bit of prefix "100000000100"
  bits[0]=1; bits[1]=0; bits[2]=0; bits[3]=0; bits[4]=0; bits[5]=0; bits[6]=0; bits[7]=0;
  bits[8]=0; bits[9]=1; bits[10]=0; bits[11]=0;

  // Bits 17-19: 111
  bits[17]=1; bits[18]=1; bits[19]=1;

  // Swing: bits 20, 22
  uint8_t sw = (swing == climate::CLIMATE_SWING_OFF) ? 1 : 0;
  bits[20] = sw; bits[21] = 1; bits[22] = sw;

  // Bits 23-24: 11, bits 25-35: 0
  bits[23]=1; bits[24]=1;

  // Temperature: bits 36-39 (LSB-first, val = temp-16)
  uint8_t tv = (uint8_t)(temp - 16);
  for (int i = 0; i < 4; i++) bits[36 + i] = (tv >> i) & 1;

  // Bit 40: 1
  bits[40] = 1;

  // Fan: bits 41-43
  uint8_t fb = this->fan_bits_(fan);
  for (int i = 0; i < 3; i++) bits[41 + i] = (fb >> i) & 1;

  // Mode: bits 44-46
  uint8_t mb = this->mode_bits_(mode);
  for (int i = 0; i < 3; i++) bits[44 + i] = (mb >> i) & 1;

  // Bits 47-52: 0, bits 53-54: 11, bit 55: 1
  bits[53]=1; bits[54]=1; bits[55]=1;

  // Checksum: bits 12-16 over bits 17-55
  uint8_t chk = this->checksum_(bits + 17, 39);
  for (int i = 0; i < 5; i++) bits[12 + i] = (chk >> (4 - i)) & 1;
}

// --- Pronto encoding helpers ---

static void append_bits_pronto(std::vector<uint16_t> &pairs, const uint8_t *bits, int count,
                                uint16_t mark, uint16_t s0, uint16_t s1) {
  for (int i = 0; i < count; i++) {
    pairs.push_back(mark);
    pairs.push_back(bits[i] ? s1 : s0);
  }
}

static std::string pronto_to_string(uint16_t freq, const std::vector<uint16_t> &pairs) {
  std::string result;
  char buf[6];
  int n = pairs.size() / 2;
  snprintf(buf, sizeof(buf), "%04X", 0); result += buf; result += ' ';
  snprintf(buf, sizeof(buf), "%04X", freq); result += buf; result += ' ';
  snprintf(buf, sizeof(buf), "%04X", n); result += buf; result += ' ';
  snprintf(buf, sizeof(buf), "%04X", 0); result += buf;
  for (auto v : pairs) {
    result += ' ';
    snprintf(buf, sizeof(buf), "%04X", v);
    result += buf;
  }
  return result;
}

std::string SamsungAqvClimate::encode_off_() {
  const uint8_t b1[56] = {0,1,0,0,0,0,0,0,0,1,0,0,1,1,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1};
  const uint8_t b2[56] = {1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,1,0,1,1,0,0,0,0,0,0,0,0,0,0};
  const uint8_t b3[56] = {1,0,0,0,0,0,0,0,0,1,0,0,0,1,0,0,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,1,0,1,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,1,1,0,0};

  // Fix: b3 is 54 bits in the original, pad to 56
  // Actually from protocol doc the OFF burst 3 is also 56 bits. Let me use the verified bits.

  std::vector<uint16_t> pairs;
  pairs.push_back(P_HDR_LONG); pairs.push_back(P_HDR_SPACE);
  append_bits_pronto(pairs, b1, 56, P_MARK, P_SPACE_0, P_SPACE_1);
  pairs.push_back(P_MARK); pairs.push_back(P_INTER_SPACE);
  pairs.push_back(P_INTER_MARK); pairs.push_back(P_INTER_SPACE);
  append_bits_pronto(pairs, b2, 56, P_MARK, P_SPACE_0, P_SPACE_1);
  pairs.push_back(P_MARK); pairs.push_back(P_INTER_SPACE);
  pairs.push_back(P_INTER_MARK); pairs.push_back(P_INTER_SPACE);
  append_bits_pronto(pairs, b3, 56, P_MARK, P_SPACE_0, P_SPACE_1);
  pairs.push_back(P_MARK); pairs.push_back(P_TAIL);

  return pronto_to_string(P_FREQ, pairs);
}

std::string SamsungAqvClimate::encode_on_() {
  auto fan = this->resolve_fan_(this->mode, *this->fan_mode);
  float temp = this->target_temperature;
  if (temp < 16) temp = 16;
  if (temp > 30) temp = 30;

  uint8_t b1[56], b2[56];
  this->build_burst1_(b1, fan);
  this->build_burst2_(b2, temp, this->mode, fan, *this->swing_mode);

  uint16_t hdr = (this->mode == climate::CLIMATE_MODE_FAN_ONLY) ? P_HDR_LONG : P_HDR_SHORT;

  std::vector<uint16_t> pairs;
  pairs.push_back(hdr); pairs.push_back(P_HDR_SPACE);
  append_bits_pronto(pairs, b1, 56, P_MARK, P_SPACE_0, P_SPACE_1);
  pairs.push_back(P_MARK); pairs.push_back(P_INTER_SPACE);
  pairs.push_back(P_INTER_MARK); pairs.push_back(P_INTER_SPACE);
  append_bits_pronto(pairs, b2, 56, P_MARK, P_SPACE_0, P_SPACE_1);
  pairs.push_back(P_MARK); pairs.push_back(P_TAIL);

  return pronto_to_string(P_FREQ, pairs);
}

void SamsungAqvClimate::transmit_state() {
  std::string pronto;
  if (this->mode == climate::CLIMATE_MODE_OFF) {
    pronto = this->encode_off_();
  } else {
    pronto = this->encode_on_();
  }
  ESP_LOGD(TAG, "Sending: %s", pronto.c_str());
  this->transmit_pronto_(pronto);
}

void SamsungAqvClimate::transmit_pronto_(const std::string &data) {
  auto transmit = this->transmitter_->transmit();
  auto *tx_data = transmit.get_data();
  // Parse Pronto hex and convert to raw timing
  // Pronto format: 0000 FREQ N_PAIRS 0000 [mark space]...
  std::vector<uint16_t> codes;
  std::istringstream ss(data);
  std::string token;
  while (ss >> token) {
    codes.push_back((uint16_t)strtoul(token.c_str(), nullptr, 16));
  }
  if (codes.size() < 4) return;

  uint16_t freq_code = codes[1];
  // Pronto frequency: 1000000 / (freq_code * 0.241246)
  float period_us = freq_code * 0.241246f;
  uint32_t carrier_freq = (uint32_t)(1000000.0f / period_us);

  tx_data->set_carrier_frequency(carrier_freq);
  // Convert pairs to raw timing (mark positive, space negative in µs)
  for (size_t i = 4; i < codes.size(); i++) {
    int32_t us = (int32_t)(codes[i] * period_us);
    if (i % 2 == 0) {
      tx_data->mark(us);
    } else {
      tx_data->space(us);
    }
  }
  transmit.perform();
}

// --- IR Receiver ---
bool SamsungAqvClimate::on_receive(remote_base::RemoteReceiveData data) {
  // Need at least ~230 edges for a 2-burst ON command
  if (data.size() < 230) return false;

  // Find header: mark > 2500µs followed by space > 8000µs
  int hdr_idx = -1;
  for (int i = 0; i < (int)data.size() - 1; i += 2) {
    if (data.peek_mark(HDR_MARK_MIN, i) && data.peek_space(HDR_SPACE_MIN, i + 1)) {
      hdr_idx = i;
      break;
    }
  }
  if (hdr_idx < 0) return false;

  // Determine OFF vs ON by header mark length
  // OFF/fan_only: ~4900µs, ON: ~2900µs
  bool is_off = data.peek_mark(4000, hdr_idx);

  // Decode 56 bits from burst starting after header
  auto decode_burst = [&](int start) -> bool {
    // We'll decode into a temporary buffer
    return start + 56 * 2 <= (int)data.size();
  };

  int b1_start = hdr_idx + 2;
  if (b1_start + 56 * 2 > (int)data.size()) return false;

  // Decode burst 1 bits
  uint8_t b1[56];
  for (int i = 0; i < 56; i++) {
    int idx = b1_start + i * 2;
    if (idx + 1 >= (int)data.size()) return false;
    // Mark should be ~450µs, space determines bit value
    if (!data.peek_mark(BIT_MARK_MIN, idx)) return false;
    b1[i] = data.peek_space(BIT_ONE_SPACE, idx + 1) ? 1 : 0;
  }

  // Verify this looks like Samsung AQV (byte 0 should be 0x02 in LSB = bit pattern 01000000)
  if (b1[0] != 0 || b1[1] != 1) return false;

  if (is_off) {
    this->mode = climate::CLIMATE_MODE_OFF;
    this->publish_state();
    ESP_LOGD(TAG, "Received OFF");
    return true;
  }

  // Find burst 2: scan for next inter-burst mark > 2500µs
  int b2_start = -1;
  for (int i = b1_start + 56 * 2; i < (int)data.size() - 1; i += 2) {
    if (data.peek_mark(HDR_MARK_MIN, i) && data.peek_space(HDR_SPACE_MIN, i + 1)) {
      b2_start = i + 2;
      break;
    }
  }
  if (b2_start < 0 || b2_start + 56 * 2 > (int)data.size()) return false;

  // Decode burst 2 bits
  uint8_t b2[56];
  for (int i = 0; i < 56; i++) {
    int idx = b2_start + i * 2;
    if (idx + 1 >= (int)data.size()) return false;
    if (!data.peek_mark(BIT_MARK_MIN, idx)) return false;
    b2[i] = data.peek_space(BIT_ONE_SPACE, idx + 1) ? 1 : 0;
  }

  // Decode fields from burst 2
  // Temperature: bits 36-39 (LSB-first, value = temp-16)
  int temp_raw = b2[36] | (b2[37] << 1) | (b2[38] << 2) | (b2[39] << 3);
  float temp = temp_raw + 16.0f;

  // Fan: burst 1 bit 41 = quiet flag, burst 2 bits 41-43
  bool quiet = (b1[41] == 1);
  int fan_raw = b2[41] | (b2[42] << 1) | (b2[43] << 2);

  // Mode: bits 44-46
  int mode_raw = b2[44] | (b2[45] << 1) | (b2[46] << 2);

  // Swing: bit 20 (0=moving, 1=stopped)
  bool swing_off = (b2[20] == 1);

  // Map mode
  switch (mode_raw) {
    case 0b001: this->mode = climate::CLIMATE_MODE_COOL; break;
    case 0b010: this->mode = climate::CLIMATE_MODE_HEAT; break;
    case 0b100: this->mode = climate::CLIMATE_MODE_DRY; break;
    case 0b110: this->mode = climate::CLIMATE_MODE_FAN_ONLY; break;
    case 0b011: this->mode = climate::CLIMATE_MODE_HEAT_COOL; break;
    default: return false;
  }

  // Map fan
  if (quiet) {
    this->fan_mode = climate::CLIMATE_FAN_QUIET;
  } else {
    switch (fan_raw) {
      case 0b000: this->fan_mode = climate::CLIMATE_FAN_AUTO; break;
      case 0b010: this->fan_mode = climate::CLIMATE_FAN_LOW; break;
      case 0b001: this->fan_mode = climate::CLIMATE_FAN_MEDIUM; break;
      case 0b101: this->fan_mode = climate::CLIMATE_FAN_HIGH; break;
      default: this->fan_mode = climate::CLIMATE_FAN_AUTO; break;
    }
  }

  this->target_temperature = temp;
  this->swing_mode = swing_off ? climate::CLIMATE_SWING_OFF : climate::CLIMATE_SWING_VERTICAL;
  this->publish_state();

  ESP_LOGD(TAG, "Received: mode=%d temp=%.0f fan=%d swing=%s",
           this->mode, temp, *this->fan_mode, swing_off ? "off" : "on");
  return true;
}

}  // namespace samsung_aqv
}  // namespace esphome
