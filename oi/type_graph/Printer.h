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

#include "NodeTracker.h"
#include "Types.h"
#include "Visitor.h"

namespace oi::detail::type_graph {

/*
 * Printer
 */
class Printer : public ConstVisitor {
 public:
  Printer(std::ostream& out, NodeTracker& tracker, size_t numTypes);

  void print(const Type& type);

  void visit(const Incomplete& i) override;
  void visit(const Class& c) override;
  void visit(const Container& c) override;
  void visit(const Primitive& p) override;
  void visit(const Enum& e) override;
  void visit(const Array& a) override;
  void visit(const Typedef& td) override;
  void visit(const Pointer& p) override;
  void visit(const Reference& r) override;
  void visit(const Dummy& d) override;
  void visit(const DummyAllocator& d) override;
  void visit(const CaptureKeys& d) override;

 private:
  void prefix();
  [[nodiscard]] bool prefix(const Type& type);
  void print_param(const TemplateParam& param);
  void print_parent(const Parent& parent);
  void print_member(const Member& member);
  void print_function(const Function& function);
  void print_type(std::string_view header, const Type& type);
  void print_value(const std::string& value);
  void print_qualifiers(const QualifierSet& qualifiers);
  void print_enumerator(int64_t val, const std::string& name);
  static std::string align_str(uint64_t align);

  NodeTracker& tracker_;
  std::ostream& out_;
  int baseIndent_;
  int depth_ = -1;
};

}  // namespace oi::detail::type_graph
