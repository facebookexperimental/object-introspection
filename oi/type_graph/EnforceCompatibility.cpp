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
#include "EnforceCompatibility.h"

#include <algorithm>
#include <cassert>

#include "AddPadding.h"
#include "Flattener.h"
#include "TypeGraph.h"
#include "TypeIdentifier.h"
#include "oi/OICodeGen.h"

namespace oi::detail::type_graph {

Pass EnforceCompatibility::createPass() {
  auto fn = [](TypeGraph& typeGraph, NodeTracker& tracker) {
    EnforceCompatibility pass{tracker};
    for (auto& type : typeGraph.rootTypes()) {
      pass.accept(type);
    }
  };

  return Pass("EnforceCompatibility", fn);
}

void EnforceCompatibility::accept(Type& type) {
  if (tracker_.visit(type))
    return;

  type.accept(*this);
}

namespace {
bool isTypeToStub(const Class& c) {
  auto it = std::ranges::find_if(OICodeGen::typesToStub,
                                 [&c](const auto& typeToStub) {
                                   return c.name().starts_with(typeToStub);
                                 });
  return it != OICodeGen::typesToStub.end();
}
}  // namespace

void EnforceCompatibility::visit(Class& c) {
  if (isTypeToStub(c)) {
    c.members.clear();
  }

  for (auto& param : c.templateParams) {
    accept(param.type());
  }
  for (auto& parent : c.parents) {
    accept(parent.type());
  }
  for (auto& member : c.members) {
    accept(member.type());
  }
  for (auto& child : c.children) {
    accept(child);
  }

  std::erase_if(c.members, [](Member member) {
    // CodeGen v1 replaces parent containers with padding
    if (member.name.starts_with(Flattener::ParentPrefix))
      return true;

    if (auto* ptr = dynamic_cast<Pointer*>(&member.type())) {
      if (auto* incomplete = dynamic_cast<Incomplete*>(&ptr->pointeeType())) {
        // This is a pointer to an incomplete type. CodeGen v1 does not record
        // the pointer's address in this case.
        return true;
      }
    }

    return false;
  });
}

}  // namespace oi::detail::type_graph
