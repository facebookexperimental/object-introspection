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

/*
 * TypeCheckingWalker
 *
 * Walks a dynamic data segment type simultaneously with the contents of the
 * data segment, providing context to each extracted element.
 *
 * The dynamic types are implemented as a stack machine. Handling a type
 * pops the top element, handles it which may involve pushing more types onto
 * the stack, and returns an element from the data segment if appropriate or
 * recurses. See the implementation for details.
 */

#include <cstdint>
#include <optional>
#include <span>
#include <stack>
#include <stdexcept>
#include <variant>

#include "oi/types/dy.h"

namespace ObjectIntrospection {
namespace exporters {

class TypeCheckingWalker {
 public:
  struct VarInt {
    uint64_t value;
  };
  struct SumIndex {
    uint64_t index;
  };
  struct ListLength {
    uint64_t length;
  };
  using Element = std::variant<VarInt, SumIndex, ListLength>;

  TypeCheckingWalker(types::dy::Dynamic rootType,
                     std::span<const uint64_t> buffer)
      : stack({rootType}), buf(buffer) {
  }

  std::optional<Element> advance();

 private:
  std::stack<types::dy::Dynamic> stack;
  std::span<const uint64_t> buf;

 private:
  uint64_t popFront() {
    if (buf.empty()) {
      throw std::runtime_error("unexpected end of data segment");
    }
    auto el = buf.front();
    buf = buf.last(buf.size() - 1);
    return el;
  }
};

}  // namespace exporters
}  // namespace ObjectIntrospection
