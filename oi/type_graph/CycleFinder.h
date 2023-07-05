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

#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "PassManager.h"
#include "Types.h"
#include "Visitor.h"

namespace type_graph {

class TypeGraph;

/*
 * CycleFinder
 *
 * Find a set of edges that must be broken in order to convert the type graph
 * from a directed graph to a directed acyclic graph (DAG).
 */
class CycleFinder : public RecursiveVisitor {
 public:
  static Pass createPass();

  CycleFinder(TypeGraph& typeGraph) : typeGraph_(typeGraph) {
  }

  using RecursiveVisitor::accept;
  using RecursiveVisitor::visit;

  void accept(Type& type) override;

  void visit(CycleBreaker&) override{
      // Do not follow a cyclebreaker as it has already been followed
  };

 private:
  void breakCycle(std::span<std::reference_wrapper<Type>>);
  bool push(Type& t);
  void pop();
  CycleBreaker& edge(Type& t);

  TypeGraph& typeGraph_;

  std::vector<std::reference_wrapper<Type>> currentPath_;
  std::unordered_set<const Type*> currentPathSet_;
  std::unordered_map<const Type*, std::reference_wrapper<CycleBreaker>>
      replacements_;
};

}  // namespace type_graph
