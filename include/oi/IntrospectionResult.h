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
#ifndef INCLUDED_OI_INTROSPECTIONRESULT_H
#define INCLUDED_OI_INTROSPECTIONRESULT_H 1

#include <oi/exporters/inst.h>
#include <oi/result/Element.h>
#include <oi/types/dy.h>

#include <cstdint>
#include <optional>
#include <span>
#include <stack>
#include <string_view>
#include <vector>

namespace oi {

class IntrospectionResult {
 public:
  class const_iterator {
    friend class IntrospectionResult;

   public:
    bool operator==(const const_iterator& that) const;
    bool operator!=(const const_iterator& that) const;
    const result::Element& operator*() const;
    const result::Element* operator->() const;
    const_iterator& operator++();
    const_iterator operator++(int);

   private:
    const_iterator(std::vector<uint8_t>::const_iterator data,
                   exporters::inst::Inst type);
    const_iterator(std::vector<uint8_t>::const_iterator data);

    std::vector<uint8_t>::const_iterator data_;
    std::stack<exporters::inst::Inst> stack_;
    std::optional<result::Element> next_;

    std::vector<std::string_view> type_path_;
  };

  IntrospectionResult(std::vector<uint8_t> buf, exporters::inst::Inst inst);

  const_iterator cbegin() const;
  const_iterator cend() const;

 private:
  std::vector<uint8_t> buf_;
  exporters::inst::Inst inst_;
};

}  // namespace oi

#include <oi/IntrospectionResult-inl.h>
#endif
