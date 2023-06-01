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
#include "RemoveIgnored.h"

#include "TypeGraph.h"

namespace type_graph {

Pass RemoveIgnored::createPass(
    const std::vector<std::pair<std::string, std::string>>& membersToIgnore) {
  auto fn = [&membersToIgnore](TypeGraph& typeGraph) {
    RemoveIgnored removeIgnored{typeGraph, membersToIgnore};
    for (auto& type : typeGraph.rootTypes()) {
      removeIgnored.visit(type);
    }
  };

  return Pass("RemoveIgnored", fn);
}

void RemoveIgnored::visit(Type& type) {
  if (visited_.count(&type) != 0)
    return;

  visited_.insert(&type);
  type.accept(*this);
}

void RemoveIgnored::visit(Class& c) {
  for (size_t i = 0; i < c.members.size(); i++) {
    if (!ignoreMember(c.name(), c.members[i].name)) {
      continue;
    }
    auto* primitive = typeGraph_.make_type<Primitive>(Primitive::Kind::Int8);
    auto* paddingArray =
        typeGraph_.make_type<Array>(primitive, c.members[i].type->size());
    c.members[i] = Member{paddingArray, c.members[i].name, c.members[i].offset};
  }
}

bool RemoveIgnored::ignoreMember(const std::string& typeName,
                                 const std::string& memberName) const {
  for (const auto& [ignoredType, ignoredMember] : membersToIgnore_) {
    if (typeName == ignoredType && memberName == ignoredMember) {
      return true;
    }
  }
  return false;
}

}  // namespace type_graph
