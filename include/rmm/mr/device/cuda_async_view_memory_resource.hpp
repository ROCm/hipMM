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

#include <rmm/cuda_device.hpp>
#include <rmm/cuda_stream_view.hpp>
#include <rmm/detail/cuda_util.hpp>
#include <rmm/detail/dynamic_load_runtime.hpp>
#include <rmm/detail/error.hpp>
#include <rmm/mr/device/device_memory_resource.hpp>

#include <rmm/detail/thrust_namespace.h>
#include <thrust/optional.h>

#include <rmm/cuda_runtime_api.h>

#include <cstddef>
#include <limits>

namespace rmm::mr {
/**
 * @addtogroup device_memory_resources
 * @{
 * @file
 */

/**
 * @brief `device_memory_resource` derived class that uses `cudaMallocAsync`/`cudaFreeAsync` for
 * allocation/deallocation.
 */
class cuda_async_view_memory_resource final : public device_memory_resource {
 public:
#ifdef RMM_CUDA_MALLOC_ASYNC_SUPPORT
  /**
   * @brief Constructs a cuda_async_view_memory_resource which uses an existing CUDA memory pool.
   * The provided pool is not owned by cuda_async_view_memory_resource and must remain valid
   * during the lifetime of the memory resource.
   *
   * @throws rmm::runtime_error if the CUDA version does not support `cudaMallocAsync`
   *
   * @param valid_pool_handle Handle to a CUDA memory pool which will be used to
   * serve allocation requests.
   */
  cuda_async_view_memory_resource(cudaMemPool_t valid_pool_handle)
    : cuda_pool_handle_{[valid_pool_handle]() {
        RMM_EXPECTS(nullptr != valid_pool_handle, "Unexpected null pool handle.");
        return valid_pool_handle;
      }()}
  {
#  if ! ( defined(__HIP_PLATFORM_AMD__) || defined(__HIP_PLATFORM_HCC__) ) //: HIP/AMD: RMM_CUDA_MALLOC_ASYNC_SUPPORT implies the support of pools
    // Check if cudaMallocAsync Memory pool supported
    auto const device = rmm::get_current_cuda_device();
    int cuda_pool_supported{};
    auto result =
      cudaDeviceGetAttribute(&cuda_pool_supported, cudaDevAttrMemoryPoolsSupported, device.value());
    RMM_EXPECTS(result == cudaSuccess && cuda_pool_supported,
                "cudaMallocAsync not supported with this CUDA driver/runtime version");
#  endif
  }
#endif

#ifdef RMM_CUDA_MALLOC_ASYNC_SUPPORT
  /**
   * @brief Returns the underlying native handle to the CUDA pool
   *
   */
  [[nodiscard]] cudaMemPool_t pool_handle() const noexcept { return cuda_pool_handle_; }
#endif

  cuda_async_view_memory_resource() = default;
  cuda_async_view_memory_resource(cuda_async_view_memory_resource const&) =
    default;  ///< @default_copy_constructor
  cuda_async_view_memory_resource(cuda_async_view_memory_resource&&) =
    default;  ///< @default_move_constructor
  cuda_async_view_memory_resource& operator=(cuda_async_view_memory_resource const&) =
    default;  ///< @default_copy_assignment{cuda_async_view_memory_resource}
  cuda_async_view_memory_resource& operator=(cuda_async_view_memory_resource&&) =
    default;  ///< @default_move_assignment{cuda_async_view_memory_resource}

  /**
   * @brief Query whether the resource supports use of non-null CUDA streams for
   * allocation/deallocation. `cuda_memory_resource` does not support streams.
   *
   * @returns bool true
   */
  [[nodiscard]] bool supports_streams() const noexcept override { return true; }

  /**
   * @brief Query whether the resource supports the get_mem_info API.
   *
   * @return true
   */
  [[nodiscard]] bool supports_get_mem_info() const noexcept override { return false; }

 private:
#ifdef RMM_CUDA_MALLOC_ASYNC_SUPPORT
  cudaMemPool_t cuda_pool_handle_{};
#endif

  /**
   * @brief Allocates memory of size at least \p bytes.
   *
   * The returned pointer will have at minimum 256 byte alignment.
   *
   * @param bytes The size of the allocation
   * @param stream Stream on which to perform allocation
   * @return void* Pointer to the newly allocated memory
   */
  void* do_allocate(std::size_t bytes, rmm::cuda_stream_view stream) override
  {
    void* ptr{nullptr};
#ifdef RMM_CUDA_MALLOC_ASYNC_SUPPORT
    if (bytes > 0) {
      RMM_CUDA_TRY_ALLOC(rmm::detail::async_alloc::cudaMallocFromPoolAsync(
        &ptr, bytes, pool_handle(), stream.value()));
    }
#else
    (void)bytes;
    (void)stream;
#endif
    return ptr;
  }

  /**
   * @brief Deallocate memory pointed to by \p p.
   *
   * @param ptr Pointer to be deallocated
   * @param bytes The size in bytes of the allocation. This must be equal to the
   * value of `bytes` that was passed to the `allocate` call that returned `p`.
   * @param stream Stream on which to perform deallocation
   */
  void do_deallocate(void* ptr,
                     [[maybe_unused]] std::size_t bytes,
                     rmm::cuda_stream_view stream) override
  {
#ifdef RMM_CUDA_MALLOC_ASYNC_SUPPORT
    if (ptr != nullptr) {
      RMM_ASSERT_CUDA_SUCCESS(rmm::detail::async_alloc::cudaFreeAsync(ptr, stream.value()));
    }
#else
    (void)ptr;
    (void)bytes;
    (void)stream;
#endif
  }

  /**
   * @brief Compare this resource to another.
   *
   * @param other The other resource to compare to
   * @return true If the two resources are equivalent
   * @return false If the two resources are not equal
   */
  [[nodiscard]] bool do_is_equal(device_memory_resource const& other) const noexcept override
  {
    return dynamic_cast<cuda_async_view_memory_resource const*>(&other) != nullptr;
  }

  /**
   * @brief Get free and available memory for memory resource
   *
   * @throws rmm::cuda_error if unable to retrieve memory info.
   *
   * @return std::pair contaiing free_size and total_size of memory
   */
  [[nodiscard]] std::pair<std::size_t, std::size_t> do_get_mem_info(
    rmm::cuda_stream_view) const override
  {
    return std::make_pair(0, 0);
  }
};

/** @} */  // end of group
}  // namespace rmm::mr
