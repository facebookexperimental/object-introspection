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
#include "Flattener.h"

#include "TypeGraph.h"

namespace type_graph {

Pass Flattener::createPass() {
  auto fn = [](TypeGraph& typeGraph) {
    Flattener flattener;
    flattener.flatten(typeGraph.rootTypes());
    // TODO should flatten just operate on a single type and we do the looping
    // here?
  };

  return Pass("Flattener", fn);
}

void Flattener::flatten(std::vector<std::reference_wrapper<Type>>& types) {
  for (auto& type : types) {
    visit(type);
  }
}

void Flattener::visit(Type& type) {
  if (visited_.count(&type) != 0)
    return;

  visited_.insert(&type);
  type.accept(*this);
}

// TODO this function is a massive hack. don't do it like this please
Class& stripTypedefs(Type& type) {
  Type* t = &type;
  while (const Typedef* td = dynamic_cast<Typedef*>(t)) {
    t = td->underlyingType();
  }
  return dynamic_cast<Class&>(*t);
}

void Flattener::visit(Class& c) {
  // Members of a base class will be contiguous, but it's possible for derived
  // class members to be intersperced between embedded parent classes.
  //
  // This will happen when vptrs are present.
  //
  // e.g. Givin the original C++ classes:
  //   class Parent {
  //     int x;
  //     int y;
  //   };
  //   class Child : Parent {
  //     int a;
  //     int b;
  //   };
  //
  // The in memory (flattened) representation could be:
  //   class Child {
  //     int a;
  //     int x;
  //     int y;
  //     int b;
  //   };
  //    TODO comment about virtual inheritance

  // TODO alignment of parent classes
  //
  // TODO flatten template parameters??? ### TEST THIS ###

  // Flatten types referenced by members and parents
  for (const auto& member : c.members) {
    visit(*member.type);
  }
  for (const auto& parent : c.parents) {
    visit(*parent.type);
  }

  // Pull in functions from flattened parents
  for (const auto& parent : c.parents) {
    const Class& parentClass = stripTypedefs(*parent.type);
    c.functions.insert(c.functions.end(), parentClass.functions.begin(),
                       parentClass.functions.end());
  }

  // Pull member variables from flattened parents into this class
  std::vector<Member> flattenedMembers;

  std::size_t member_idx = 0;
  std::size_t parent_idx = 0;
  while (member_idx < c.members.size() && parent_idx < c.parents.size()) {
    auto member_offset = c.members[member_idx].offset;
    auto parent_offset = c.parents[parent_idx].offset;
    if (member_offset < parent_offset) {
      // Add our own member
      const auto& member = c.members[member_idx++];
      flattenedMembers.push_back(member);
    } else {
      // Add parent's members
      // If member_offset == parent_offset then the parent is empty. Also take
      // this path.
      const auto& parent = c.parents[parent_idx++];
      const Class& parentClass = stripTypedefs(*parent.type);

      for (size_t i = 0; i < parentClass.members.size(); i++) {
        const auto& member = parentClass.members[i];
        flattenedMembers.push_back(member);
        flattenedMembers.back().offset += parent.offset;
        if (i == 0) {
          flattenedMembers.back().align =
              std::max(flattenedMembers.back().align, parentClass.align());
        }
      }
    }
  }
  while (member_idx < c.members.size()) {
    const auto& member = c.members[member_idx++];
    flattenedMembers.push_back(member);
  }
  while (parent_idx < c.parents.size()) {
    const auto& parent = c.parents[parent_idx++];
    const Class& parentClass = stripTypedefs(*parent.type);
    for (const auto& member : parentClass.members) {
      flattenedMembers.push_back(member);
      flattenedMembers.back().offset += parent.offset;
    }
  }

  c.parents.clear();
  c.members = std::move(flattenedMembers);

  // Flatten types referenced by children.
  // This must be run after flattening the current class in order to respect
  // the changes we have made here.
  for (const auto& child : c.children) {
    visit(child);
  }
}

void Flattener::visit(Container& c) {
  // Containers themselves don't need to be flattened, but their template
  // parameters might need to be
  for (const auto& templateParam : c.templateParams) {
    visit(*templateParam.type);
  }
}

}  // namespace type_graph
