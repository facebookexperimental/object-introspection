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
#include "TopoSorter.h"

#include "TypeGraph.h"

template <typename T>
using ref = std::reference_wrapper<T>;

namespace oi::detail::type_graph {

Pass TopoSorter::createPass() {
  auto fn = [](TypeGraph& typeGraph, NodeTracker&) {
    TopoSorter sorter;
    sorter.sort(typeGraph.rootTypes());
    typeGraph.finalTypes = std::move(sorter.sortedTypes());
  };

  return Pass("TopoSorter", fn);
}

void TopoSorter::sort(const std::vector<ref<Type>>& types) {
  for (const auto& type : types) {
    typesToSort_.push(type);
  }
  while (!typesToSort_.empty()) {
    accept(typesToSort_.front());
    typesToSort_.pop();
  }
}

const std::vector<ref<Type>>& TopoSorter::sortedTypes() const {
  return sortedTypes_;
}

void TopoSorter::accept(Type& type) {
  if (visited_.count(&type) != 0)
    return;

  visited_.insert(&type);
  type.accept(*this);
}

void TopoSorter::visit(Class& c) {
  for (const auto& parent : c.parents) {
    accept(parent.type());
  }
  for (const auto& mem : c.members) {
    accept(mem.type());
  }
  for (const auto& param : c.templateParams) {
    accept(param.type());
  }
  sortedTypes_.push_back(c);

  // Same as pointers, children do not create a dependency so are delayed until
  // the end.
  for (const auto& child : c.children) {
    acceptAfter(child);
  }
}

void TopoSorter::visit(Primitive& p) {
  sortedTypes_.push_back(p);
}

namespace {
/*
 * C++17 allows the std::vector, std::list and std::forward_list containers to
 * be instantiated with incomplete types.
 *
 * Other containers are not required to do this, but might still have this
 * behaviour.
 */
bool containerAllowsIncompleteParam(const Container& c, size_t i) {
  switch (c.containerInfo_.ctype) {
    case SEQ_TYPE:
    case LIST_TYPE:
    case UNIQ_PTR_TYPE:
    case SHRD_PTR_TYPE:
      // Also std::forward_list, if we ever support that
      // Would be good to have this as an option in the TOML files
      return i == 0;
    default:
      return false;
  }
}
}  // namespace

void TopoSorter::visit(Container& c) {
  for (size_t i = 0; i < c.templateParams.size(); i++) {
    const auto& param = c.templateParams[i];
    if (containerAllowsIncompleteParam(c, i)) {
      acceptAfter(param.type());
    } else {
      accept(param.type());
    }
  }
  sortedTypes_.push_back(c);
}

void TopoSorter::visit(Enum& e) {
  sortedTypes_.push_back(e);
}

void TopoSorter::visit(Typedef& td) {
  accept(td.underlyingType());
  sortedTypes_.push_back(td);
}

void TopoSorter::visit(Pointer& p) {
  if (dynamic_cast<Typedef*>(&p.pointeeType())) {
    // Typedefs can not be forward declared, so we must sort them before
    // pointers which reference them
    accept(p.pointeeType());
    return;
  }

  // Pointers do not create a dependency, but we do still care about the types
  // they point to, so delay them until the end.
  acceptAfter(p.pointeeType());
}

void TopoSorter::visit(CaptureKeys& c) {
  accept(c.container());
  sortedTypes_.push_back(c);
}

void TopoSorter::visit(Incomplete& i) {
  sortedTypes_.push_back(i);
}

/*
 * A type graph may contain cycles, so we need to slightly tweak the standard
 * topological sorting algorithm. Cycles can only be introduced by certain
 * edges, e.g. a pointer's underlying type. However, these edges do not
 * introduce dependencies as these types can be forward declared in a C++
 * program. This means we can delay processing them until after all of the true
 * dependencies have been sorted.
 */
void TopoSorter::acceptAfter(Type& type) {
  typesToSort_.push(type);
}

void TopoSorter::acceptAfter(Type* type) {
  if (type) {
    acceptAfter(*type);
  }
}

}  // namespace oi::detail::type_graph
