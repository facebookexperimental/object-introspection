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
#include "AddPadding.h"

#include <cassert>

#include "TypeGraph.h"

template <typename T>
using ref = std::reference_wrapper<T>;

namespace type_graph {

Pass AddPadding::createPass() {
  auto fn = [](TypeGraph& typeGraph) {
    AddPadding pass(typeGraph);
    for (auto& type : typeGraph.rootTypes()) {
      pass.visit(type);
    }
  };

  return Pass("AddPadding", fn);
}

void AddPadding::visit(Type& type) {
  if (visited_.count(&type) != 0)
    return;

  visited_.insert(&type);
  type.accept(*this);
}

// TODO normalise pass names, e.g. Flattener -> Flatten, AlignmentCalc ->
// CalcAlignment
void AddPadding::visit(Class& c) {
  // AddPadding should be run after Flattener
  assert(c.parents.empty());

  for (auto& param : c.templateParams) {
    visit(param.type);
  }
  for (auto& member : c.members) {
    visit(*member.type);
  }

  if (c.kind() == Class::Kind::Union) {
    // Don't pad unions
    return;
  }

  std::vector<Member> paddedMembers;
  paddedMembers.reserve(c.members.size());
  for (size_t i = 0; i < c.members.size(); i++) {
    if (i >= 1) {
      uint64_t prevMemberEnd =
          c.members[i - 1].offset + c.members[i - 1].type->size();
      size_t paddingSize = c.members[i].offset - prevMemberEnd;
      if (paddingSize > 0) {
        auto* primitive =
            typeGraph_.make_type<Primitive>(Primitive::Kind::Int8);
        auto* paddingArray =
            typeGraph_.make_type<Array>(primitive, paddingSize);
        paddedMembers.emplace_back(paddingArray, MemberPrefix, prevMemberEnd);
      }
    }
    paddedMembers.push_back(c.members[i]);
  }

  // TODO reduce duplication with above? (put into function?)
  uint64_t prevMemberEnd = 0;
  if (!c.members.empty()) {
    prevMemberEnd = c.members.back().offset + c.members.back().type->size();
  }
  size_t paddingSize = c.size() - prevMemberEnd;
  if (paddingSize > 0) {
    auto* primitive = typeGraph_.make_type<Primitive>(Primitive::Kind::Int8);
    auto* paddingArray = typeGraph_.make_type<Array>(primitive, paddingSize);
    paddedMembers.emplace_back(paddingArray, MemberPrefix, prevMemberEnd);
  }

  c.members = std::move(paddedMembers);

  for (const auto& child : c.children) {
    visit(child);
  }
}

}  // namespace type_graph
