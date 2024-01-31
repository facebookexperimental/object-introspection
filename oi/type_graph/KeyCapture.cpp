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
#include "KeyCapture.h"

#include "TypeGraph.h"

namespace oi::detail::type_graph {

Pass KeyCapture::createPass(
    const std::vector<OICodeGen::Config::KeyToCapture>& keysToCapture,
    std::vector<std::unique_ptr<ContainerInfo>>& containerInfos) {
  auto fn = [&keysToCapture, &containerInfos](TypeGraph& typeGraph,
                                              NodeTracker& tracker) {
    KeyCapture pass{tracker, typeGraph, keysToCapture, containerInfos};
    pass.insertCaptureDataNodes(typeGraph.rootTypes());
  };

  return Pass("KeyCapture", fn);
}

/*
 * This function should be used as the main entry point to this pass, to add
 * special handling of top-level types.
 */
void KeyCapture::insertCaptureDataNodes(
    std::vector<std::reference_wrapper<Type>>& types) {
  for (const auto& keyToCapture : keysToCapture_) {
    if (!keyToCapture.topLevel)
      continue;

    // Capture keys from all top-level types
    for (size_t i = 0; i < types.size(); i++) {
      types[i] = captureKey(types[i]);
    }
    break;
  }

  for (const auto& type : types) {
    accept(type);
  }
}

void KeyCapture::accept(Type& type) {
  if (tracker_.visit(type))
    return;

  type.accept(*this);
}

void KeyCapture::visit(Class& c) {
  for (const auto& keyToCapture : keysToCapture_) {
    if (!keyToCapture.type.has_value() || c.name() != *keyToCapture.type)
      continue;
    if (!keyToCapture.member.has_value())
      continue;
    for (auto& member : c.members) {
      if (member.name != *keyToCapture.member)
        continue;

      member = Member{captureKey(member.type()), member};
    }
  }

  RecursiveVisitor::visit(c);
}

/*
 * captureKey
 *
 * If the given type is a container, insert a CaptureKey node above it.
 * Otherwise, just return the container node unchanged.
 *
 * Before:
 *   Container: std::map
 *     Param
 *       [KEY]
 *     Param
 *       [VAL]
 *
 * After:
 *   CaptureKeys
 *     Container: std::map
 *       Param
 *         [KEY]
 *       Param
 *         [VAL]
 */
Type& KeyCapture::captureKey(Type& type) {
  auto* container = dynamic_cast<Container*>(&(stripTypedefs(type)));
  if (!container)  // We only want to capture keys from containers
    return type;

  /*
   * Create a copy of the container info for capturing keys.
   * CodeGen and other places may deduplicate containers based on the container
   * info object, so it is necessary to create a new one when we want different
   * behaviour.
   */
  auto newContainerInfo = container->containerInfo_.clone();
  newContainerInfo.captureKeys = true;
  auto infoPtr = std::make_unique<ContainerInfo>(std::move(newContainerInfo));
  const auto& info = containerInfos_.emplace_back(std::move(infoPtr));

  // auto& captureKeysNode = typeGraph_.makeType<CaptureKeys>(*container,
  // *info);
  auto& captureKeysNode = typeGraph_.makeType<CaptureKeys>(type, *info);
  return captureKeysNode;
}

}  // namespace oi::detail::type_graph
