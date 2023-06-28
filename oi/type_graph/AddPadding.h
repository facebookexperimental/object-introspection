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
#include <unordered_set>

#include "PassManager.h"
#include "Types.h"
#include "Visitor.h"

namespace type_graph {

class TypeGraph;

/*
 * AddPadding
 *
 * Adds members to classes to represent padding. This is necessary until we have
 * complete alignment information from DWARF, otherwise our classes may be
 * undersized.
 */
class AddPadding final : public RecursiveVisitor {
 public:
  static Pass createPass();

  explicit AddPadding(TypeGraph& typeGraph) : typeGraph_(typeGraph) {
  }

  using RecursiveVisitor::visit;

  void visit(Type& type) override;
  void visit(Class& c) override;

  static const inline std::string MemberPrefix = "__oid_padding";

 private:
  std::unordered_set<Type*> visited_;
  TypeGraph& typeGraph_;

  void addPadding(const Member& prevMember,
                  uint64_t paddingEndBits,
                  std::vector<Member>& paddedMembers);
};

}  // namespace type_graph
