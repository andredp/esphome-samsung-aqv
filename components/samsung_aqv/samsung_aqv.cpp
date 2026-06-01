#include "samsung_aqv.h"
#include "protocol.h"
#include "esphome/components/remote_base/pronto_protocol.h"
#include "esphome/core/log.h"
#include <cinttypes>

namespace esphome {
namespace samsung_aqv {

static const char *const TAG = "samsung_aqv";

// Receiver timing constants (µs) — nominal values, ESPHome applies ±25% tolerance
static constexpr int RX_HDR_MARK_SHORT = 2920;     // ON header mark
static constexpr int RX_HDR_MARK_LONG = 4950;      // fan_only header mark
static constexpr int RX_HDR_SPACE = 8900;          // Header/inter-burst space
static constexpr int RX_BIT_MARK = 450;            // Data bit mark
static constexpr int RX_SPACE_ONE = 1630;          // Logic 1 space
static constexpr int RX_SPACE_ZERO = 630;          // Logic 0 space
static constexpr int RX_INTER_SPACE = 1900;        // Inter-burst gap space
static constexpr int RX_OFF_SIZE_THRESHOLD = 280;  // 3 bursts > this, 2 bursts < this

static Mode to_proto_mode(climate::ClimateMode m) {
  switch (m) {
    case climate::CLIMATE_MODE_COOL:
      return MODE_COOL;
    case climate::CLIMATE_MODE_HEAT:
      return MODE_HEAT;
    case climate::CLIMATE_MODE_DRY:
      return MODE_DRY;
    case climate::CLIMATE_MODE_FAN_ONLY:
      return MODE_FAN_ONLY;
    default:
      return MODE_HEAT_COOL;
  }
}

static Fan to_proto_fan(climate::ClimateFanMode f) {
  switch (f) {
    case climate::CLIMATE_FAN_QUIET:
      return FAN_QUIET;
    case climate::CLIMATE_FAN_LOW:
      return FAN_LOW;
    case climate::CLIMATE_FAN_MEDIUM:
      return FAN_MEDIUM;
    case climate::CLIMATE_FAN_HIGH:
      return FAN_HIGH;
    default:
      return FAN_AUTO;
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

  ESP_LOGD(TAG, "Sent %s mode=%d temp=%d", this->mode == climate::CLIMATE_MODE_OFF ? "OFF" : "ON", (int) this->mode,
           (int) this->target_temperature);
}

bool SamsungAqvClimate::on_receive(remote_base::RemoteReceiveData data) {
  if (this->decode_(data))
    return true;
#ifdef USE_TEXT_SENSOR
  if (this->debug_sensor_ != nullptr) {
    auto raw = data.get_raw_data();
    char buf[128];
    int pos = 0;
    int n = std::min((int) raw.size(), 20);
    for (int i = 0; i < n && pos < 120; i++)
      pos += snprintf(buf + pos, sizeof(buf) - pos, i ? " %" PRId32 : "%" PRId32, raw[i]);
    snprintf(buf + pos, sizeof(buf) - pos, " (size=%d)", (int) raw.size());
    this->debug_sensor_->publish_state(buf);
  }
#endif
  return false;
}

bool SamsungAqvClimate::decode_(remote_base::RemoteReceiveData &data) {
  ESP_LOGD(TAG, "on_receive called, size=%d", data.size());

  // OFF = 3 bursts (~347-348 items), ON = 2 bursts (~231-232 items)
  if (data.size() > RX_OFF_SIZE_THRESHOLD) {
    ESP_LOGD(TAG, "Received OFF (3 bursts, size=%d)", data.size());
    this->mode = climate::CLIMATE_MODE_OFF;
    this->publish_state();
    return true;
  }

  // Header mark may or may not be in buffer (receiver timing dependent).
  // Try to consume it; if buffer starts with space, skip to header space.
  data.expect_mark(RX_HDR_MARK_SHORT) || data.expect_mark(RX_HDR_MARK_LONG);

  // Header space
  if (!data.expect_space(RX_HDR_SPACE))
    return false;

  // Decode burst 1: 56 bits
  uint8_t b1[56] = {};
  for (int i = 0; i < 56; i++) {
    if (!data.expect_mark(RX_BIT_MARK))
      return false;
    if (data.expect_space(RX_SPACE_ONE))
      b1[i] = 1;
    else if (data.expect_space(RX_SPACE_ZERO))
      b1[i] = 0;
    else
      return false;
  }

  // Verify burst 1 prefix
  if (b1[1] != 1 || b1[9] != 1)
    return false;

  // Inter-burst: trailing mark + short space + burst 2 header mark + space
  if (!data.expect_mark(RX_BIT_MARK))
    return false;
  if (!data.expect_space(RX_INTER_SPACE))
    return false;
  if (!(data.expect_item(RX_HDR_MARK_SHORT, RX_HDR_SPACE) || data.expect_item(RX_HDR_MARK_LONG, RX_HDR_SPACE)))
    return false;

  // Decode burst 2: 56 bits
  uint8_t b2[56] = {};
  for (int i = 0; i < 56; i++) {
    if (!data.expect_mark(RX_BIT_MARK))
      return false;
    if (data.expect_space(RX_SPACE_ONE))
      b2[i] = 1;
    else if (data.expect_space(RX_SPACE_ZERO))
      b2[i] = 0;
    else
      return false;
  }

  // Use shared protocol decode
  auto decoded = decode_from_bits(b1, b2);
  if (!decoded.valid)
    return false;
  if (decoded.is_off) {
    this->mode = climate::CLIMATE_MODE_OFF;
    this->publish_state();
    return true;
  }

  // Map protocol enums to ESPHome enums
  switch (decoded.mode) {
    case MODE_COOL:
      this->mode = climate::CLIMATE_MODE_COOL;
      break;
    case MODE_HEAT:
      this->mode = climate::CLIMATE_MODE_HEAT;
      break;
    case MODE_DRY:
      this->mode = climate::CLIMATE_MODE_DRY;
      break;
    case MODE_FAN_ONLY:
      this->mode = climate::CLIMATE_MODE_FAN_ONLY;
      break;
    case MODE_HEAT_COOL:
      this->mode = climate::CLIMATE_MODE_HEAT_COOL;
      break;
  }
  this->target_temperature = decoded.temp;
  switch (decoded.fan) {
    case FAN_QUIET:
      this->fan_mode = climate::CLIMATE_FAN_QUIET;
      break;
    case FAN_LOW:
      this->fan_mode = climate::CLIMATE_FAN_LOW;
      break;
    case FAN_MEDIUM:
      this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
      break;
    case FAN_HIGH:
      this->fan_mode = climate::CLIMATE_FAN_HIGH;
      break;
    default:
      this->fan_mode = climate::CLIMATE_FAN_AUTO;
      break;
  }
  this->swing_mode = (decoded.swing == SWING_ON) ? climate::CLIMATE_SWING_VERTICAL : climate::CLIMATE_SWING_OFF;

  this->publish_state();
  ESP_LOGD(TAG, "Received: mode=%d temp=%d fan=%d swing=%d", (int) this->mode, decoded.temp, (int) decoded.fan,
           (int) decoded.swing);
  return true;
}

}  // namespace samsung_aqv
}  // namespace esphome
