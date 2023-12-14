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
#include "AlignmentCalc.h"

#include <cassert>

#include "TypeGraph.h"

template <typename T>
using ref = std::reference_wrapper<T>;

namespace oi::detail::type_graph {

Pass AlignmentCalc::createPass() {
  auto fn = [](TypeGraph& typeGraph, NodeTracker&) {
    AlignmentCalc alignmentCalc;
    alignmentCalc.calculateAlignments(typeGraph.rootTypes());
  };

  return Pass("AlignmentCalc", fn);
}

void AlignmentCalc::calculateAlignments(const std::vector<ref<Type>>& types) {
  for (auto& type : types) {
    accept(type);
  }
};

void AlignmentCalc::accept(Type& type) {
  if (visited_.count(&type) != 0)
    return;

  visited_.insert(&type);
  type.accept(*this);
}

void AlignmentCalc::visit(Class& c) {
  RecursiveVisitor::visit(c);

  uint64_t alignment = 1;
  for (auto& member : c.members) {
    if (member.align == 0) {
      // If the member does not have an explicit alignment, calculate it from
      // the member's type.
      accept(member.type());
      member.align = member.type().align();
    }
    alignment = std::max(alignment, member.align);

    if (member.align != 0 && (member.bitOffset / 8) % member.align != 0) {
      // Mark as packed if members are not aligned
      c.setPacked();
    }
  }

  c.setAlign(alignment);

  if (c.size() % c.align() != 0) {
    // Mark as packed if there is no tail padding
    c.setPacked();
  }
}

void AlignmentCalc::visit(Container& c) {
  RecursiveVisitor::visit(c);

  if (c.underlying()) {
    c.setAlign(c.underlying()->align());
  }
}

}  // namespace oi::detail::type_graph
