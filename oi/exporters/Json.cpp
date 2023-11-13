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
#include <oi/exporters/Json.h>

#include <algorithm>
#include <stdexcept>
#include <variant>

template <class>
inline constexpr bool always_false_v = false;

namespace oi::exporters {
namespace {

template <typename It>
void printStringList(std::ostream& out, It it, It end, bool pretty) {
  out << '[';
  for (; it != end; ++it) {
    out << '"' << (*it) << '"';
    if (it != end - 1)
      out << (pretty ? ", " : ",");
  }
  out << ']';
}

std::string makeIndent(size_t depth) {
  depth = std::max(depth, 1UL);
  return std::string((depth - 1) * 4, ' ');
}

}  // namespace

Json::Json(std::ostream& out) : out_(out) {
}

void Json::print(const IntrospectionResult& r) {
  auto begin = r.cbegin();
  auto end = r.cend();
  return print(begin, end);
}

void Json::print(IntrospectionResult::const_iterator& it,
                 IntrospectionResult::const_iterator& end) {
  const auto firstTypePathSize = it->type_path.size();

  const auto indent = pretty_ ? makeIndent(firstTypePathSize) : "";
  const auto lastIndent =
      pretty_ ? makeIndent(std::max(firstTypePathSize, 1UL) - 1) : "";
  const auto* tab = pretty_ ? "  " : "";
  const auto* space = pretty_ ? " " : "";
  const auto* endl = pretty_ ? "\n" : "";

  out_ << '[' << endl << indent;

  bool first = true;
  while (it != end) {
    if (it->type_path.size() < firstTypePathSize) {
      // no longer a sibling, must be a sibling of the type we're printing
      break;
    }

    if (!first)
      out_ << ',' << endl << indent;
    first = false;

    out_ << '{' << endl << indent;

    out_ << tab << "\"name\"" << space << ':' << space << "\"" << it->name
         << "\"," << endl
         << indent;

    out_ << tab << "\"typePath\"" << space << ':' << space << "";
    printStringList(out_, it->type_path.begin(), it->type_path.end(), pretty_);
    out_ << (pretty_ ? ",\n" : ",") << indent;

    out_ << tab << "\"typeNames\"" << space << ':' << space;
    printStringList(
        out_, it->type_names.begin(), it->type_names.end(), pretty_);
    out_ << ',' << endl << indent;

    out_ << tab << "\"staticSize\":" << space << it->static_size << ',' << endl
         << indent;
    out_ << tab << "\"exclusiveSize\":" << space << it->exclusive_size << ','
         << endl
         << indent;

    if (it->pointer.has_value()) {
      out_ << tab << "\"pointer\":" << space << *(it->pointer) << ',' << endl
           << indent;
    }

    if (auto* s = std::get_if<result::Element::Scalar>(&it->data)) {
      out_ << tab << "\"data\":" << space << s->n << ',' << endl << indent;
    } else if (auto* p = std::get_if<result::Element::Pointer>(&it->data)) {
      out_ << tab << "\"data\":" << space << "\"0x" << std::hex << p->p
           << std::dec << "\"," << endl
           << indent;
    } else if (auto* str = std::get_if<std::string>(&it->data)) {
      out_ << tab << "\"data\":" << space << "\"" << *str << "\"," << endl
           << indent;
    }

    if (it->container_stats.has_value()) {
      out_ << tab << "\"length\":" << space << it->container_stats->length
           << ',' << endl
           << indent;
      out_ << tab << "\"capacity\":" << space << it->container_stats->capacity
           << ',' << endl
           << indent;
    }
    if (it->is_set_stats.has_value()) {
      out_ << tab << "\"is_set\":" << space << it->is_set_stats->is_set << ','
           << endl
           << indent;
    }

    out_ << tab << "\"members\":" << space;
    if (++it != end && it->type_path.size() > firstTypePathSize) {
      print(it, end);
    } else {
      out_ << "[]" << endl;
    }

    out_ << indent << "}";
  }
  if (firstTypePathSize == 1) {
    out_ << endl << ']' << endl;
  } else {
    out_ << endl << lastIndent << tab << ']' << endl;
  }
}

}  // namespace oi::exporters
