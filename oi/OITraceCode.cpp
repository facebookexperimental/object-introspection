/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#define NDEBUG 1
// Required for compatibility with new glibc headers
#define __malloc__(x, y) __malloc__
#if !__has_builtin(__builtin_free)
#define __builtin_free(x) free(x)
#endif
#pragma clang diagnostic ignored "-Wunknown-attributes"

// clang-format off
// The header xmmintrin.h must come first. Otherwise it results in errors
// jemalloc during JIT compilation
#include <xmmintrin.h>
#include <cstdint>

#include <utility>
#include <unistd.h>

// clang-format on

#define C10_USING_CUSTOM_GENERATED_MACROS

// These globals are set by oid, see end of OIDebugger::compileCode()
extern uint8_t* dataBase;
extern size_t dataSize;
extern uintptr_t cookieValue;
extern int logFile;

constexpr int oidMagicId = 0x01DE8;

#include <array>

namespace {

class {
 private:
  // 1 MiB of pointers
  std::array<uintptr_t, (1 << 20) / sizeof(uintptr_t)> data;

  // twang_mix64 hash function, taken from Folly where it is used
  // as the default hash function for 64-bit integers
  constexpr static uint64_t twang_mix64(uint64_t key) noexcept {
    key = (~key) + (key << 21);  // key *= (1 << 21) - 1; key -= 1;
    key = key ^ (key >> 24);
    key = key + (key << 3) + (key << 8);  // key *= 1 + (1 << 3) + (1 << 8)
    key = key ^ (key >> 14);
    key = key + (key << 2) + (key << 4);  // key *= 1 + (1 << 2) + (1 << 4)
    key = key ^ (key >> 28);
    key = key + (key << 31);  // key *= 1 + (1 << 31)
    return key;
  }

 public:
  void initialize() noexcept {
    data.fill(0);
  }

  // Adds the pointer to the set.
  // Returns `true` if the value was newly added,
  // or `false` if the value was already present.
  bool add(uintptr_t pointer) noexcept {
    __builtin_assume(pointer > 0);
    uint64_t index = twang_mix64(pointer) % data.size();
    while (true) {
      uintptr_t entry = data[index];
      if (entry == 0) {
        data[index] = pointer;
        return true;
      }
      if (entry == pointer) {
        return false;
      }
      index = (index + 1) % data.size();
    }
  }
} static pointers;

void __jlogptr(uintptr_t ptr) {
  static constexpr char hexdigits[] = "0123456789abcdef";
  static constexpr size_t ptrlen = 2 * sizeof(ptr);

  static char hexstr[ptrlen + 1] = {};

  size_t i = ptrlen;
  while (i--) {
    hexstr[i] = hexdigits[ptr & 0xf];
    ptr = ptr >> 4;
  }
  hexstr[ptrlen] = '\n';
  write(logFile, hexstr, sizeof(hexstr));
}

}  // namespace

// alignas(0) is ignored according to docs so can be default
template <unsigned int N, unsigned int align = 0>
struct alignas(align) DummySizedOperator {
  char c[N];
};

// The empty class specialization is, unfortunately, necessary. When this
// operator is passed as a template parameter to something like unordered_map,
// even though an empty class and a class with a single character have size one,
// there is some empty class optimization that changes the static size of the
// container if an empty class is passed.

// DummySizedOperator<0,0> also collapses to this
template <>
struct DummySizedOperator<0> {};

template <template <typename, size_t, size_t> typename DerivedT,
          typename T,
          size_t N,
          size_t Align>
struct DummyAllocatorBase {
  using value_type = T;
  T* allocate(std::size_t n) {
    return nullptr;
  }
  void deallocate(T* p, std::size_t n) noexcept {
  }

  template <typename U>
  struct rebind {
    using other = DerivedT<U, N, Align>;
  };
};

template <typename T, size_t N, size_t Align = 0>
struct alignas(Align) DummyAllocator
    : DummyAllocatorBase<DummyAllocator, T, N, Align> {
  char c[N];
};

template <typename T>
struct DummyAllocator<T, 0> : DummyAllocatorBase<DummyAllocator, T, 0, 0> {};

template <typename Type, size_t ExpectedSize, size_t ActualSize = 0>
struct validate_size : std::true_type {
  static_assert(ExpectedSize == ActualSize);
};

template <typename Type, size_t ExpectedSize>
struct validate_size<Type, ExpectedSize>
    : validate_size<Type, ExpectedSize, sizeof(Type)> {};

template <size_t ExpectedOffset, size_t ActualOffset>
struct validate_offset : std::true_type {
  static_assert(ExpectedOffset == ActualOffset);
};
