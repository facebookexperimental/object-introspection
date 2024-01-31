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

constexpr int oidMagicId = 0x01DE8;

#include <array>

namespace {

template <typename T, size_t N>
constexpr std::array<T, N + 1> arrayPrepend(std::array<T, N> a, T t);
template <typename T, size_t N, size_t... I>
constexpr std::array<T, N + 1> arrayPrependHelper(std::array<T, N> a,
                                                  T t,
                                                  std::index_sequence<I...>);

template <size_t Size = (1 << 20) / sizeof(uintptr_t)>
class PointerHashSet {
 private:
  // 1 MiB of pointers
  std::array<uintptr_t, Size> data;
  size_t numEntries;

  /*
   * twang_mix64 hash function, taken from Folly where it is used as the
   * default hash function for 64-bit integers.
   */
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
    numEntries = 0;
  }

  /*
   * Adds the pointer to the set.
   * Returns `true` if the value was newly added. `false` may be returned if
   * the value was already present, a null pointer was passed or if there are
   * no entries left in the array.
   */
  bool add(uintptr_t pointer) noexcept {
    if (pointer == 0) {
      return false;
    }

    uint64_t index = twang_mix64(pointer) % data.size();
    while (true) {
      uintptr_t entry = data[index];

      if (entry == 0) {
        data[index] = pointer;
        ++numEntries;
        return true;
      }

      if (entry == pointer || numEntries >= data.size()) {
        return false;
      }

      index = (index + 1) % data.size();
    }
  }

  size_t size(void) {
    return numEntries;
  }

  size_t capacity(void) {
    return data.size();
  }

  bool add(const auto* p) {
    return add((uintptr_t)p);
  }
};

}  // namespace

// alignas(0) is ignored according to docs so can be default
template <unsigned int N, unsigned int align = 0, int32_t Id = 0>
struct alignas(align) DummySizedOperator {
  char c[N];
};

// The empty class specialization is, unfortunately, necessary. When this
// operator is passed as a template parameter to something like unordered_map,
// even though an empty class and a class with a single character have size one,
// there is some empty class optimization that changes the static size of the
// container if an empty class is passed.
template <int32_t Id>
struct DummySizedOperator<0, 0, Id> {};
template <int32_t Id>
struct DummySizedOperator<0, 1, Id> {};

template <template <typename, size_t, size_t, int32_t> typename DerivedT,
          typename T,
          size_t N,
          size_t Align,
          int32_t Id>
struct DummyAllocatorBase {
  using value_type = T;
  T* allocate(std::size_t n) {
    return nullptr;
  }
  void deallocate(T* p, std::size_t n) noexcept {
  }

  template <typename U>
  struct rebind {
    using other = DerivedT<U, N, Align, Id>;
  };
};

template <typename T, size_t N, size_t Align = 0, int32_t Id = 0>
struct alignas(Align) DummyAllocator
    : DummyAllocatorBase<DummyAllocator, T, N, Align, Id> {
  char c[N];
};

template <typename T, int32_t Id>
struct DummyAllocator<T, 0, 0, Id>
    : DummyAllocatorBase<DummyAllocator, T, 0, 0, Id> {};
template <typename T, int32_t Id>
struct DummyAllocator<T, 0, 1, Id>
    : DummyAllocatorBase<DummyAllocator, T, 0, 1, Id> {};

template <typename Type, size_t ExpectedSize, size_t ActualSize>
struct validate_size_eq {
  static constexpr bool value = true;
  static_assert(ExpectedSize == ActualSize);
};

template <typename Type, size_t ExpectedSize>
struct validate_size : validate_size_eq<Type, ExpectedSize, sizeof(Type)> {};

template <size_t ExpectedOffset, size_t ActualOffset>
struct validate_offset {
  static constexpr bool value = true;
  static_assert(ExpectedOffset == ActualOffset);
};

enum class StubbedPointer : uintptr_t {};

bool isStorageInline(const auto& c) {
  uintptr_t data_p = (uintptr_t)std::data(c);
  uintptr_t container_p = (uintptr_t)&c;

  return data_p >= container_p && data_p < container_p + sizeof(c);
}

namespace {

template <typename T, size_t N, size_t... I>
constexpr std::array<T, N + 1> arrayPrependHelper(std::array<T, N> a,
                                                  T t,
                                                  std::index_sequence<I...>) {
  return {t, a[I]...};
}

template <typename T, size_t N>
constexpr std::array<T, N + 1> arrayPrepend(std::array<T, N> a, T t) {
  return arrayPrependHelper(a, t, std::make_index_sequence<N>());
}

}  // namespace

namespace OIInternal {
namespace {

template <typename T>
struct Incomplete;

template <typename T>
constexpr bool oi_is_complete = true;
template <>
constexpr bool oi_is_complete<void> = false;
template <typename T>
constexpr bool oi_is_complete<Incomplete<T>> = false;

}  // namespace
}  // namespace OIInternal
