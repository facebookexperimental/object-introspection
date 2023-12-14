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
#include <unordered_map>
#include <vector>

#include "NodeTracker.h"
#include "PassManager.h"
#include "Types.h"
#include "Visitor.h"
#include "oi/ContainerInfo.h"

namespace oi::detail::type_graph {

class TypeGraph;

/*
 * IdentifyContainers
 *
 * Walks a flattened type graph and replaces type nodes based on container
 * definition TOML files.
 */
class IdentifyContainers : public RecursiveMutator {
 public:
  static Pass createPass(
      const std::vector<std::unique_ptr<ContainerInfo>>& containers);

  IdentifyContainers(
      TypeGraph& typeGraph,
      const std::vector<std::unique_ptr<ContainerInfo>>& containers);

  using RecursiveMutator::mutate;

  Type& mutate(Type& type) override;
  Type& visit(Class& c) override;
  Type& visit(Container& c) override;

 private:
  ResultTracker<Type*> tracker_;
  TypeGraph& typeGraph_;
  const std::vector<std::unique_ptr<ContainerInfo>>& containers_;
};

}  // namespace oi::detail::type_graph
