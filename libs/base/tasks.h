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

#ifndef LIBS_BASE_TASKS_H_
#define LIBS_BASE_TASKS_H_

#include "FreeRTOSConfig.h"
#include "third_party/nxp/rt1176-sdk/devices/MIMXRT1176/fsl_device_registers.h"

namespace coralmicro {

template <int N>
struct TaskPriorityImpl {
  static constexpr auto value = N;
  static_assert(0 <= value && value < configMAX_PRIORITIES,
                "Invalid value for task priority");
};

template <int N>
inline constexpr int TaskPriority = TaskPriorityImpl<N>::value;

// Absolute, not relative to the ceiling. Level 4 is left empty for an
// application task that must outrank every driver; the relative form used to
// put the console and the USB stack there and would drag them back up with
// any change to configMAX_PRIORITIES.
//
// Bands and their order are otherwise unchanged: drivers at 3, application
// and the Edge TPU at 2.
#if (__CORTEX_M == 7)
enum {
  kIpcTaskPriority = TaskPriority<3>,
  kConsoleTaskPriority = TaskPriority<2>,
  kAppTaskPriority = TaskPriority<2>,
  kUsbDeviceTaskPriority = TaskPriority<3>,
  kUsbHostTaskPriority = TaskPriority<3>,
  kEdgeTpuDfuTaskPriority = TaskPriority<2>,
  kEdgeTpuTaskPriority = TaskPriority<2>,
  kRandomTaskPriority = TaskPriority<3>,
  kPmicTaskPriority = TaskPriority<3>,
  kCameraTaskPriority = TaskPriority<3>,
  kAudioTaskPriority = TaskPriority<3>,
};
#elif (__CORTEX_M == 4)
enum {
  kIpcTaskPriority = TaskPriority<configMAX_PRIORITIES - 1>,
  kConsoleTaskPriority = TaskPriority<configMAX_PRIORITIES - 1>,
  kAppTaskPriority = TaskPriority<configMAX_PRIORITIES - 1>,
  kCameraTaskPriority = TaskPriority<configMAX_PRIORITIES - 1>,
  kPmicTaskPriority = TaskPriority<configMAX_PRIORITIES - 1>,
};
#else
#error "__CORTEX_M not defined"
#endif

}  // namespace coralmicro

#endif  // LIBS_BASE_TASKS_H_
