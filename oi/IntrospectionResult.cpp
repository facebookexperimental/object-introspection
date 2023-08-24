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
#include <oi/IntrospectionResult.h>
#include <oi/exporters/ParsedData.h>
#include <oi/types/dy.h>

#include <cassert>
#include <iterator>
#include <stdexcept>
#include <iostream> // TODO: remove

template <typename T>
inline constexpr bool always_false_v = false;

namespace oi {

IntrospectionResult::const_iterator&
IntrospectionResult::const_iterator::operator++() {
  if (stack_.empty()) {
    next_ = std::nullopt;
    return *this;
  }

  auto el = stack_.top();
  stack_.pop();

  return std::visit(
      [this](auto&& r) -> IntrospectionResult::const_iterator& {
        using U = std::decay_t<decltype(r)>;
        if constexpr (std::is_same_v<U, exporters::inst::PopTypePath>) {
          type_path_.pop_back();
          return operator++();
        } else {
          // reference wrapper
          auto ty = r.get();
          using T = std::decay_t<decltype(ty)>;

          if constexpr (std::is_same_v<T, exporters::inst::Field>) {
            std::cerr << "ty.name = " << ty.name << std::endl;
            type_path_.emplace_back(ty.name);
            stack_.emplace(exporters::inst::PopTypePath{});
            next_ = result::Element{
                .name = ty.name,
                .type_path = type_path_,
                .type_names = ty.type_names,
                .static_size = ty.static_size,
                .exclusive_size = ty.exclusive_size,
                .container_stats = std::nullopt,
                .is_set_stats = std::nullopt,
            };

            for (const auto& [dy, handler] : ty.processors) {
              auto parsed = exporters::ParsedData::parse(data_, dy);
              handler(*next_, stack_, parsed);
            }
            for (auto it = ty.fields.rbegin(); it != ty.fields.rend(); ++it) {
              stack_.emplace(*it);
            }

            return *this;
          } else {
            static_assert(always_false_v<T>, "non-exhaustive visitor!");
          }
        }
      },
      el);
}

}  // namespace oi
