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

namespace type_graph {

Pass TypeIdentifier::createPass() {
  auto fn = [](TypeGraph& typeGraph) {
    TypeIdentifier typeId{typeGraph};
    for (auto& type : typeGraph.rootTypes()) {
      typeId.visit(type);
    }
  };

  return Pass("TypeIdentifier", fn);
}

void TypeIdentifier::visit(Type& type) {
  if (visited_.count(&type) != 0)
    return;

  visited_.insert(&type);
  type.accept(*this);
}

namespace {
bool isAllocator(Type* t) {
  auto* c = dynamic_cast<Class*>(t);
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
}  // namespace

void TypeIdentifier::visit(Container& c) {
  const auto& stubParams = c.containerInfo_.stubTemplateParams;
  // TODO these two arrays could be looped over in sync for better performance
  for (size_t i = 0; i < c.templateParams.size(); i++) {
    const auto& param = c.templateParams[i];
    if (dynamic_cast<Dummy*>(param.type) ||
        dynamic_cast<DummyAllocator*>(param.type)) {
      // In case the TypeIdentifier pass is run multiple times, we don't want to
      // replace dummies again as the context of the original replacement has
      // been lost.
      continue;
    }

    if (std::find(stubParams.begin(), stubParams.end(), i) !=
        stubParams.end()) {
      size_t size = param.type->size();
      if (size == 1) {
        // Hack: when we get a reported size of 1 for these parameters, it
        // turns out that a size of 0 is actually expected.
        size = 0;
      }

      if (isAllocator(param.type)) {
        auto* allocator =
            dynamic_cast<Class*>(param.type);  // TODO please don't do this...
        Type* typeToAllocate;
        if (!allocator->templateParams.empty()) {
          typeToAllocate = allocator->templateParams.at(0).type;
        } else {
          // We've encountered some bad DWARF. Let's guess that the type to
          // allocate is the first parameter of the container.
          typeToAllocate = c.templateParams[0].type;
        }
        auto* dummy = typeGraph_.make_type<DummyAllocator>(
            *typeToAllocate, size, param.type->align());
        c.templateParams[i] = dummy;
      } else {
        auto* dummy = typeGraph_.make_type<Dummy>(size, param.type->align());
        c.templateParams[i] = dummy;
      }
    }
  }

  for (const auto& param : c.templateParams) {
    visit(*param.type);
  }
}

}  // namespace type_graph
