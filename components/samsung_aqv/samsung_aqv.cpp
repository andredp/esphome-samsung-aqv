#include "samsung_aqv.h"
#include "protocol.h"
#include "esphome/components/remote_base/pronto_protocol.h"
#include "esphome/core/log.h"

namespace esphome {
namespace samsung_aqv {

static const char *const TAG = "samsung_aqv";

static Mode to_proto_mode(climate::ClimateMode m) {
  switch (m) {
    case climate::CLIMATE_MODE_COOL: return MODE_COOL;
    case climate::CLIMATE_MODE_HEAT: return MODE_HEAT;
    case climate::CLIMATE_MODE_DRY: return MODE_DRY;
    case climate::CLIMATE_MODE_FAN_ONLY: return MODE_FAN_ONLY;
    default: return MODE_HEAT_COOL;
  }
}

static Fan to_proto_fan(climate::ClimateFanMode f) {
  switch (f) {
    case climate::CLIMATE_FAN_QUIET: return FAN_QUIET;
    case climate::CLIMATE_FAN_LOW: return FAN_LOW;
    case climate::CLIMATE_FAN_MEDIUM: return FAN_MEDIUM;
    case climate::CLIMATE_FAN_HIGH: return FAN_HIGH;
    default: return FAN_AUTO;
  }
}

static Swing to_proto_swing(climate::ClimateSwingMode s) {
  return s == climate::CLIMATE_SWING_VERTICAL ? SWING_ON : SWING_OFF;
}

void SamsungAqvClimate::transmit_state() {
  std::string pronto;
  if (this->mode == climate::CLIMATE_MODE_OFF) {
    pronto = encode_off();
  } else {
    auto mode = to_proto_mode(this->mode);
    auto fan = to_proto_fan(this->fan_mode.value_or(climate::CLIMATE_FAN_AUTO));
    auto swing = to_proto_swing(this->swing_mode);
    fan = resolve_fan(mode, fan);
    int temp = (int) this->target_temperature;
    pronto = encode_on(temp, mode, fan, swing);
  }

  remote_base::ProntoData data{};
  data.data = pronto;
  data.delta = 0;
  this->transmit_<remote_base::ProntoProtocol>(data);

  ESP_LOGD(TAG, "Sent %s mode=%d temp=%d", this->mode == climate::CLIMATE_MODE_OFF ? "OFF" : "ON",
           (int) this->mode, (int) this->target_temperature);
}

bool SamsungAqvClimate::on_receive(remote_base::RemoteReceiveData data) {
  ESP_LOGD(TAG, "on_receive called, size=%d", data.size());

  // Data starts with header space (~9000µs) - header mark triggered capture
  if (!data.expect_space(8900)) {
    ESP_LOGV(TAG, "Header space not matched");
    return false;
  }

  ESP_LOGD(TAG, "Header matched, decoding burst 1");

  // Decode burst 1: 56 bits
  uint8_t b1[56] = {};
  for (int i = 0; i < 56; i++) {
    if (!data.expect_mark(450)) {
      ESP_LOGD(TAG, "Burst 1 mark failed at bit %d", i);
      return false;
    }
    if (data.expect_space(1630)) {
      b1[i] = 1;
    } else if (data.expect_space(630)) {
      b1[i] = 0;
    } else {
      ESP_LOGD(TAG, "Burst 1 space failed at bit %d", i);
      return false;
    }
  }

  // Verify burst 1 prefix: bit 1 and bit 9 must be 1
  if (b1[1] != 1 || b1[9] != 1) {
    ESP_LOGD(TAG, "Burst 1 prefix check failed: b1[1]=%d b1[9]=%d", b1[1], b1[9]);
    return false;
  }

  ESP_LOGD(TAG, "Burst 1 decoded, looking for burst 2 header");

  // After 56 bits decoded, next is inter-burst mark (~2940) + burst 2 header space (~9089)
  if (!data.expect_item(2920, 8900)) {
    if (!data.expect_item(4950, 8900)) {
      ESP_LOGD(TAG, "Burst 2 header failed");
      return false;
    }
  }

  ESP_LOGD(TAG, "Decoding burst 2");

  // Decode burst 2: 56 bits
  uint8_t b2[56] = {};
  for (int i = 0; i < 56; i++) {
    if (!data.expect_mark(450)) {
      ESP_LOGD(TAG, "Burst 2 mark failed at bit %d", i);
      return false;
    }
    if (data.expect_space(1630)) {
      b2[i] = 1;
    } else if (data.expect_space(630)) {
      b2[i] = 0;
    } else {
      ESP_LOGD(TAG, "Burst 2 space failed at bit %d", i);
      return false;
    }
  }

  ESP_LOGD(TAG, "Burst 2 decoded, validating checksum");

  // Validate checksum on burst 2
  int ones = 0;
  for (int i = 17; i < 56; i++) ones += b2[i];
  ones += 1;  // implicit trailing bit
  uint8_t expected_ck = reverse_bits(33 - (ones % 32), 5);
  uint8_t actual_ck = 0;
  for (int i = 0; i < 5; i++) actual_ck |= (b2[12 + i] << (4 - i));
  if (actual_ck != expected_ck) {
    ESP_LOGD(TAG, "Checksum failed: expected=%d actual=%d", expected_ck, actual_ck);
    return false;
  }

  // Decode temperature (bits 36-39, LSB-first)
  int temp_raw = b2[36] | (b2[37] << 1) | (b2[38] << 2) | (b2[39] << 3);
  this->target_temperature = temp_raw + 16;

  // Decode mode (bits 44-46, MSB-first)
  int mode_raw = (b2[44] << 2) | (b2[45] << 1) | b2[46];
  switch (mode_raw) {
    case 0b100: this->mode = climate::CLIMATE_MODE_COOL; break;
    case 0b001: this->mode = climate::CLIMATE_MODE_HEAT; break;
    case 0b010: this->mode = climate::CLIMATE_MODE_DRY; break;
    case 0b110: this->mode = climate::CLIMATE_MODE_FAN_ONLY; break;
    case 0b011: this->mode = climate::CLIMATE_MODE_HEAT_COOL; break;
    default: return false;
  }

  // Decode fan (bits 41-43, MSB-first) + quiet flag (burst 1 bit 45)
  int fan_raw = (b2[41] << 2) | (b2[42] << 1) | b2[43];
  bool quiet = (b1[45] == 1);
  if (quiet) {
    this->fan_mode = climate::CLIMATE_FAN_QUIET;
  } else {
    switch (fan_raw) {
      case 0b010: this->fan_mode = climate::CLIMATE_FAN_LOW; break;
      case 0b001: this->fan_mode = climate::CLIMATE_FAN_MEDIUM; break;
      case 0b101: this->fan_mode = climate::CLIMATE_FAN_HIGH; break;
      default: this->fan_mode = climate::CLIMATE_FAN_AUTO; break;
    }
  }

  // Decode swing (burst 2 bits 20, 22: 0=moving, 1=stopped)
  this->swing_mode = (b2[20] == 0) ? climate::CLIMATE_SWING_VERTICAL : climate::CLIMATE_SWING_OFF;

  this->publish_state();
  ESP_LOGD(TAG, "Received: mode=%d temp=%.0f fan=%d swing=%d",
           (int) this->mode, this->target_temperature,
           (int) this->fan_mode.value_or(climate::CLIMATE_FAN_AUTO), (int) this->swing_mode);
  return true;
}

}  // namespace samsung_aqv
}  // namespace esphome
