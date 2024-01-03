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
#ifndef INCLUDED_OI_RESULT_SIZED_ELEMENT_H
#define INCLUDED_OI_RESULT_SIZED_ELEMENT_H 1

#include <cstddef>
#include <optional>
#include <type_traits>
#include <vector>

namespace oi::result {

template <typename El>
struct SizedElement : public El {
  SizedElement(const El& el, size_t size);
  const El& inner() const;

  size_t size;
};

template <typename Res>
class SizedResult {
 private:
  using It = std::decay_t<decltype(std::declval<Res&>().begin())>;
  using ParentEl = std::decay_t<decltype(std::declval<It&>().operator*())>;

 public:
  using Element = SizedElement<ParentEl>;

 private:
  struct SizeHelper {
    size_t size = -1;
    size_t last_child = -1;
  };

 public:
  class const_iterator {
    friend SizedResult;

   public:
    bool operator==(const const_iterator& that) const;
    bool operator!=(const const_iterator& that) const;
    const Element& operator*() const;
    const Element* operator->() const;
    const_iterator& operator++();
    const_iterator operator++(int);

   private:
    const_iterator(It start, const It& end);
    const_iterator(It end);

    It data_;

    std::vector<SizeHelper> helpers_;
    size_t count_ = 0;
    std::optional<Element> next_;
  };

  SizedResult(Res res);

  const_iterator begin() const;
  const_iterator end() const;

 private:
  Res res_;
};

}  // namespace oi::result

#include "SizedResult-inl.h"
#endif
