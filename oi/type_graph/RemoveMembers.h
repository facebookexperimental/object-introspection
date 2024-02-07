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

#include <unordered_set>

#include "PassManager.h"
#include "Types.h"
#include "Visitor.h"

namespace oi::detail::type_graph {

/*
 * RemoveMembers
 *
 * Removes members as requested by the [[codegen.ignore]] section in the OI
 * config.
 *
 * Removes members from unions as we have no way of telling which one is active.
 */
class RemoveMembers : public RecursiveVisitor {
 public:
  static Pass createPass(
      const std::vector<std::pair<std::string, std::string>>& membersToIgnore);

  RemoveMembers(
      const std::vector<std::pair<std::string, std::string>>& membersToIgnore)
      : membersToIgnore_(membersToIgnore) {
  }

  using RecursiveVisitor::accept;

  void accept(Type& type) override;
  void visit(Class& c) override;

 private:
  bool ignoreMember(std::string_view typeName,
                    const std::string& memberName) const;

  std::unordered_set<Type*> visited_;
  const std::vector<std::pair<std::string, std::string>>& membersToIgnore_;
};

}  // namespace oi::detail::type_graph
