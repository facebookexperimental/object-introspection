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
#include <memory>
#include <vector>

#include "NodeTracker.h"
#include "PassManager.h"
#include "Types.h"
#include "Visitor.h"
#include "oi/OICodeGen.h"

namespace oi::detail::type_graph {

/*
 * KeyCapture
 *
 * Marks containers for which the user has requested key-capture.
 */
class KeyCapture : public RecursiveVisitor {
 public:
  static Pass createPass(
      const std::vector<OICodeGen::Config::KeyToCapture>& keysToCapture,
      std::vector<std::unique_ptr<ContainerInfo>>& containerInfos);

  KeyCapture(NodeTracker& tracker,
             TypeGraph& typeGraph,
             const std::vector<OICodeGen::Config::KeyToCapture>& keysToCapture,
             std::vector<std::unique_ptr<ContainerInfo>>& containerInfos)
      : tracker_(tracker),
        typeGraph_(typeGraph),
        keysToCapture_(keysToCapture),
        containerInfos_(containerInfos) {
  }

  using RecursiveVisitor::accept;
  using RecursiveVisitor::visit;

  void insertCaptureDataNodes(std::vector<std::reference_wrapper<Type>>& types);
  void visit(Class& c) override;

 private:
  NodeTracker& tracker_;
  TypeGraph& typeGraph_;
  const std::vector<OICodeGen::Config::KeyToCapture>& keysToCapture_;
  std::vector<std::unique_ptr<ContainerInfo>>& containerInfos_;

  void accept(Type& type) override;
  Type& captureKey(Type& type);
};

}  // namespace oi::detail::type_graph
