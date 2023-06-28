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
      addPadding(c.members[i - 1], c.members[i].offset, paddedMembers);
    }
    paddedMembers.push_back(c.members[i]);
  }

  if (!c.members.empty()) {
    addPadding(c.members.back(), c.size(), paddedMembers);
  }

  c.members = std::move(paddedMembers);

  for (const auto& child : c.children) {
    visit(child);
  }
}

void AddPadding::addPadding(const Member& prevMember,
                            uint64_t paddingEnd,
                            std::vector<Member>& paddedMembers) {
  uint64_t prevMemberEnd = prevMember.offset + prevMember.type->size();
  uint64_t padding = paddingEnd - prevMemberEnd;
  if (padding == 0)
    return;

  auto* primitive = typeGraph_.make_type<Primitive>(Primitive::Kind::Int8);
  auto* paddingArray = typeGraph_.make_type<Array>(primitive, padding);
  paddedMembers.emplace_back(paddingArray, MemberPrefix, prevMemberEnd);
}

}  // namespace type_graph
