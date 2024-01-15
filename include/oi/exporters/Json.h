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
#ifndef INCLUDED_OI_EXPORTERS_JSON_H
#define INCLUDED_OI_EXPORTERS_JSON_H 1

#include <oi/IntrospectionResult.h>
#include <oi/result/SizedResult.h>

#include <ostream>
#include <string_view>

namespace oi::exporters {

class Json {
 public:
  Json(std::ostream& out);

  template <typename Res>
  void print(const Res& r) {
    auto begin = r.begin();
    auto end = r.end();
    return print(begin, end);
  }
  template <typename It>
  void print(It& it, const It& end);

  void setPretty(bool pretty) {
    pretty_ = pretty;
  }

 private:
  std::string_view tab() const;
  std::string_view space() const;
  std::string_view endl() const;
  static std::string makeIndent(size_t depth);

  void printStringField(std::string_view name,
                        std::string_view value,
                        std::string_view indent);
  void printBoolField(std::string_view name,
                      bool value,
                      std::string_view indent);
  void printUnsignedField(std::string_view name,
                          uint64_t value,
                          std::string_view indent);
  void printPointerField(std::string_view name,
                         uintptr_t value,
                         std::string_view indent);
  template <typename Rng>
  void printListField(std::string_view name,
                      const Rng& range,
                      std::string_view indent);

  void printFields(const result::Element&, std::string_view indent);
  template <typename El>
  void printFields(const result::SizedElement<El>&, std::string_view indent);

  bool pretty_ = false;
  std::ostream& out_;
};

inline Json::Json(std::ostream& out) : out_(out) {
}

inline std::string_view Json::tab() const {
  return pretty_ ? "  " : "";
}
inline std::string_view Json::space() const {
  return pretty_ ? " " : "";
}
inline std::string_view Json::endl() const {
  return pretty_ ? "\n" : "";
}
inline std::string Json::makeIndent(size_t depth) {
  depth = std::max(depth, 1UL);
  return std::string((depth - 1) * 4, ' ');
}

inline void Json::printStringField(std::string_view name,
                                   std::string_view value,
                                   std::string_view indent) {
  out_ << tab() << '"' << name << '"' << ':' << space() << "\"" << value
       << "\"," << endl() << indent;
}
inline void Json::printBoolField(std::string_view name,
                                 bool value,
                                 std::string_view indent) {
  out_ << tab() << '"' << name << "\":" << space() << (value ? "true" : "false")
       << ',' << endl() << indent;
}
inline void Json::printUnsignedField(std::string_view name,
                                     uint64_t value,
                                     std::string_view indent) {
  out_ << tab() << '"' << name << "\":" << space() << value << ',' << endl()
       << indent;
}
inline void Json::printPointerField(std::string_view name,
                                    uintptr_t value,
                                    std::string_view indent) {
  out_ << tab() << '"' << name << "\":" << space() << "\"0x" << std::hex
       << value << std::dec << "\"," << endl() << indent;
}
template <typename Rng>
void Json::printListField(std::string_view name,
                          const Rng& range,
                          std::string_view indent) {
  out_ << tab() << '"' << name << '"' << ':' << space() << '[';
  bool first = true;
  for (const auto& el : range) {
    if (!std::exchange(first, false))
      out_ << ',' << space();
    out_ << '"' << el << '"';
  }
  out_ << "]," << endl() << indent;
}

template <typename El>
void Json::printFields(const result::SizedElement<El>& el,
                       std::string_view indent) {
  printUnsignedField("size", el.size, indent);

  printFields(el.inner(), indent);
}

inline void Json::printFields(const result::Element& el,
                              std::string_view indent) {
  printStringField("name", el.name, indent);
  printListField("typePath", el.type_path, indent);
  printListField("typeNames", el.type_names, indent);
  printUnsignedField("staticSize", el.static_size, indent);
  printUnsignedField("exclusiveSize", el.exclusive_size, indent);
  if (el.pointer.has_value())
    printUnsignedField("pointer", *el.pointer, indent);

  if (const auto* s = std::get_if<result::Element::Scalar>(&el.data)) {
    printUnsignedField("data", s->n, indent);
  } else if (const auto* p = std::get_if<result::Element::Pointer>(&el.data)) {
    printPointerField("data", p->p, indent);
  } else if (const auto* str = std::get_if<std::string>(&el.data)) {
    printStringField("data", *str, indent);
  }

  if (el.container_stats.has_value()) {
    printUnsignedField("length", el.container_stats->length, indent);
    printUnsignedField("capacity", el.container_stats->capacity, indent);
  }
  if (el.is_set_stats.has_value())
    printUnsignedField("is_set", el.is_set_stats->is_set, indent);
  printBoolField("is_primitive", el.is_primitive, indent);
}

template <typename It>
void Json::print(It& it, const It& end) {
  const auto depth = it->type_path.size();

  const auto thisIndent = pretty_ ? makeIndent(depth) : "";
  const auto lastIndent = pretty_ ? makeIndent(depth - 1) : "";

  out_ << '[' << endl() << thisIndent;

  bool first = true;
  while (it != end && it->type_path.size() >= depth) {
    if (!std::exchange(first, false))
      out_ << ',' << endl() << thisIndent;

    out_ << '{' << endl() << thisIndent;

    printFields(*it, thisIndent);

    out_ << tab() << "\"members\":" << space();
    if (++it != end && it->type_path.size() > depth) {
      print(it, end);
    } else {
      out_ << "[]" << endl();
    }

    out_ << thisIndent << "}";
  }
  if (depth == 1) {
    out_ << endl() << ']' << endl();
  } else {
    out_ << endl() << lastIndent << tab() << ']' << endl();
  }
}

}  // namespace oi::exporters

#endif
