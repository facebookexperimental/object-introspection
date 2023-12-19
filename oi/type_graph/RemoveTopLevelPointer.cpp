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
#include "RemoveTopLevelPointer.h"

#include "TypeGraph.h"

namespace oi::detail::type_graph {

Pass RemoveTopLevelPointer::createPass() {
  auto fn = [](TypeGraph& typeGraph, NodeTracker&) {
    RemoveTopLevelPointer pass;
    pass.removeTopLevelPointers(typeGraph.rootTypes());
  };

  return Pass("RemoveTopLevelPointer", fn);
}

void RemoveTopLevelPointer::removeTopLevelPointers(
    std::vector<std::reference_wrapper<Type>>& types) {
  for (size_t i = 0; i < types.size(); i++) {
    Type& type = types[i];
    topLevelType_ = &type;
    type.accept(*this);
    types[i] = *topLevelType_;
  }
}

void RemoveTopLevelPointer::visit(Pointer& p) {
  topLevelType_ = &p.pointeeType();
}

void RemoveTopLevelPointer::visit(Reference& r) {
  topLevelType_ = &r.pointeeType();
}

}  // namespace oi::detail::type_graph
