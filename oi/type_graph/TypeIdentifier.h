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

#include <vector>

#include "NodeTracker.h"
#include "PassManager.h"
#include "Types.h"
#include "Visitor.h"
#include "oi/ContainerInfo.h"

namespace type_graph {

class TypeGraph;

/*
 * TODO Pass Name
 *
 * TODO description
 */
class TypeIdentifier : public RecursiveVisitor {
 public:
  static Pass createPass(const std::vector<ContainerInfo>& passThroughTypes);
  static bool isAllocator(Type& t);

  TypeIdentifier(NodeTracker& tracker,
                 TypeGraph& typeGraph,
                 const std::vector<ContainerInfo>& passThroughTypes)
      : tracker_(tracker),
        typeGraph_(typeGraph),
        passThroughTypes_(passThroughTypes) {
  }

  using RecursiveVisitor::accept;

  void accept(Type& type) override;
  void visit(Class& c) override;
  void visit(Container& c) override;

 private:
  NodeTracker& tracker_;
  TypeGraph& typeGraph_;
  const std::vector<ContainerInfo>& passThroughTypes_;
};

}  // namespace type_graph
