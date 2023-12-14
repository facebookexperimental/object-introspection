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
#include "TypeIdentifier.h"

#include "TypeGraph.h"
#include "oi/ContainerInfo.h"

namespace oi::detail::type_graph {

Pass TypeIdentifier::createPass(
    const std::vector<ContainerInfo>& passThroughTypes) {
  auto fn = [&passThroughTypes](TypeGraph& typeGraph, NodeTracker& tracker) {
    TypeIdentifier typeId{tracker, typeGraph, passThroughTypes};
    for (auto& type : typeGraph.rootTypes()) {
      typeId.accept(type);
    }
  };

  return Pass("TypeIdentifier", fn);
}

bool TypeIdentifier::isAllocator(Type& t) {
  auto* c = dynamic_cast<Class*>(&t);
  if (!c)
    return false;

  // Maybe add more checks for an allocator.
  // For now, just test for the presence of an "allocate" function
  for (const auto& func : c->functions) {
    if (func.name == "allocate") {
      return true;
    }
  }
  return false;
}

void TypeIdentifier::accept(Type& type) {
  if (tracker_.visit(type))
    return;

  type.accept(*this);
}

void TypeIdentifier::visit(Container& c) {
  const auto& stubParams = c.containerInfo_.stubTemplateParams;
  // TODO these two arrays could be looped over in sync for better performance
  for (size_t i = 0; i < c.templateParams.size(); i++) {
    const auto& param = c.templateParams[i];
    if (dynamic_cast<Dummy*>(&param.type()) ||
        dynamic_cast<DummyAllocator*>(&param.type()) ||
        dynamic_cast<Container*>(&param.type())) {
      // In case the TypeIdentifier pass is run multiple times, we don't want to
      // replace dummies again as the context of the original replacement has
      // been lost.
      continue;
    }

    if (Class* paramClass = dynamic_cast<Class*>(&param.type())) {
      bool replaced = false;
      for (const auto& info : passThroughTypes_) {
        if (std::regex_search(paramClass->fqName(), info.matcher)) {
          // Create dummy containers. Use a map so previously deduplicated nodes
          // remain deduplicated.
          Container* dummy;
          if (auto it = passThroughTypeDummys_.find(paramClass->id());
              it != passThroughTypeDummys_.end()) {
            dummy = &it->second.get();
          } else {
            dummy = &typeGraph_.makeType<Container>(
                info, param.type().size(), paramClass);
            dummy->templateParams = paramClass->templateParams;
            passThroughTypeDummys_.insert(it,
                                          {paramClass->id(), std::ref(*dummy)});
          }
          c.templateParams[i] = *dummy;
          replaced = true;
          break;
        }
      }

      if (replaced) {
        continue;
      }
    }

    if (std::find(stubParams.begin(), stubParams.end(), i) !=
        stubParams.end()) {
      size_t size = param.type().size();
      if (size == 1) {
        // Hack: when we get a reported size of 1 for these parameters, it
        // turns out that a size of 0 is actually expected.
        size = 0;
      }

      if (isAllocator(param.type())) {
        auto* allocator = dynamic_cast<Class*>(
            &param.type());  // TODO please don't do this...
        Type& typeToAllocate = allocator->templateParams.at(0).type();
        auto& dummy = typeGraph_.makeType<DummyAllocator>(
            typeToAllocate, size, param.type().align(), allocator->name());
        c.templateParams[i] = dummy;
      } else {
        auto& dummy = typeGraph_.makeType<Dummy>(
            size, param.type().align(), param.type().name());
        c.templateParams[i] = dummy;
      }
    }
  }

  for (const auto& param : c.templateParams) {
    accept(param.type());
  }
}

}  // namespace oi::detail::type_graph
