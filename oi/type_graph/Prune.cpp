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
#include "Prune.h"

#include "TypeGraph.h"
#include "TypeIdentifier.h"

namespace oi::detail::type_graph {

Pass Prune::createPass() {
  auto fn = [](TypeGraph& typeGraph, NodeTracker& tracker) {
    Prune pass{tracker};
    for (auto& type : typeGraph.rootTypes()) {
      pass.accept(type);
    }
  };

  return Pass("Prune", fn);
}

void Prune::accept(Type& type) {
  if (tracker_.visit(type))
    return;

  type.accept(*this);
}

void Prune::visit(Class& c) {
  RecursiveVisitor::visit(c);

  c.templateParams.clear();
  c.parents.clear();
  c.functions.clear();

  // Should we bother with this shrinking? It saves memory but costs CPU
  c.templateParams.shrink_to_fit();
  c.parents.shrink_to_fit();
  c.functions.shrink_to_fit();
}

void Prune::visit(Container& c) {
  RecursiveVisitor::visit(c);

  c.setUnderlying(nullptr);
}

}  // namespace oi::detail::type_graph
