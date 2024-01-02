/*
 * Copyright (c) Shelly-HomeKit Contributors
 * All rights reserved
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "mgos_hap_chars.hpp"
#include "shelly_switch.hpp"

namespace shelly {
namespace hap {

class LightTimer : public ShellySwitch {
 public:
  LightTimer(int id, Input *in, Output *out, PowerMeter *out_pm, Output *led_out,
        struct mgos_config_sw *cfg);
  virtual ~LightTimer();

  Status Init() override;
  void SetOutputState(bool new_state, const char *source) override;

  void ArmUpdateTimer() override;

 protected:
  mgos::hap::UInt8Characteristic *brightness_characteristic;

  mgos::Timer update_timer_;

  uint8_t last_auto_off_remaining_percent_ = 255;
  bool last_output_state_ = false;

  void UpdateCB();
  void SetAutoOffPercent(uint8_t percent, const std::string &source);
  uint8_t GetAutoOffRemainingPercent() const;
  void checkConfig();
};

}  // namespace hap
}  // namespace shelly
