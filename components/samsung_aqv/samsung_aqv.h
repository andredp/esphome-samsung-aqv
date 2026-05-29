#pragma once

#include "esphome/components/climate_ir/climate_ir.h"

namespace esphome {
namespace samsung_aqv {

/// Samsung AQV IR Climate (ARH-466 remote)
/// Protocol: 38kHz, pulse-distance, LSB-first, Pronto hex
/// Models: AQV18NSCN, AQV09NSAX, Samsung AQV family
class SamsungAqvClimate : public climate_ir::ClimateIR {
 public:
  SamsungAqvClimate()
      : climate_ir::ClimateIR(16, 30, 1, true, true,
                              {climate::CLIMATE_FAN_AUTO, climate::CLIMATE_FAN_QUIET,
                               climate::CLIMATE_FAN_LOW, climate::CLIMATE_FAN_MEDIUM,
                               climate::CLIMATE_FAN_HIGH},
                              {climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_VERTICAL}) {}

 protected:
  void transmit_state() override;
  bool on_receive(remote_base::RemoteReceiveData data) override;

 private:
  // Pronto timing constants (captured from ARH-466 remote)
  static constexpr uint16_t P_FREQ = 0x006D;
  static constexpr uint16_t P_HDR_SHORT = 0x006F;   // ON commands
  static constexpr uint16_t P_HDR_LONG = 0x00BC;    // OFF, fan_only
  static constexpr uint16_t P_HDR_SPACE = 0x015F;
  static constexpr uint16_t P_INTER_MARK = 0x0071;
  static constexpr uint16_t P_INTER_SPACE = 0x015E;
  static constexpr uint16_t P_MARK = 0x0011;
  static constexpr uint16_t P_SPACE_0 = 0x0018;
  static constexpr uint16_t P_SPACE_1 = 0x003E;
  static constexpr uint16_t P_TAIL = 0x0181;

  // Timing thresholds for receiver (µs)
  static constexpr int32_t HDR_MARK_MIN = 2500;
  static constexpr int32_t HDR_SPACE_MIN = 8000;
  static constexpr int32_t BIT_MARK_MIN = 200;
  static constexpr int32_t BIT_MARK_MAX = 800;
  static constexpr int32_t BIT_ONE_SPACE = 1000;

  climate::ClimateFanMode resolve_fan_(climate::ClimateMode mode, climate::ClimateFanMode fan);
  uint8_t fan_bits_(climate::ClimateFanMode fan);
  uint8_t mode_bits_(climate::ClimateMode mode);
  uint8_t reverse_bits_(uint8_t val, int n);
  uint8_t checksum_(const uint8_t *bits, int count);

  void build_burst1_(uint8_t *bits, climate::ClimateFanMode fan);
  void build_burst2_(uint8_t *bits, float temp, climate::ClimateMode mode,
                     climate::ClimateFanMode fan, climate::ClimateSwingMode swing);

  void transmit_pronto_(const std::string &data);
  std::string encode_on_();
  std::string encode_off_();
};

}  // namespace samsung_aqv
}  // namespace esphome
