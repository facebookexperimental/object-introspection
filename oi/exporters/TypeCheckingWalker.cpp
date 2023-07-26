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
#include "TypeCheckingWalker.h"

#include <type_traits>

template <class>
inline constexpr bool always_false_v = false;

namespace oi::detail::exporters {

std::optional<TypeCheckingWalker::Element> TypeCheckingWalker::advance() {
  if (stack.empty()) {
    return std::nullopt;
  }

  auto el = stack.top();
  stack.pop();

  return std::visit(
      [this](auto&& r) -> std::optional<TypeCheckingWalker::Element> {
        const auto& ty = r.get();
        using T = std::decay_t<decltype(ty)>;

        if constexpr (std::is_same_v<T, types::dy::Unit>) {
          // Unit type - noop.
          return advance();
        } else if constexpr (std::is_same_v<T, types::dy::VarInt>) {
          // VarInt type - pop one element and return as a `VarInt`.
          return TypeCheckingWalker::VarInt{popFront()};
        } else if constexpr (std::is_same_v<T, types::dy::Pair>) {
          // Pair type - read all of left then all of right. Recurse to get the
          // values.
          stack.push(ty.second);
          stack.push(ty.first);
          return advance();
        } else if constexpr (std::is_same_v<T, types::dy::List>) {
          // List type - pop one element as the length of the list, and place
          // that many of the element type on the stack. Return the value as a
          // `ListLength`.
          auto el = popFront();
          for (size_t i = 0; i < el; i++) {
            stack.push(ty.element);
          }
          return TypeCheckingWalker::ListLength{el};
        } else if constexpr (std::is_same_v<T, types::dy::Sum>) {
          // Sum type - pop one element as the index of the tagged union, and
          // place the matching element type on the stack. Return the value as a
          // `SumIndex`. Throw if the index is invalid.
          auto el = popFront();
          if (el >= ty.variants.size()) {
            throw std::runtime_error(std::string("invalid sum index: got ") +
                                     std::to_string(el) + ", max " +
                                     std::to_string(ty.variants.size()));
          }
          stack.push(ty.variants[el]);
          return TypeCheckingWalker::SumIndex{el};
        } else {
          static_assert(always_false_v<T>, "non-exhaustive visitor!");
        }
      },
      el);
}

}  // namespace oi::detail::exporters
