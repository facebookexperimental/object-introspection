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
  if (!currentPathSet_.emplace(&type).second) {
    auto& from = currentPath_.top().get();

    auto& edge = typeGraph_.makeType<CycleBreaker>(from, type);
    if (auto* p = dynamic_cast<Pointer*>(&from)) {
      p->setPointeeType(edge);
    } else if (auto* c = dynamic_cast<Container*>(&from)) {
      for (auto& param : c->templateParams) {
        if (param.type() == &type) {
          param.setType(&edge);
        }
      }
    } else {
      throw std::logic_error(
          "all cycles are expected to come from a Pointer or Container");
    }

    return;
  }

  // no cycle, add to path and continue
  currentPath_.push(type);
  type.accept(*this);
  currentPath_.pop();
  currentPathSet_.erase(&type);
}

}  // namespace type_graph
