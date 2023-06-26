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

#include <ostream>
#include <unordered_map>

#include "Types.h"
#include "Visitor.h"

namespace type_graph {

/*
 * Printer
 */
class Printer : public ConstVisitor {
 public:
  Printer(std::ostream& out, size_t numTypes);

  void print(const Type& type);

  void visit(const Class& c) override;
  void visit(const Container& c) override;
  void visit(const Primitive& p) override;
  void visit(const Enum& e) override;
  void visit(const Array& a) override;
  void visit(const Typedef& td) override;
  void visit(const Pointer& p) override;
  void visit(const Dummy& d) override;
  void visit(const DummyAllocator& d) override;

 private:
  bool prefix(const Type* type = nullptr);
  void print_param(const TemplateParam& param);
  void print_parent(const Parent& parent);
  void print_member(const Member& member);
  void print_function(const Function& function);
  void print_child(const Type& child);
  void print_value(const std::string& value);
  void print_qualifiers(const QualifierSet& qualifiers);
  static std::string align_str(uint64_t align);

  std::ostream& out_;
  int baseIndent_;
  int depth_ = -1;
  int nextNodeNum_ = 0;
  std::unordered_map<const Type*, int> nodeNums_;
};

}  // namespace type_graph
