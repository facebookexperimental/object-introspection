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
#include <list>
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
    const_iterator(const const_iterator&) = default;
    const_iterator& operator=(const const_iterator& other) = default;

   public:
    const_iterator(const_iterator&&) = default;
    const_iterator& operator=(const_iterator&&) = default;
    // Explicit interface for copying
    const_iterator clone() const;

   private:
    using stack_t =
        std::stack<exporters::inst::Inst, std::vector<exporters::inst::Inst>>;

    const_iterator(std::vector<uint8_t>::const_iterator data,
                   exporters::inst::Inst type);
    const_iterator(std::vector<uint8_t>::const_iterator data);

    std::vector<uint8_t>::const_iterator data_;
    stack_t stack_;
    std::optional<result::Element> next_;

    std::vector<std::string_view> type_path_;
    // Holds a pair of the type path entry this represents and the owned string
    // that type_path_ has a view of.
    std::list<std::pair<size_t, std::string>> dynamic_type_path_;

    // We cannot track the position in the iteration solely by the underlying
    // iterator as some fields do not extract data (for example, primitives).
    // Track the number of increment operations as well to get an accurate
    // equality check.
    uint64_t increments_ = 0;
  };

  IntrospectionResult(std::vector<uint8_t> buf, exporters::inst::Inst inst);

  const_iterator begin() const;
  const_iterator cbegin() const;

  const_iterator end() const;
  const_iterator cend() const;

 private:
  std::vector<uint8_t> buf_;
  exporters::inst::Inst inst_;
};

}  // namespace oi

#include <oi/IntrospectionResult-inl.h>
#endif
