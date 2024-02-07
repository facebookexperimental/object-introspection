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
#if defined(INCLUDED_OI_RESULT_SIZED_ELEMENT_INL_H) || \
    !defined(INCLUDED_OI_RESULT_SIZED_ELEMENT_H)
static_assert(false,
              "SizedResult-inl.h provides inline declarations for "
              "SizedResult.h and should only "
              "be included by SizedResult.h");
#endif
#define INCLUDED_OI_RESULT_SIZED_ELEMENT_INL_H 1

#include <utility>
#include <vector>

#include "SizedResult.h"

namespace oi::result {

template <typename Res>
SizedResult<Res>::SizedResult(Res res) : res_{std::move(res)} {
}

template <typename Res>
typename SizedResult<Res>::const_iterator SizedResult<Res>::begin() const {
  const_iterator it{res_.begin(), res_.end()};
  ++it;
  return it;
}
template <typename Res>
typename SizedResult<Res>::const_iterator SizedResult<Res>::end() const {
  return res_.end();
}

template <typename Res>
SizedResult<Res>::const_iterator::const_iterator(It it, const It& end)
    : data_{it.clone()} {
  struct StackEntry {
    size_t index;
    size_t depth;
  };
  std::vector<StackEntry> stack;

  size_t size = 0;
  size_t count = -1;
  for (; it != end; ++it) {
    ++count;

    auto depth = it->type_path.size();
    while (!stack.empty() && stack.back().depth >= depth) {
      auto i = stack.back().index;
      stack.pop_back();
      helpers_[i].last_child = count - 1;
    }

    size += it->exclusive_size;
    helpers_.emplace_back(SizeHelper{.size = size});
    stack.emplace_back(StackEntry{.index = count, .depth = depth});
  }
  while (!stack.empty()) {
    auto i = stack.back().index;
    stack.pop_back();
    helpers_[i].last_child = count;
  }
}

template <typename Res>
SizedResult<Res>::const_iterator::const_iterator(It end)
    : data_{std::move(end)} {
}

template <typename Res>
typename SizedResult<Res>::const_iterator
SizedResult<Res>::const_iterator::operator++(int) {
  auto old = *this;
  operator++();
  return old;
}

template <typename Res>
typename SizedResult<Res>::const_iterator&
SizedResult<Res>::const_iterator::operator++() {
  // The below iterator is already pointing at the first element while this
  // iterator is not. Skip incrementing it the first time around.
  if (count_ != 0)
    ++data_;

  if (count_ == helpers_.size()) {
    next_.reset();
    return *this;
  }

  size_t size = helpers_[helpers_[count_].last_child].size;
  if (count_ != 0)
    size -= helpers_[count_ - 1].size;

  next_.emplace(*data_, size);
  ++count_;
  return *this;
}

template <typename Res>
const typename SizedResult<Res>::Element&
SizedResult<Res>::const_iterator::operator*() const {
  return *next_;
}
template <typename Res>
const typename SizedResult<Res>::Element*
SizedResult<Res>::const_iterator::operator->() const {
  return &*next_;
}

template <typename Res>
bool SizedResult<Res>::const_iterator::operator==(
    const SizedResult<Res>::const_iterator& that) const {
  return this->data_ == that.data_;
}

template <typename Res>
bool SizedResult<Res>::const_iterator::operator!=(
    const SizedResult<Res>::const_iterator& that) const {
  return !(*this == that);
}

template <typename El>
SizedElement<El>::SizedElement(const El& el, size_t size_)
    : El{el}, size{size_} {
}

template <typename El>
const El& SizedElement<El>::inner() const {
  return static_cast<const El&>(*this);
}

}  // namespace oi::result
