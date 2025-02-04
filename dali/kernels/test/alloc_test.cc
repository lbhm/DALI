// Copyright (c) 2019, 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <cuda_runtime.h>
#include <gtest/gtest.h>
#include <cstring>
#include "dali/kernels/alloc.h"
#include "dali/core/cuda_error.h"

namespace dali {
namespace kernels {

TEST(KernelAlloc, AllocFree) {
  size_t size = 1<<20;  // 1 MiB
  const char *names[static_cast<int>(AllocType::Count)] = { "Host", "Pinned", "GPU", "Unified" };
  for (int i = 0; i < static_cast<int>(AllocType::Count); i++) {
    try {
      AllocType alloc = static_cast<AllocType>(i);
      void *mem = memory::Allocate(alloc, size);
      EXPECT_EQ(cudaGetLastError(), 0) << "Error when allocating for " << names[i];
      // use ThrowMemoryError to call cudaGetLastError from the kernel libray
      // (and use CUDA rt status from there)
      if (!mem) {
        memory::ThrowMemoryError(alloc, size);
      }
      memory::GetDeleter(alloc)(mem);
      EXPECT_EQ(cudaGetLastError(), 0) << "Error when freeing for " << names[i];
    } catch (const CUDAError &e) {
      // skip this case if Unified is not supported
      bool not_supported = (e.is_drv_api() && e.drv_error() == CUDA_ERROR_NOT_SUPPORTED) ||
                           (e.is_rt_api() && e.rt_error() == cudaErrorNotSupported);
      EXPECT_TRUE(not_supported) << "Unexpected CUDA exception: " << e.what();
    }
  }
}

TEST(KernelAlloc, HostDevice) {
  (void)cudaGetLastError();
  size_t size = 1<<20;  // 1 MiB
  void *pinned = memory::Allocate(AllocType::Pinned, size);
  void *plain = memory::Allocate(AllocType::Host, size);
  void *gpu = memory::Allocate(AllocType::GPU, size);

  ASSERT_NE(pinned, nullptr);
  ASSERT_NE(plain, nullptr);
  ASSERT_NE(gpu, nullptr);

  int *data = reinterpret_cast<int*>(pinned);
  for (size_t i = 0; i < size/sizeof(int); i++)
    data[i] = i*i + 5;
  std::memset(plain, 0, size);

  EXPECT_EQ(cudaMemcpy(gpu, pinned, size, cudaMemcpyHostToDevice), 0);
  EXPECT_EQ(cudaMemcpy(plain, gpu, size, cudaMemcpyDeviceToHost), 0);

  EXPECT_EQ(std::memcmp(plain, pinned, size), 0);

  auto pinned_deallocator = memory::GetDeleter(AllocType::Pinned);
  auto host_deallocator = memory::GetDeleter(AllocType::Host);
  auto gpu_deallocator = memory::GetDeleter(AllocType::GPU);

  int count = 1;
  cudaGetDeviceCount(&count);
  if (count > 1)
    CUDA_CALL(cudaSetDevice(1));
  EXPECT_EQ(cudaGetLastError(), 0);
  pinned_deallocator(pinned);
  host_deallocator(plain);
  gpu_deallocator(gpu);
  EXPECT_EQ(cudaGetLastError(), 0);
  CUDA_CALL(cudaSetDevice(0));
}

TEST(KernelAlloc, Unique) {
  size_t size = 1<<20;  // 1 M
  const char *names[static_cast<int>(AllocType::Count)] = { "Host", "Pinned", "GPU", "Unified" };
  for (int i = 0; i < static_cast<int>(AllocType::Count); i++) {
    try {
      AllocType alloc = static_cast<AllocType>(i);
      auto ptr = memory::alloc_unique<float>(alloc, size);
      EXPECT_EQ(cudaGetLastError(), 0) << "Error when allocating for " << names[i];
      ASSERT_NE(ptr, nullptr);
      ptr.reset();
      EXPECT_EQ(cudaGetLastError(), 0) << "Error when freeing for " << names[i];
    } catch (const CUDAError &e) {
      // skip this case if Unified is not supported
      bool not_supported = (e.is_drv_api() && e.drv_error() == CUDA_ERROR_NOT_SUPPORTED) ||
                           (e.is_rt_api() && e.rt_error() == cudaErrorNotSupported);
      EXPECT_TRUE(not_supported) << "Unexpected CUDA exception: " << e.what();
    }
  }
}

TEST(KernelAlloc, Shared) {
  size_t size = 1<<20;  // 1 M
  const char *names[static_cast<int>(AllocType::Count)] = { "Host", "Pinned", "GPU", "Unified" };
  for (int i = 0; i < static_cast<int>(AllocType::Count); i++) {
    try {
      AllocType alloc = static_cast<AllocType>(i);
      std::shared_ptr<float> ptr = memory::alloc_shared<float>(alloc, size);
      EXPECT_EQ(cudaGetLastError(), 0) << "Error when allocating for " << names[i];
      std::shared_ptr<float> ptr2 = ptr;
      ASSERT_NE(ptr, nullptr);
      ASSERT_NE(ptr2, nullptr);
      ptr.reset();
      ptr2.reset();
      EXPECT_EQ(cudaGetLastError(), 0) << "Error when freeing for " << names[i];
    } catch (const CUDAError &e) {
      // skip this case if Unified is not supported
      bool not_supported = (e.is_drv_api() && e.drv_error() == CUDA_ERROR_NOT_SUPPORTED) ||
                           (e.is_rt_api() && e.rt_error() == cudaErrorNotSupported);
      EXPECT_TRUE(not_supported) << "Unexpected CUDA exception: " << e.what();
    }
  }
}

TEST(KernelAllocFail, Host) {
  (void)cudaGetLastError();
  size_t size = -1_uz;
  EXPECT_THROW(memory::alloc_unique<uint8_t>(AllocType::Host, size), std::bad_alloc);
  EXPECT_THROW(memory::alloc_shared<uint8_t>(AllocType::Host, size), std::bad_alloc);
  EXPECT_EQ(memory::alloc_unique<uint8_t>(AllocType::Host, 0),
            kernels::memory::KernelUniquePtr<uint8_t>{});
  EXPECT_EQ(memory::alloc_shared<uint8_t>(AllocType::Host, 0),
            std::shared_ptr<uint8_t>{});
}

TEST(KernelAllocFail, Pinned) {
  (void)cudaGetLastError();
  size_t size = -1_uz;
  EXPECT_THROW(memory::alloc_unique<uint8_t>(AllocType::Pinned, size), CUDABadAlloc);
  EXPECT_THROW(memory::alloc_shared<uint8_t>(AllocType::Pinned, size), CUDABadAlloc);
  EXPECT_EQ(memory::alloc_unique<uint8_t>(AllocType::Pinned, 0),
            kernels::memory::KernelUniquePtr<uint8_t>{});
  EXPECT_EQ(memory::alloc_shared<uint8_t>(AllocType::Pinned, 0),
            std::shared_ptr<uint8_t>{});
}

TEST(KernelAllocFail, GPU) {
  (void)cudaGetLastError();
  size_t size = -1_uz;
  EXPECT_THROW(memory::alloc_unique<uint8_t>(AllocType::GPU, size), CUDABadAlloc);
  EXPECT_THROW(memory::alloc_shared<uint8_t>(AllocType::GPU, size), CUDABadAlloc);
  EXPECT_EQ(memory::alloc_unique<uint8_t>(AllocType::GPU, 0),
            kernels::memory::KernelUniquePtr<uint8_t>{});
  EXPECT_EQ(memory::alloc_shared<uint8_t>(AllocType::GPU, 0),
            std::shared_ptr<uint8_t>{});
}

}  // namespace kernels
}  // namespace dali
