/*
 * Copyright (c) 2020-2021, NVIDIA CORPORATION.
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

#include "mr_test.hpp"

#include <gtest/gtest.h>

#include <rmm/cuda_stream.hpp>
#include <rmm/mr/device/arena_memory_resource.hpp>
#include <rmm/mr/device/cuda_memory_resource.hpp>
#include <rmm/mr/device/device_memory_resource.hpp>
#include <rmm/mr/device/per_device_resource.hpp>
#include <rmm/mr/device/pool_memory_resource.hpp>

#include <thread>
#include <vector>

namespace rmm::test {
namespace {

struct mr_test_mt : public mr_test {};

INSTANTIATE_TEST_CASE_P(MultiThreadResourceTests,
                        mr_test_mt,
                        ::testing::Values(mr_factory{"CUDA", &make_cuda},
#ifdef RMM_CUDA_MALLOC_ASYNC_SUPPORT
                                          mr_factory{"CUDA_Async", &make_cuda_async},
#endif
                                          mr_factory{"Managed", &make_managed},
                                          mr_factory{"Pool", &make_pool},
                                          mr_factory{"Arena", &make_arena},
                                          mr_factory{"Binning", &make_binning}),
                        [](auto const& info) { return info.param.name; });

template <typename Task, typename... Arguments>
void spawn_n(std::size_t num_threads, Task task, Arguments&&... args)
{
  std::vector<std::thread> threads;
  threads.reserve(num_threads);
  for (std::size_t i = 0; i < num_threads; ++i) {
    threads.emplace_back(std::thread(task, std::forward<Arguments>(args)...));
  }

  for (auto& thread : threads) {
    thread.join();
  }
}

template <typename Task, typename... Arguments>
void spawn(Task task, Arguments&&... args)
{
  spawn_n(4, task, std::forward<Arguments>(args)...);
}

TEST(DefaultTest, UseCurrentDeviceResource_mt) { spawn(test_get_current_device_resource); }

TEST(DefaultTest, CurrentDeviceResourceIsCUDA_mt)
{
  spawn([]() {
    EXPECT_NE(nullptr, rmm::mr::get_current_device_resource());
    EXPECT_TRUE(rmm::mr::get_current_device_resource()->is_equal(rmm::mr::cuda_memory_resource{}));
  });
}

TEST(DefaultTest, GetCurrentDeviceResource_mt)
{
  spawn([]() {
    rmm::mr::device_memory_resource* mr = rmm::mr::get_current_device_resource();
    EXPECT_NE(nullptr, mr);
    EXPECT_TRUE(mr->is_equal(rmm::mr::cuda_memory_resource{}));
  });
}

TEST_P(mr_test_mt, SetCurrentDeviceResource_mt)
{
  // single thread changes default resource, then multiple threads use it

  rmm::mr::device_memory_resource* old = rmm::mr::set_current_device_resource(this->mr.get());
  EXPECT_NE(nullptr, old);

  spawn([mr = this->mr.get()]() {
    EXPECT_EQ(mr, rmm::mr::get_current_device_resource());
    test_get_current_device_resource();  // test allocating with the new default resource
  });

  // setting default resource w/ nullptr should reset to initial
  rmm::mr::set_current_device_resource(nullptr);
  EXPECT_TRUE(old->is_equal(*rmm::mr::get_current_device_resource()));
}

TEST_P(mr_test_mt, SetCurrentDeviceResourcePerThread_mt)
{
  int num_devices{};
  RMM_CUDA_TRY(cudaGetDeviceCount(&num_devices));

  std::vector<std::thread> threads;
  threads.reserve(num_devices);
  for (int i = 0; i < num_devices; ++i) {
    threads.emplace_back(std::thread{[mr = this->mr.get()](auto dev_id) {
                                       RMM_CUDA_TRY(cudaSetDevice(dev_id));
                                       rmm::mr::device_memory_resource* old =
                                         rmm::mr::set_current_device_resource(mr);
                                       EXPECT_NE(nullptr, old);
                                       // initial resource for this device should be CUDA mr
                                       EXPECT_TRUE(old->is_equal(rmm::mr::cuda_memory_resource{}));
                                       // get_current_device_resource should equal the resource we
                                       // just set
                                       EXPECT_EQ(mr, rmm::mr::get_current_device_resource());
                                       // Setting current dev resource to nullptr should reset to
                                       // cuda MR and return the MR we previously set
                                       old = rmm::mr::set_current_device_resource(nullptr);
                                       EXPECT_NE(nullptr, old);
                                       EXPECT_EQ(old, mr);
                                       EXPECT_TRUE(rmm::mr::get_current_device_resource()->is_equal(
                                         rmm::mr::cuda_memory_resource{}));
                                     },
                                     i});
  }

  for (auto& thread : threads) {
    thread.join();
  }
}

TEST_P(mr_test_mt, AllocateDefaultStream)
{
  spawn(test_various_allocations, this->mr.get(), rmm::cuda_stream_view{});
}

TEST_P(mr_test_mt, AllocateOnStream)
{
  spawn(test_various_allocations, this->mr.get(), this->stream.view());
}

TEST_P(mr_test_mt, RandomAllocationsDefaultStream)
{
  spawn(test_random_allocations,
        this->mr.get(),
        default_num_allocations,
        default_max_size,
        rmm::cuda_stream_view{});
}

TEST_P(mr_test_mt, RandomAllocationsStream)
{
  spawn(test_random_allocations,
        this->mr.get(),
        default_num_allocations,
        default_max_size,
        this->stream.view());
}

TEST_P(mr_test_mt, MixedRandomAllocationFreeDefaultStream)
{
  spawn(
    test_mixed_random_allocation_free, this->mr.get(), default_max_size, rmm::cuda_stream_view{});
}

TEST_P(mr_test_mt, MixedRandomAllocationFreeStream)
{
  spawn(test_mixed_random_allocation_free, this->mr.get(), default_max_size, this->stream.view());
}

void allocate_loop(rmm::mr::device_memory_resource* mr,
                   std::size_t num_allocations,
                   std::list<allocation>& allocations,
                   std::mutex& mtx,
                   std::condition_variable& allocations_ready,
                   cudaEvent_t& event,
                   rmm::cuda_stream_view stream)
{
  constexpr std::size_t max_size{1_MiB};

  std::default_random_engine generator;
  std::uniform_int_distribution<std::size_t> size_distribution(1, max_size);

  for (std::size_t i = 0; i < num_allocations; ++i) {
    std::size_t size = size_distribution(generator);
    void* ptr        = mr->allocate(size, stream);
    {
      std::lock_guard<std::mutex> lock(mtx);
      RMM_CUDA_TRY(cudaEventRecord(event, stream.value()));
      allocations.emplace_back(ptr, size);
    }
    allocations_ready.notify_one();
  }
  // Work around for threads going away before cudaEvent has finished async processing
  cudaEventSynchronize(event);
}

void deallocate_loop(rmm::mr::device_memory_resource* mr,
                     std::size_t num_allocations,
                     std::list<allocation>& allocations,
                     std::mutex& mtx,
                     std::condition_variable& allocations_ready,
                     cudaEvent_t& event,
                     rmm::cuda_stream_view stream)
{
  for (std::size_t i = 0; i < num_allocations; i++) {
    std::unique_lock lock(mtx);
    allocations_ready.wait(lock, [&allocations] { return !allocations.empty(); });
    RMM_CUDA_TRY(cudaStreamWaitEvent(stream.value(), event, 0));
    allocation alloc = allocations.front();
    allocations.pop_front();
    mr->deallocate(alloc.ptr, alloc.size, stream);
  }

  // Work around for threads going away before cudaEvent has finished async processing
  cudaEventSynchronize(event);
}
void test_allocate_free_different_threads(rmm::mr::device_memory_resource* mr,
                                          rmm::cuda_stream_view streamA,
                                          rmm::cuda_stream_view streamB)
{
  constexpr std::size_t num_allocations{100};

  std::mutex mtx;
  std::condition_variable allocations_ready;
  std::list<allocation> allocations;
  cudaEvent_t event;

  RMM_CUDA_TRY(cudaEventCreate(&event));

  std::thread producer(allocate_loop,
                       mr,
                       num_allocations,
                       std::ref(allocations),
                       std::ref(mtx),
                       std::ref(allocations_ready),
                       std::ref(event),
                       streamA);

  std::thread consumer(deallocate_loop,
                       mr,
                       num_allocations,
                       std::ref(allocations),
                       std::ref(mtx),
                       std::ref(allocations_ready),
                       std::ref(event),
                       streamB);

  producer.join();
  consumer.join();

  RMM_CUDA_TRY(cudaEventDestroy(event));
}

TEST_P(mr_test_mt, AllocFreeDifferentThreadsDefaultStream)
{
  test_allocate_free_different_threads(
    this->mr.get(), rmm::cuda_stream_default, rmm::cuda_stream_default);
}

TEST_P(mr_test_mt, AllocFreeDifferentThreadsPerThreadDefaultStream)
{
  test_allocate_free_different_threads(
    this->mr.get(), rmm::cuda_stream_per_thread, rmm::cuda_stream_per_thread);
}

TEST_P(mr_test_mt, AllocFreeDifferentThreadsSameStream)
{
  test_allocate_free_different_threads(this->mr.get(), this->stream, this->stream);
}

TEST_P(mr_test_mt, AllocFreeDifferentThreadsDifferentStream)
{
  rmm::cuda_stream streamB;
  test_allocate_free_different_threads(this->mr.get(), this->stream, streamB);
  streamB.synchronize();
}

}  // namespace
}  // namespace rmm::test
