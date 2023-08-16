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
#include <oi/exporters/ParsedData.h>

#include <cassert>
#include <stdexcept>
#include <type_traits>

template <typename T>
constexpr bool always_false_v = false;

namespace oi::exporters {
namespace {
uint64_t parseVarint(std::vector<uint8_t>::const_iterator& it);
}

ParsedData ParsedData::parse(std::vector<uint8_t>::const_iterator& it,
                             types::dy::Dynamic dy) {
  return std::visit(
      [&it](const auto el) -> ParsedData {
        auto ty = el.get();
        using T = std::decay_t<decltype(ty)>;
        if constexpr (std::is_same_v<T, types::dy::Unit>) {
          return ParsedData::Unit{};
        } else if constexpr (std::is_same_v<T, types::dy::VarInt>) {
          return ParsedData::VarInt{.value = parseVarint(it)};
        } else if constexpr (std::is_same_v<T, types::dy::Pair>) {
          return ParsedData::Pair{
              .first = Lazy{it, ty.first},
              .second = Lazy{it, ty.second},
          };
        } else if constexpr (std::is_same_v<T, types::dy::List>) {
          return ParsedData::List{
              .length = parseVarint(it),
              .values = {it, ty.element},
          };
        } else if constexpr (std::is_same_v<T, types::dy::Sum>) {
          auto index = parseVarint(it);
          assert(index < ty.variants.size());
          return ParsedData::Sum{
              .index = parseVarint(it),
              .value = {it, ty.variants[index]},
          };
        } else {
          static_assert(always_false_v<T>, "non-exhaustive visitor!");
        }
      },
      dy);
}

namespace {
uint64_t parseVarint(std::vector<uint8_t>::const_iterator& it) {
  uint64_t v = 0;
  int shift = 0;
  while (*it >= 0x80) {
    v |= static_cast<uint64_t>(*it++ & 0x7f) << shift;
    shift += 7;
  }
  v |= static_cast<uint64_t>(*it++ & 0x7f) << shift;
  return v;
}
}  // namespace
}  // namespace oi::exporters
