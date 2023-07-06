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

#include <string>
#include <unordered_set>
#include <vector>

#include "PassManager.h"
#include "Types.h"
#include "Visitor.h"

namespace type_graph {

/*
 * Flattener
 *
 * Flattens classes by removing parents and adding their members directly into
 * derived classes.
 */
class Flattener : public RecursiveVisitor {
 public:
  static Pass createPass();

  void flatten(std::vector<std::reference_wrapper<Type>>& types);

  using RecursiveVisitor::visit;

  void visit(Type& type) override;
  void visit(Class& c) override;
  void visit(Container& c) override;

  static const inline std::string ParentPrefix = "__oi_parent";

 private:
  std::unordered_set<Type*> visited_;
  std::vector<Member> flattened_members_;
  std::vector<uint64_t> offset_stack_;
};

}  // namespace type_graph
