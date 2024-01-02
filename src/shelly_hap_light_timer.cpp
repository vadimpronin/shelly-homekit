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

#include "shelly_hap_light_timer.hpp"

#include "mgos_hap_accessory.hpp"

namespace shelly {
namespace hap {

LightTimer::LightTimer(int id, Input *in, Output *out, PowerMeter *out_pm,
                       Output *led_out, struct mgos_config_sw *cfg)
    : ShellySwitch(id, in, out, out_pm, led_out, cfg),
      update_timer_(std::bind(&LightTimer::UpdateCB, this)) {
}

LightTimer::~LightTimer() {
}

Status LightTimer::Init() {
  auto st = ShellySwitch::Init();
  if (!st.ok()) return st;

  checkConfig();

  const int id1 = id() - 1;  // IDs used to start at 0, preserve compat.
  uint16_t iid =
      SHELLY_HAP_IID_BASE_LIGHTING + (SHELLY_HAP_IID_STEP_LIGHTING * id1);
  svc_.iid = iid++;
  svc_.serviceType = &kHAPServiceType_LightBulb;
  svc_.debugDescription = kHAPServiceDebugDescription_LightBulb;
  // Name
  AddNameChar(iid++, cfg_->name);

  // On
  auto *on_char = new mgos::hap::BoolCharacteristic(
      iid++, &kHAPCharacteristicType_On,
      [this](HAPAccessoryServerRef *, const HAPBoolCharacteristicReadRequest *,
             bool *value) {
        *value = out_->GetState() ^ cfg_->hk_state_inverted;
        return kHAPError_None;
      },
      true /* supports_notification */,
      [this](HAPAccessoryServerRef *, const HAPBoolCharacteristicWriteRequest *,
             bool value) {
        SetOutputState(value ^ cfg_->hk_state_inverted, kCHangeReasonHAP);
        return kHAPError_None;
      },
      kHAPCharacteristicDebugDescription_On);
  state_notify_chars_.push_back(on_char);
  AddChar(on_char);

  // Brightness
  brightness_characteristic = new mgos::hap::UInt8Characteristic(
      iid++, &kHAPCharacteristicType_Brightness, 0, 100, cfg_->lb_timer_step,
      [this](HAPAccessoryServerRef *, const HAPUInt8CharacteristicReadRequest *,
             uint8_t *value) {
        *value = GetAutoOffRemainingPercent();
        LOG(LL_INFO, ("Get brightness: %d%%", *value));
        return kHAPError_None;
      },
      true /* supports_notification */,
      [this](HAPAccessoryServerRef *server UNUSED_ARG,
             const HAPUInt8CharacteristicWriteRequest *request UNUSED_ARG,
             uint8_t value) {
        LOG(LL_INFO,
            ("Brightness write %d: %d", id(), static_cast<int>(value)));
        SetAutoOffPercent(value, kCHangeReasonHAP);
        SendNotifications();
        return kHAPError_None;
      },
      kHAPCharacteristicDebugDescription_Brightness);

  state_notify_chars_.push_back(brightness_characteristic);
  AddChar(brightness_characteristic);

  // Power
  AddPowerMeter(&iid);

  return Status::OK();
}

void LightTimer::SetOutputState(bool new_state, const char *source) {
  if (new_state && !cfg_->state && cfg_->lb_timer_rate > 0) {
    SetAutoOffPercent(cfg_->lb_timer_start_value, "SetOutputState");
  } else {
    update_timer_.Clear();
    auto_off_timer_.Clear();
    LOG(LL_INFO, ("Auto off timer disarmed (%s)", source));
  }

  ShellySwitch::SetOutputState(new_state, source);
}

void LightTimer::UpdateCB() {
  uint8_t auto_off_remaining_percent = GetAutoOffRemainingPercent();
  bool output_state = out_->GetState();

  if (last_auto_off_remaining_percent_ == 255 ||
      auto_off_remaining_percent != last_auto_off_remaining_percent_ ||
      output_state != last_output_state_) {
    SendNotifications();
    last_auto_off_remaining_percent_ = auto_off_remaining_percent;
    last_output_state_ = output_state;
  }
}

void LightTimer::SetAutoOffPercent(uint8_t percent, const std::string &source) {
  LOG(LL_INFO, ("Set auto_off percent (%s): %u", source.c_str(), percent));
  if (percent > 0 && cfg_->lb_timer_rate > 0 &&
      (percent <= 95 || cfg_->lb_timer_always_on_enabled == 0)) {
    int seconds = percent * cfg_->lb_timer_rate;

    LOG(LL_INFO, ("Set new auto_off time (%s): %u%% -> %u seconds",
                  source.c_str(), percent, seconds));
    auto_off_timer_.Reset(seconds * 1000, 0);

    ArmUpdateTimer();
  } else {
    LOG(LL_INFO, ("Auto off disabled (%s), requested: %u%%, rate: 1%% = %u "
                  "sec, 95%% always on enabled: %s",
                  source.c_str(), percent, cfg_->lb_timer_rate,
                  cfg_->lb_timer_always_on_enabled ? "on" : "off"));

    auto_off_timer_.Clear();
    update_timer_.Clear();
  }
}

void LightTimer::ArmUpdateTimer() {
  if (!auto_off_timer_.IsValid()) {
    LOG(LL_INFO, ("Auto off timer is not running, skip arming update timer"));
    update_timer_.Clear();
    return;
  }

  LOG(LL_INFO, ("Arming update timer"));
  update_timer_.Reset(1000, MGOS_TIMER_REPEAT);
}

uint8_t LightTimer::GetAutoOffRemainingPercent() const {
  int remaining_msecs = auto_off_timer_.GetMsecsLeft();
  int remaining_secs;
  int remaining_percent;

  if (remaining_msecs == 0) {
    remaining_secs = 0;

    if (out_->GetState() && cfg_->lb_timer_always_on_enabled == 1) {
      // auto off disabled
      remaining_percent = 100;
    } else {
      remaining_percent = 0;
    }
  } else {
    remaining_secs = remaining_msecs / 1000;
    remaining_percent = (remaining_secs / cfg_->lb_timer_rate) + 1;

    if (remaining_percent > 100) {
      remaining_percent = 100;
    } else if (remaining_percent < 0) {
      remaining_percent = 0;
    }
  }

  LOG(LL_DEBUG, ("Auto off remaining seconds: %d, percent: %u%%",
                 remaining_secs, remaining_percent));

  return static_cast<uint8_t>(remaining_percent);
}

void LightTimer::checkConfig() {
  if (cfg_->lb_timer_rate < 0) {
    cfg_->lb_timer_rate = 0;
  }

  if (cfg_->lb_timer_step < 1) {
    cfg_->lb_timer_step = 1;
  } else if (cfg_->lb_timer_step > 100) {
    cfg_->lb_timer_step = 100;
  }

  if (cfg_->lb_timer_always_on_enabled != 1) {
    cfg_->lb_timer_always_on_enabled = 0;
  }

  if (cfg_->lb_timer_start_value < 1) {
    cfg_->lb_timer_start_value = 1;
  } else if (cfg_->lb_timer_start_value > 100) {
    cfg_->lb_timer_start_value = 100;
  }
}

}  // namespace hap
}  // namespace shelly
