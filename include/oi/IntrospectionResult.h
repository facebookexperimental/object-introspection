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
    using stack_t =
        std::stack<exporters::inst::Inst, std::vector<exporters::inst::Inst>>;

    const_iterator(std::vector<uint8_t>::const_iterator data,
                   exporters::inst::Inst type);
    const_iterator(std::vector<uint8_t>::const_iterator data);

    std::vector<uint8_t>::const_iterator data_;
    stack_t stack_;
    std::optional<result::Element> next_;

    std::vector<std::string_view> type_path_;
    // This field could be more space efficient as these strings are primarily
    // empty. They are used when the string isn't stored in the .rodata section,
    // currently when performing key capture. It needs reference stability as we
    // keep views in type_path_. A std::unique_ptr<std::string> would be an
    // improvement but it isn't copyable. A string type with size fixed at
    // construction would also be good.
    std::list<std::string> dynamic_type_path_;
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
