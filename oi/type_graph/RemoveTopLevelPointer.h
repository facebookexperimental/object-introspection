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
#pragma once

#include <functional>
#include <vector>

#include "PassManager.h"
#include "Types.h"
#include "Visitor.h"

namespace oi::detail::type_graph {

/*
 * RemoveTopLevelPointer
 *
 * If the top type node is a pointer, remove it from the graph and instead have
 * the pointee type as the top-level node.
 */
class RemoveTopLevelPointer : public LazyVisitor {
 public:
  static Pass createPass();

  void removeTopLevelPointers(std::vector<std::reference_wrapper<Type>>& types);
  void visit(Pointer& p) override;
  void visit(Reference& p) override;

 private:
  Type* topLevelType_ = nullptr;
};

}  // namespace oi::detail::type_graph
