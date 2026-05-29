#pragma once

#include "esphome/components/climate_ir/climate_ir.h"

namespace esphome {
namespace samsung_aqv {

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
};

}  // namespace samsung_aqv
}  // namespace esphome
