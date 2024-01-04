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
#include <string>
#include <unordered_set>
#include <vector>

#include "PassManager.h"
#include "Types.h"
#include "Visitor.h"

namespace oi::detail::type_graph {

// TODO make all final
/*
 * NameGen
 *
 * Generates unique names for all types in a type graph.
 */
class NameGen final : public RecursiveVisitor {
 public:
  static Pass createPass();

  void generateNames(const std::vector<std::reference_wrapper<Type>>& types);

  using RecursiveVisitor::accept;

  void visit(Class& c) override;
  void visit(Container& c) override;
  void visit(Enum& e) override;
  void visit(Array& a) override;
  void visit(Typedef& td) override;
  void visit(Pointer& p) override;
  void visit(Reference& r) override;
  void visit(DummyAllocator& d) override;
  void visit(CaptureKeys& d) override;
  void visit(Incomplete& i) override;

  static const inline std::string AnonPrefix = "__oi_anon";

 private:
  void accept(Type& type) override;
  void deduplicate(std::string& name);

  std::unordered_set<Type*> visited_;
  int n = 0;
};

}  // namespace oi::detail::type_graph
