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
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

template <typename T>
inline constexpr bool always_false_v = false;

namespace oi {
namespace {
std::optional<std::string> genNameFromData(
    const decltype(result::Element::data)&);
}

IntrospectionResult::const_iterator&
IntrospectionResult::const_iterator::operator++() {
  if (stack_.empty()) {
    if (next_ != std::nullopt) {
      ++increments_;
      next_ = std::nullopt;
    }
    return *this;
  }
  ++increments_;

  auto el = stack_.top();
  stack_.pop();

  return std::visit(
      [this](auto&& r) -> IntrospectionResult::const_iterator& {
        using U = std::decay_t<decltype(r)>;
        if constexpr (std::is_same_v<U, exporters::inst::PopTypePath>) {
          if (!dynamic_type_path_.empty() &&
              dynamic_type_path_.back().first == type_path_.size())
            dynamic_type_path_.pop_back();
          type_path_.pop_back();
          return operator++();
        } else if constexpr (std::is_same_v<U, exporters::inst::Repeat>) {
          if (r.n-- != 0) {
            stack_.emplace(r);
            stack_.emplace(r.field);
          }
          return operator++();
        } else {
          // reference wrapper
          auto ty = r.get();
          using T = std::decay_t<decltype(ty)>;

          if constexpr (std::is_same_v<T, exporters::inst::Field>) {
            type_path_.emplace_back(ty.name);
            stack_.emplace(exporters::inst::PopTypePath{});
            next_.emplace(result::Element{
                .name = ty.name,
                .type_path = type_path_,
                .type_names = ty.type_names,
                .static_size = ty.static_size,
                .exclusive_size = ty.exclusive_size,
                .container_stats = std::nullopt,
                .is_set_stats = std::nullopt,
                .is_primitive = ty.is_primitive,
            });

            for (const auto& [dy, handler] : ty.processors) {
              auto parsed = exporters::ParsedData::parse(data_, dy);
              handler(
                  *next_, [this](auto i) { stack_.emplace(i); }, parsed);
            }

            if (auto new_name = genNameFromData(next_->data)) {
              std::string& new_name_ref =
                  dynamic_type_path_
                      .emplace_back(type_path_.size(), std::move(*new_name))
                      .second;

              type_path_.back() = new_name_ref;
              next_->name = new_name_ref;
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

namespace {

std::optional<std::string> genNameFromData(
    const decltype(result::Element::data)& d) {
  return std::visit(
      [](const auto& d) -> std::optional<std::string> {
        using V = std::decay_t<decltype(d)>;
        if constexpr (std::is_same_v<std::string, V>) {
          std::string out = "[";
          out.reserve(d.size() + 2);
          out += d;
          out += "]";
          return out;
        } else if constexpr (std::is_same_v<result::Element::Pointer, V>) {
          std::stringstream out;
          out << '[' << reinterpret_cast<void*>(d.p) << ']';
          return out.str();
        } else if constexpr (std::is_same_v<result::Element::Scalar, V>) {
          std::string out = "[";
          out += std::to_string(d.n);
          out += ']';
          return out;
        } else if constexpr (std::is_same_v<std::nullopt_t, V>) {
          return std::nullopt;
        } else {
          static_assert(always_false_v<V>, "missing variant");
        }
      },
      d);
}

}  // namespace
}  // namespace oi
