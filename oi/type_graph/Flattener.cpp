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
#include "TypeIdentifier.h"

namespace oi::detail::type_graph {

Pass Flattener::createPass() {
  auto fn = [](TypeGraph& typeGraph, NodeTracker& tracker) {
    Flattener flattener{tracker};
    for (auto& type : typeGraph.rootTypes()) {
      flattener.accept(type);
    }
  };

  return Pass("Flattener", fn);
}

void Flattener::accept(Type& type) {
  if (tracker_.visit(type))
    return;

  type.accept(*this);
}

namespace {
void flattenParent(const Parent& parent,
                   std::vector<Member>& flattenedMembers) {
  Type& parentType = stripTypedefs(parent.type());
  if (auto* parentClass = dynamic_cast<Class*>(&parentType)) {
    for (size_t i = 0; i < parentClass->members.size(); i++) {
      const auto& member = parentClass->members[i];
      flattenedMembers.push_back(member);
      flattenedMembers.back().bitOffset += parent.bitOffset;
      if (i == 0) {
        flattenedMembers.back().align =
            std::max(flattenedMembers.back().align, parentClass->align());
      }
    }
  } else if (auto* parentContainer = dynamic_cast<Container*>(&parentType)) {
    // Create a new member to represent this parent container
    flattenedMembers.emplace_back(
        *parentContainer, Flattener::ParentPrefix, parent.bitOffset);
  } else if (auto* parentPrimitive = dynamic_cast<Incomplete*>(&parentType)) {
    // Bad DWARF can lead to us seeing incomplete parent types. Just ignore
    // these as there is nothing we can do to recover the missing info.
  } else {
    throw std::runtime_error("Invalid type for parent");
  }
}

/*
 * Some compilers generate bad DWARF for allocators, such that they are missing
 * their template parameter (which represents the type to be allocated).
 *
 * Try to add this missing parameter back in by pulling it from the allocator's
 * parent class.
 */
void fixAllocatorParams(Class& alloc) {
  if (!TypeIdentifier::isAllocator(alloc)) {
    return;
  }

  if (!alloc.templateParams.empty()) {
    // The DWARF looks ok
    return;
  }

  if (alloc.parents.empty()) {
    // Nothing we can do
    return;
  }

  Type& parent = stripTypedefs(alloc.parents[0].type());
  Class* parentClass = dynamic_cast<Class*>(&parent);
  if (!parentClass) {
    // Not handled
    return;
  }

  if (parentClass->templateParams.empty()) {
    // Nothing we can do
    return;
  }

  if (parentClass->templateParams[0].value) {
    // Nothing we can do
    return;
  }

  Type& allocParam = parentClass->templateParams[0].type();
  Type& typeToAllocate = stripTypedefs(allocParam);
  alloc.templateParams.push_back(TemplateParam{typeToAllocate});
}
}  // namespace

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

  // Flatten types referenced by template params, parents and members
  for (const auto& param : c.templateParams) {
    accept(param.type());
  }
  for (const auto& parent : c.parents) {
    accept(parent.type());
  }
  for (const auto& member : c.members) {
    accept(member.type());
  }

  // Pull in functions from flattened parents
  for (const auto& parent : c.parents) {
    Type& parentType = stripTypedefs(parent.type());
    if (Class* parentClass = dynamic_cast<Class*>(&parentType)) {
      c.functions.insert(c.functions.end(),
                         parentClass->functions.begin(),
                         parentClass->functions.end());
    }
  }

  // Pull member variables from flattened parents into this class
  std::vector<Member> flattenedMembers;

  std::size_t member_idx = 0;
  std::size_t parent_idx = 0;
  while (member_idx < c.members.size() && parent_idx < c.parents.size()) {
    auto member_offset = c.members[member_idx].bitOffset;
    auto parent_offset = c.parents[parent_idx].bitOffset;
    if (member_offset < parent_offset) {
      // Add our own member
      const auto& member = c.members[member_idx++];
      flattenedMembers.push_back(member);
    } else {
      // Add parent's members
      // If member_offset == parent_offset then the parent is empty. Also take
      // this path.
      const auto& parent = c.parents[parent_idx++];
      flattenParent(parent, flattenedMembers);
    }
  }
  while (member_idx < c.members.size()) {
    const auto& member = c.members[member_idx++];
    flattenedMembers.push_back(member);
  }
  while (parent_idx < c.parents.size()) {
    const auto& parent = c.parents[parent_idx++];
    flattenParent(parent, flattenedMembers);
  }

  // Perform fixups for bad DWARF
  fixAllocatorParams(c);

  c.parents.clear();
  c.members = std::move(flattenedMembers);

  // Flatten types referenced by children.
  // This must be run after flattening the current class in order to respect
  // the changes we have made here.
  for (const auto& child : c.children) {
    accept(child);
  }

  // Pull in children from flattened children
  // This may result in duplicates, but that shouldn't be a big deal
  for (const Class& child : c.children) {
    c.children.insert(
        c.children.end(), child.children.begin(), child.children.end());
  }
}

}  // namespace oi::detail::type_graph
