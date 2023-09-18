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
#ifndef INCLUDED_OI_EXPORTERS_CSV_H
#define INCLUDED_OI_EXPORTERS_CSV_H 1

#include <oi/IntrospectionResult.h>

#include <ostream>

namespace oi::exporters {

// CSV exporter following [RFC 4180](https://www.rfc-editor.org/rfc/rfc4180).
class CSV {
 public:
  CSV(std::ostream& out) : out_(out) {
  }

  void print(const IntrospectionResult&);
  void print(IntrospectionResult::const_iterator& begin,
             IntrospectionResult::const_iterator end);

 private:
  static constexpr std::string_view kCRLF = "\r\n";
  static constexpr std::string_view kDelimiter = ",";
  static constexpr std::string_view kQuote = "\"";
  static constexpr std::string_view kEscapedQuote = "\\\"";
  static constexpr std::string_view kListDelimiter = ";";

  static constexpr std::string_view kColumns[] = {
      "id",         "name",          "typePath", "typeNames",
      "staticSize", "exclusiveSize", "pointer",  "length",
      "capacity",   "is_set",        "parent_id"};

  size_t id_ = 0;
  std::vector<size_t> parentIdStack_ = {0};

  std::ostream& out_;

  void printHeader();

  template <typename Seq>
  static std::string escapeField(const Seq&);

  static std::string escapeField(std::string_view);
  static std::string escapeField(std::string);
};

}  // namespace oi::exporters

#endif
