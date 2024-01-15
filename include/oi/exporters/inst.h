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

#ifndef INCLUDED_OI_EXPORTERS_INST_H
#define INCLUDED_OI_EXPORTERS_INST_H 1

#include <oi/exporters/ParsedData.h>
#include <oi/result/Element.h>
#include <oi/types/dy.h>

#include <array>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <utility>
#include <variant>

namespace oi::exporters::inst {

struct PopTypePath;
struct Field;
struct Repeat;

using Inst =
    std::variant<PopTypePath, Repeat, std::reference_wrapper<const Field>>;
using Processor = void (*)(result::Element&,
                           std::function<void(Inst)>,
                           ParsedData);
using ProcessorInst = std::pair<types::dy::Dynamic, Processor>;

struct PopTypePath {};

struct Repeat {
  constexpr Repeat(size_t n_, const Field& field_) : n(n_), field(field_) {
  }

  size_t n;
  std::reference_wrapper<const Field> field;
};

struct Field {
  template <size_t N0, size_t N1, size_t N2>
  constexpr Field(size_t static_size_,
                  size_t exclusive_size_,
                  std::string_view name_,
                  const std::array<std::string_view, N0>& type_names_,
                  const std::array<Field, N1>& fields_,
                  const std::array<ProcessorInst, N2>& processors_,
                  bool is_primitive_);
  template <size_t N0, size_t N1, size_t N2>
  constexpr Field(size_t static_size_,
                  std::string_view name_,
                  const std::array<std::string_view, N0>& type_names_,
                  const std::array<Field, N1>& fields_,
                  const std::array<ProcessorInst, N2>& processors_,
                  bool is_primitive_)
      : Field(static_size_,
              static_size_,
              name_,
              type_names_,
              fields_,
              processors_,
              is_primitive_) {
  }
  constexpr Field(const Field&) = default;  // no idea why this is needed

  size_t static_size;
  size_t exclusive_size;
  std::string_view name;
  std::span<const std::string_view> type_names;
  std::span<const Field> fields;
  std::span<const ProcessorInst> processors;
  bool is_primitive;
};

template <size_t N0, size_t N1, size_t N2>
constexpr Field::Field(size_t static_size_,
                       size_t exclusive_size_,
                       std::string_view name_,
                       const std::array<std::string_view, N0>& type_names_,
                       const std::array<Field, N1>& fields_,
                       const std::array<ProcessorInst, N2>& processors_,
                       bool is_primitive_)
    : static_size(static_size_),
      exclusive_size(exclusive_size_),
      name(name_),
      type_names(type_names_),
      fields(fields_),
      processors(processors_),
      is_primitive(is_primitive_) {
}

}  // namespace oi::exporters::inst

#endif
