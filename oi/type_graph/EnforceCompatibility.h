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

namespace type_graph {

/*
 * EnforceCompatibility
 *
 * Transforms the type graph so that CodeGen produces code which is compatible
 * with OICodeGen.
 */
class EnforceCompatibility : public RecursiveVisitor {
 public:
  static Pass createPass();

  EnforceCompatibility(NodeTracker& tracker) : tracker_(tracker) {
  }

  using RecursiveVisitor::accept;

  void accept(Type& type) override;
  void visit(Class& c) override;

 private:
  NodeTracker& tracker_;
};

}  // namespace type_graph
