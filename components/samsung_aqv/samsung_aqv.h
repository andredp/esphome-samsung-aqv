#pragma once

#include "esphome/components/climate_ir/climate_ir.h"

#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif

namespace esphome {
namespace samsung_aqv {

class SamsungAqvClimate : public climate_ir::ClimateIR {
 public:
  SamsungAqvClimate()
      : climate_ir::ClimateIR(16, 30, 1, true, true,
                              {climate::CLIMATE_FAN_AUTO, climate::CLIMATE_FAN_QUIET, climate::CLIMATE_FAN_LOW,
                               climate::CLIMATE_FAN_MEDIUM, climate::CLIMATE_FAN_HIGH},
                              {climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_VERTICAL}) {}

#ifdef USE_TEXT_SENSOR
  void set_debug_sensor(text_sensor::TextSensor *sensor) { this->debug_sensor_ = sensor; }
#endif

 protected:
  void transmit_state() override;
  bool on_receive(remote_base::RemoteReceiveData data) override;
  bool decode_(remote_base::RemoteReceiveData &data);

#ifdef USE_TEXT_SENSOR
  text_sensor::TextSensor *debug_sensor_{nullptr};
#endif
};

}  // namespace samsung_aqv
}  // namespace esphome
