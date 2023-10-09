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
#pragma once

#include <bitset>

template <typename T, size_t N>
class EnumBitset {
 private:
  using BitsetType = std::bitset<N>;

 public:
  EnumBitset() = default;
  EnumBitset(std::initializer_list<T> values) {
    for (auto v : values) {
      (*this)[v] = true;
    }
  }

  constexpr bool operator[](T v) const {
    return bitset[static_cast<size_t>(v)];
  }
  typename BitsetType::reference operator[](T v) {
    return bitset[static_cast<size_t>(v)];
  }

  bool all() const noexcept {
    return bitset.all();
  }
  bool any() const noexcept {
    return bitset.any();
  }
  bool none() const noexcept {
    return bitset.none();
  }

  bool operator==(const EnumBitset<T, N>& that) const {
    return bitset == that.bitset;
  }
  EnumBitset<T, N>& operator|=(const EnumBitset<T, N>& that) {
    bitset |= that.bitset;
    return *this;
  }
  EnumBitset<T, N>& operator&=(const EnumBitset<T, N>& that) {
    bitset &= that.bitset;
    return *this;
  }

 private:
  BitsetType bitset;
};

template <typename T, size_t N>
EnumBitset<T, N> operator&(const EnumBitset<T, N>& lhs,
                           const EnumBitset<T, N>& rhs) {
  auto out = lhs;
  out &= rhs;
  return out;
}
