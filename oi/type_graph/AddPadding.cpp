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

namespace oi::detail::type_graph {

Pass AddPadding::createPass() {
  auto fn = [](TypeGraph& typeGraph) {
    AddPadding pass(typeGraph);
    for (auto& type : typeGraph.rootTypes()) {
      pass.accept(type);
    }
  };

  return Pass("AddPadding", fn);
}

void AddPadding::accept(Type& type) {
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
    accept(param.type());
  }
  for (auto& member : c.members) {
    accept(member.type());
  }

  if (c.kind() == Class::Kind::Union) {
    // Only apply padding to the full size of the union, not between members
    for (const auto& member : c.members) {
      if (member.bitsize == c.size() * 8 || member.type().size() == c.size())
        return;  // Don't add padding to unions which don't need it
    }
    // This union's members aren't big enough to make up its size, so add a
    // single padding member
    addPadding(0, c.size() * 8, c.members);
    return;
  }

  std::vector<Member> paddedMembers;
  paddedMembers.reserve(c.members.size());
  for (size_t i = 0; i < c.members.size(); i++) {
    if (i == 0) {
      addPadding(0, c.members[0].bitOffset, paddedMembers);
    } else {
      addPadding(c.members[i - 1], c.members[i].bitOffset, paddedMembers);
    }

    paddedMembers.push_back(c.members[i]);
  }

  if (!c.members.empty()) {
    addPadding(c.members.back(), c.size() * 8, paddedMembers);
  } else {
    // Pad out empty classes
    addPadding(0, c.size() * 8, paddedMembers);
  }

  c.members = std::move(paddedMembers);

  for (const auto& child : c.children) {
    accept(child);
  }
}

void AddPadding::addPadding(const Member& prevMember,
                            uint64_t paddingEndBits,
                            std::vector<Member>& paddedMembers) {
  uint64_t prevMemberSizeBits;
  if (prevMember.bitsize == 0) {
    prevMemberSizeBits = prevMember.type().size() * 8;
  } else {
    prevMemberSizeBits = prevMember.bitsize;
  }

  uint64_t prevMemberEndBits = prevMember.bitOffset + prevMemberSizeBits;

  addPadding(prevMemberEndBits, paddingEndBits, paddedMembers);
}

void AddPadding::addPadding(uint64_t paddingStartBits,
                            uint64_t paddingEndBits,
                            std::vector<Member>& paddedMembers) {
  uint64_t paddingBits = paddingEndBits - paddingStartBits;
  if (paddingBits == 0)
    return;

  // We must only use Int8s for padding in order to not accidentally increase
  // the alignment requirements of this type. This applies to bitfield padding
  // as well.
  auto& primitive = typeGraph_.makeType<Primitive>(Primitive::Kind::Int8);

  if (paddingBits % 8 != 0) {
    // Pad with a bitfield up to the next byte
    paddedMembers.emplace_back(primitive, MemberPrefix, paddingStartBits,
                               paddingBits % 8);
  }

  uint64_t paddingBytes = paddingBits / 8;
  if (paddingBytes > 0) {
    // Pad with an array of bytes
    uint64_t paddingStartByte = (paddingStartBits + 7) / 8;
    auto& paddingArray = typeGraph_.makeType<Array>(primitive, paddingBytes);
    // Set inputName to an empty string as it has no name in the input
    auto& m =
        paddedMembers.emplace_back(paddingArray, "", paddingStartByte * 8);
    m.name = MemberPrefix;
  }
}

}  // namespace oi::detail::type_graph
