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
#include "CycleFinder.h"

#include <glog/logging.h>

#include "TypeGraph.h"

namespace type_graph {

Pass CycleFinder::createPass() {
  auto fn = [](TypeGraph& typeGraph) {
    CycleFinder finder{typeGraph};
    for (auto& type : typeGraph.rootTypes()) {
      finder.accept(type);
    }
  };

  return Pass("CycleFinder", fn);
}

void CycleFinder::accept(Type& type) {
  if (push(type)) {
    type.accept(*this);
    pop();
    return;
  }

  auto search = std::find_if(currentPath_.begin(), currentPath_.end(),
                             [&type](const Type& t) { return &t == &type; });
  CHECK(search != currentPath_.end()) << "set/stack invariant violated";

  breakCycle(std::span{search, currentPath_.end()});
}

bool CycleFinder::push(Type& t) {
  if (!currentPathSet_.emplace(&t).second)
    return false;
  currentPath_.emplace_back(t);
  return true;
}

void CycleFinder::pop() {
  const Type& t = currentPath_.back();
  currentPath_.pop_back();
  currentPathSet_.erase(&t);
}

CycleBreaker& CycleFinder::edge(Type& t) {
  if (auto it = replacements_.find(&t); it != replacements_.end()) {
    return it->second;
  } else {
    auto& e = typeGraph_.makeType<CycleBreaker>(t);
    replacements_.emplace(&t, e);
    return e;
  }
}

void CycleFinder::breakCycle(std::span<std::reference_wrapper<Type>> cycle) {
  if (cycle.size() >= 2) {
    Type& from = cycle.back();
    Type& to = cycle.front();

    if (auto* p = dynamic_cast<Pointer*>(&from);
        p && &p->pointeeType() == &to) {
      p->setPointeeType(edge(to));
      return;
    }

    if (auto* c = dynamic_cast<Container*>(&from)) {
      bool done = false;
      for (auto& param : c->templateParams) {
        if (param.type() == &to) {
          param.setType(&edge(to));
          done = true;
        }
      }
      if (done)
        return;
    }
  }

  // Unhandled, throw a descriptive error.
  std::string names;
  for (const Type& t : cycle) {
    names += t.name();
    names += ", ";
  }
  names += cycle.front().get().name();
  throw std::logic_error("Unable to handle type cycle: " + names);
}

}  // namespace type_graph
