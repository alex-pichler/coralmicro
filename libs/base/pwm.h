/*
 * Copyright 2022 Google LLC
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

#ifndef LIBS_BASE_PWM_H_
#define LIBS_BASE_PWM_H_

#include <optional>
#include <vector>

#include "third_party/nxp/rt1176-sdk/devices/MIMXRT1176/drivers/fsl_pwm.h"

namespace coralmicro {

// PWM-capable pins on the left-side header (J9).
//
// Note: These pins output a max of 1.8V.
//
// k9 and k10 are the two pins the board documents as PWM_B and PWM_A. k7 and
// k8 are the UART6 CTS/RTS pins, which the i.MX RT1176 can also mux to
// FlexPWM3 submodule 0; they are usable as PWM whenever UART6 flow control is
// not. Each pair belongs to a different FlexPWM instance, so the two pairs
// have independent periods and must be enabled with separate PwmEnable()
// calls.
//
// The only other PWM-capable header pins are J9 pin 3 (GPIO_AD_06,
// FLEXPWM1_PWM0_X) and J9 pin 4 (GPIO_AD_07, FLEXPWM1_PWM1_X). They are not
// listed here because the X channel cannot be driven through
// PWM_SetupPwm(): that function only distinguishes PWM_A from "not PWM_A",
// so an X channel is programmed into the B value registers and the B output
// enable. Supporting them needs direct register programming.
enum class PwmPin {
  k9,
  k10,
  k7,
  k8,
};

// Represents a PWM Pin's HW setting.
struct PwmPinSetting {
  // Pointer to the base register of the PWM module.
  PWM_Type* base;
  // The PWM submodule of this pin.
  pwm_submodule_t sub_module;
  // The channel of this pin.
  pwm_channels_t pwm_channel;
};

// Represents the configuration for a single PWM pin.
struct PwmPinConfig {
  // The duty cycle (from 0-100) of the PWM waveform.
  int duty_cycle;
  // Frequency in hz.
  uint32_t frequency;
  // The HW setting of the pin to start duty cycle.
  PwmPinSetting pin_setting;
};

// Gets the HW settings for a pwm pin.
//
// @param pin The pin to get settings for.
// @return A setting for the pin or a std::nullopt.
PwmPinSetting PwmPinSettingFor(PwmPin pin);

// Initializes the PWM module.
void PwmInit();

// Enables a PWM sub_module with some pins configs.
//
// @param pin_configs The list of pin configurations to enable.
void PwmEnable(const std::vector<PwmPinConfig>& pin_configs);

// Disables a PWM sub_module.
//
// @param pin_configs The list of pin configurations to disable.
void PwmDisable(const std::vector<PwmPinConfig>& pin_configs);

}  // namespace coralmicro

#endif  // LIBS_BASE_PWM_H_
