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
#if defined(INCLUDED_OI_INTROSPECTIONRESULT_INL_H) || \
    !defined(INCLUDED_OI_INTROSPECTIONRESULT_H)
static_assert(false,
              "IntrospectionResult-inl.h provides inline declarations for "
              "IntrospectionResult.h and should only "
              "be included by IntrospectionResult.h");
#endif
#define INCLUDED_OI_INTROSPECTIONRESULT_INL_H 1

#include <cassert>

#include "IntrospectionResult.h"

namespace oi {

inline IntrospectionResult::IntrospectionResult(std::vector<uint8_t> buf,
                                                exporters::inst::Inst inst)
    : buf_(std::move(buf)), inst_(inst) {
}

inline IntrospectionResult::const_iterator::const_iterator(
    std::vector<uint8_t>::const_iterator data, exporters::inst::Inst type)
    : data_(data), stack_({type}) {
}
inline IntrospectionResult::const_iterator::const_iterator(
    std::vector<uint8_t>::const_iterator data)
    : data_(data) {
}
inline IntrospectionResult::const_iterator IntrospectionResult::cbegin() const {
  return ++const_iterator{buf_.cbegin(), inst_};
}
inline IntrospectionResult::const_iterator IntrospectionResult::cend() const {
  return {buf_.cend()};
}

inline IntrospectionResult::const_iterator
IntrospectionResult::const_iterator::operator++(int) {
  auto old = *this;
  operator++();
  return old;
}
inline const result::Element& IntrospectionResult::const_iterator::operator*()
    const {
  assert(next_);
  return *next_;
}
inline const result::Element* IntrospectionResult::const_iterator::operator->()
    const {
  return &*next_;
}

inline bool IntrospectionResult::const_iterator::operator==(
    const IntrospectionResult::const_iterator& that) const {
  return this->data_ == that.data_ && !this->next_.has_value() &&
         !that.next_.has_value();  // TODO: is this sufficient? kind of hacky as
                                   // this only works for comparing to .end()
}
inline bool IntrospectionResult::const_iterator::operator!=(
    const IntrospectionResult::const_iterator& that) const {
  return !(*this == that);
}

}  // namespace oi
