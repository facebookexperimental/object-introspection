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
#include "RemoveMembers.h"

#include "AddPadding.h"
#include "TypeGraph.h"

namespace oi::detail::type_graph {

Pass RemoveMembers::createPass(
    const std::vector<std::pair<std::string, std::string>>& membersToIgnore) {
  auto fn = [&membersToIgnore](TypeGraph& typeGraph, NodeTracker&) {
    RemoveMembers removeMembers{membersToIgnore};
    for (auto& type : typeGraph.rootTypes()) {
      removeMembers.accept(type);
    }
  };

  return Pass("RemoveMembers", fn);
}

void RemoveMembers::accept(Type& type) {
  if (visited_.count(&type) != 0)
    return;

  visited_.insert(&type);
  type.accept(*this);
}

void RemoveMembers::visit(Class& c) {
  if (c.kind() == Class::Kind::Union) {
    // In general, we can't tell which member is active in a union, so it is not
    // safe to try and measure any of them.
    c.members.clear();
  }

  for (const auto& param : c.templateParams) {
    accept(param.type());
  }
  for (const auto& parent : c.parents) {
    accept(parent.type());
  }
  for (const auto& mem : c.members) {
    accept(mem.type());
  }
  for (const auto& child : c.children) {
    accept(child);
  }

  std::erase_if(c.members, [this, &c](Member member) {
    if (ignoreMember(c.inputName(), member.name))
      return true;
    if (dynamic_cast<Incomplete*>(&member.type()))
      return true;
    return false;
  });
}

bool RemoveMembers::ignoreMember(std::string_view typeName,
                                 const std::string& memberName) const {
  for (const auto& [ignoredType, ignoredMember] : membersToIgnore_) {
    if (typeName == ignoredType &&
        (memberName == ignoredMember || ignoredMember == "*")) {
      return true;
    }
  }
  return false;
}

}  // namespace oi::detail::type_graph
