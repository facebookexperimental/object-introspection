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
#include <oi/exporters/CSV.h>

#include <algorithm>
#include <ranges>
#include <stdexcept>

namespace oi::exporters {

void CSV::print(const IntrospectionResult& result) {
  auto begin = result.cbegin();
  return print(begin, result.cend());
}

template <typename Seq>
std::string CSV::escapeField(const Seq& seq) {
  std::string field;
  size_t index = 0;
  for (const auto item : seq) {
    if (index++ > 0) {
      field += kListDelimiter;
    }

    field += item;
  }

  return escapeField(field);
}

std::string CSV::escapeField(std::string_view field) {
  return escapeField(std::string(field));
}

std::string CSV::escapeField(std::string field) {
  // Escape every instance of double quotes.
  auto it = field.find(kQuote);
  while (it != std::string::npos) {
    field.replace(it, 1, kEscapedQuote);
    it = field.find(kQuote, it + kEscapedQuote.size());
  }

  field.insert(0, kQuote);
  field.append(kQuote);
  return field;
}

void CSV::print(IntrospectionResult::const_iterator& it,
                IntrospectionResult::const_iterator end) {
  printHeader();

  parentIdStack_.resize(1);  // Reset to parentIdStack_ = {0}
  for (/* it */; it != end; ++it) {
    out_ << ++id_ << kDelimiter;
    out_ << escapeField(it->name) << kDelimiter;
    out_ << escapeField(it->type_path) << kDelimiter;
    out_ << escapeField(it->type_names) << kDelimiter;
    out_ << it->static_size << kDelimiter;
    out_ << it->exclusive_size << kDelimiter;

    if (!it->pointer.has_value()) {
      out_ << kDelimiter;
    } else {
      out_ << it->pointer.value() << kDelimiter;
    }

    if (!it->container_stats.has_value()) {
      out_ << kDelimiter << kDelimiter;
    } else {
      out_ << it->container_stats->length << kDelimiter;
      out_ << it->container_stats->capacity << kDelimiter;
    }

    if (!it->is_set_stats.has_value()) {
      out_ << kDelimiter;
    } else {
      out_ << it->is_set_stats->is_set << kDelimiter;
    }

    while (parentIdStack_.size() > it->type_path.size()) {
      parentIdStack_.pop_back();
    }

    out_ << parentIdStack_.back();

    parentIdStack_.push_back(id_);

    out_ << kCRLF;
  }
}

void CSV::printHeader() {
  size_t index = 0;
  for (const auto column : kColumns) {
    if (index++ > 0) {
      out_ << kDelimiter;
    }

    out_ << column;
  }
  out_ << kCRLF;
}

}  // namespace oi::exporters
