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
#ifndef INCLUDED_OI_RESULT_ELEMENT_H
#define INCLUDED_OI_RESULT_ELEMENT_H 1

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace oi::result {

struct Element {
  struct ContainerStats {
    size_t capacity;
    size_t length;
  };
  struct IsSetStats {
    bool is_set;
  };
  struct Pointer {
    uintptr_t p;
  };
  struct Scalar {
    uint64_t n;
  };

  std::string_view name;
  std::vector<std::string_view>
      type_path;  // TODO: should be span<const std::string_view>
  std::span<const std::string_view> type_names;
  size_t static_size;
  size_t exclusive_size;

  std::optional<uintptr_t> pointer;
  std::variant<std::nullopt_t, Pointer, Scalar, std::string> data = {
      std::nullopt};
  std::optional<ContainerStats> container_stats;
  std::optional<IsSetStats> is_set_stats;
  bool is_primitive;
};

}  // namespace oi::result

#endif
