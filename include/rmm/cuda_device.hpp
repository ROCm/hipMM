/*
 * Copyright (c) 2021, NVIDIA CORPORATION.
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
// MIT License
//
// Modifications Copyright (C) 2023 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
#pragma once

#include <rmm/detail/error.hpp>

#include <rmm/cuda_runtime_api.h>

namespace rmm {

/**
 * @addtogroup cuda_device_management
 * @{
 * @file
 */
/**
 * @brief Strong type for a CUDA device identifier.
 *
 */
struct cuda_device_id {
  using value_type = int;  ///< Integer type used for device identifier

  /**
   * @brief Construct a `cuda_device_id` from the specified integer value
   *
   * @param dev_id The device's integer identifier
   */
  explicit constexpr cuda_device_id(value_type dev_id) noexcept : id_{dev_id} {}

  /// @briefreturn{The wrapped integer value}
  [[nodiscard]] constexpr value_type value() const noexcept { return id_; }

 private:
  value_type id_;
};

/**
 * @brief Returns a `cuda_device_id` for the current device
 *
 * The current device is the device on which the calling thread executes device code.
 *
 * @return `cuda_device_id` for the current device
 */
inline cuda_device_id get_current_cuda_device()
{
  cuda_device_id::value_type dev_id{-1};
  RMM_ASSERT_CUDA_SUCCESS(cudaGetDevice(&dev_id));
  return cuda_device_id{dev_id};
}

/**
 * @brief Returns the number of CUDA devices in the system
 *
 * @return Number of CUDA devices in the system
 */
inline int get_num_cuda_devices()
{
  cuda_device_id::value_type num_dev{-1};
  RMM_ASSERT_CUDA_SUCCESS(cudaGetDeviceCount(&num_dev));
  return num_dev;
}

/**
 * @brief RAII class that sets the current CUDA device to the specified device on construction
 * and restores the previous device on destruction.
 */
struct cuda_set_device_raii {
  /**
   * @brief Construct a new cuda_set_device_raii object and sets the current CUDA device to `dev_id`
   *
   * @param dev_id The device to set as the current CUDA device
   */
  explicit cuda_set_device_raii(cuda_device_id dev_id)
    : old_device_{get_current_cuda_device()}, needs_reset_{old_device_.value() != dev_id.value()}
  {
    if (needs_reset_) RMM_ASSERT_CUDA_SUCCESS(cudaSetDevice(dev_id.value()));
  }
  /**
   * @brief Reactivates the previous CUDA device
   */
  ~cuda_set_device_raii() noexcept
  {
    if (needs_reset_) RMM_ASSERT_CUDA_SUCCESS(cudaSetDevice(old_device_.value()));
  }

  cuda_set_device_raii(cuda_set_device_raii const&)            = delete;
  cuda_set_device_raii& operator=(cuda_set_device_raii const&) = delete;
  cuda_set_device_raii(cuda_set_device_raii&&)                 = delete;
  cuda_set_device_raii& operator=(cuda_set_device_raii&&)      = delete;

 private:
  cuda_device_id old_device_;
  bool needs_reset_;
};

/** @} */  // end of group
}  // namespace rmm
