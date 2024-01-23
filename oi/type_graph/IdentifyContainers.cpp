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
#include "IdentifyContainers.h"

#include "TypeGraph.h"
#include "oi/ContainerInfo.h"

namespace oi::detail::type_graph {

Pass IdentifyContainers::createPass(
    const std::vector<std::unique_ptr<ContainerInfo>>& containers) {
  auto fn = [&containers](TypeGraph& typeGraph, NodeTracker&) {
    IdentifyContainers typeId{typeGraph, containers};
    for (auto& type : typeGraph.rootTypes()) {
      type = typeId.mutate(type);
    }
  };

  return Pass("IdentifyContainers", fn);
}

IdentifyContainers::IdentifyContainers(
    TypeGraph& typeGraph,
    const std::vector<std::unique_ptr<ContainerInfo>>& containers)
    : tracker_(typeGraph.size()),
      typeGraph_(typeGraph),
      containers_(containers) {
}

Type& IdentifyContainers::mutate(Type& type) {
  if (Type* mutated = tracker_.get(type))
    return *mutated;

  Type& mutated = type.accept(*this);
  tracker_.set(type, &mutated);
  return mutated;
}

Type& IdentifyContainers::visit(Class& c) {
  for (const auto& containerInfo : containers_) {
    if (!containerInfo->matches(c.fqName())) {
      continue;
    }

    auto& container =
        typeGraph_.makeType<Container>(*containerInfo, c.size(), &c);
    container.templateParams = c.templateParams;

    tracker_.set(c, &container);
    visit(container);
    return container;
  }

  tracker_.set(c, &c);
  RecursiveMutator::visit(c);
  return c;
}

Type& IdentifyContainers::visit(Container& c) {
  for (auto& param : c.templateParams) {
    param.setType(mutate(param.type()));
  }

  // Do not mutate the underlying class further here as that would result in it
  // getting replaced with this Container node

  return c;
}

}  // namespace oi::detail::type_graph
