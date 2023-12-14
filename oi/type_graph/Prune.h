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
#include <vector>

#include "NodeTracker.h"
#include "PassManager.h"
#include "Types.h"
#include "Visitor.h"

namespace oi::detail::type_graph {

/*
 * Prune
 *
 * Removes unnecessary information from a type graph and releases memory where
 * possible.
 */
class Prune : public RecursiveVisitor {
 public:
  static Pass createPass();

  Prune(NodeTracker& tracker) : tracker_(tracker) {
  }

  using RecursiveVisitor::accept;
  using RecursiveVisitor::visit;

  void accept(Type& type) override;
  void visit(Class& c) override;
  void visit(Container& c) override;

 private:
  NodeTracker& tracker_;
};

}  // namespace oi::detail::type_graph
