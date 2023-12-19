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
#include "Printer.h"

#include <cmath>

namespace oi::detail::type_graph {

Printer::Printer(std::ostream& out, NodeTracker& tracker, size_t numTypes)
    : tracker_(tracker), out_(out) {
  if (numTypes == 0) {
    baseIndent_ = 0;
    return;
  }

  // Enough space for "[XYZ] ", where XYZ is the largest node number:
  baseIndent_ = static_cast<int>(log10(static_cast<double>(numTypes)) + 1) + 3;
}

void Printer::print(const Type& type) {
  depth_++;
  type.accept(*this);
  depth_--;
}

void Printer::visit(const Incomplete& i) {
  prefix();
  out_ << "Incomplete";
  if (auto underlyingType = i.underlyingType()) {
    out_ << std::endl;
    print(underlyingType.value().get());
  } else {
    out_ << ": [" << i.inputName() << "]" << std::endl;
  }
}

void Printer::visit(const Class& c) {
  if (prefix(c))
    return;

  std::string kind;
  switch (c.kind()) {
    case Class::Kind::Class:
      kind = "Class";
      break;
    case Class::Kind::Struct:
      kind = "Struct";
      break;
    case Class::Kind::Union:
      kind = "Union";
      break;
  }
  std::string name = c.name();
  out_ << kind << ": " << name;
  if (auto inp = c.inputName(); inp != name)
    out_ << " [" << inp << "]";
  out_ << " (size: " << c.size() << align_str(c.align());
  if (c.packed()) {
    out_ << ", packed";
  }
  out_ << ")" << std::endl;
  for (const auto& param : c.templateParams) {
    print_param(param);
  }
  for (const auto& parent : c.parents) {
    print_parent(parent);
  }
  for (const auto& member : c.members) {
    print_member(member);
  }
  for (const auto& function : c.functions) {
    print_function(function);
  }
  for (auto& child : c.children) {
    print_type("Child", child);
  }
}

void Printer::visit(const Container& c) {
  if (prefix(c))
    return;

  out_ << "Container: " << c.name() << " (size: " << c.size()
       << align_str(c.align()) << ")" << std::endl;
  for (const auto& param : c.templateParams) {
    print_param(param);
  }
  if (c.underlying()) {
    print_type("Underlying", *c.underlying());
  }
}

void Printer::visit(const Primitive& p) {
  prefix();
  out_ << "Primitive: " << p.name() << std::endl;
}

void Printer::visit(const Enum& e) {
  prefix();
  out_ << "Enum: " << e.name() << " (size: " << e.size() << ")" << std::endl;
  for (const auto& [val, name] : e.enumerators()) {
    print_enumerator(val, name);
  }
}

void Printer::visit(const Array& a) {
  if (prefix(a))
    return;

  out_ << "Array: ";
  if (auto inp = a.inputName(); !inp.empty())
    out_ << "[" << inp << "] ";
  out_ << "(length: " << a.len() << ")" << std::endl;
  print(a.elementType());
}

void Printer::visit(const Typedef& td) {
  if (prefix(td))
    return;

  auto name = td.name();
  out_ << "Typedef: " << name;
  if (auto inp = td.inputName(); inp != name)
    out_ << " [" << inp << "]";
  out_ << std::endl;
  print(td.underlyingType());
}

void Printer::visit(const Pointer& p) {
  if (prefix(p))
    return;

  out_ << "Pointer";
  if (auto inp = p.inputName(); !inp.empty())
    out_ << " [" << inp << "]";
  out_ << std::endl;
  print(p.pointeeType());
}

void Printer::visit(const Reference& r) {
  if (prefix(r))
    return;

  out_ << "Reference";
  if (auto inp = r.inputName(); !inp.empty())
    out_ << " [" << inp << "]";
  out_ << std::endl;
  print(r.pointeeType());
}

void Printer::visit(const Dummy& d) {
  if (prefix(d))
    return;
  out_ << "Dummy ";
  out_ << "[" << d.inputName() << "] ";
  out_ << "(size: " << d.size() << align_str(d.align()) << ")" << std::endl;
}

void Printer::visit(const DummyAllocator& d) {
  if (prefix(d))
    return;
  out_ << "DummyAllocator ";
  out_ << "[" << d.inputName() << "] ";
  out_ << "(size: " << d.size() << align_str(d.align()) << ")" << std::endl;
  print(d.allocType());
}

void Printer::visit(const CaptureKeys& d) {
  prefix();
  out_ << "CaptureKeys" << std::endl;
  print(d.container());
}

void Printer::prefix() {
  int indent = baseIndent_ + depth_ * 2;
  out_ << std::string(indent, ' ');
}

bool Printer::prefix(const Type& type) {
  int indent = baseIndent_ + depth_ * 2;

  if (tracker_.visit(type)) {
    // Node has already been printed - print a reference to it this time
    out_ << std::string(indent, ' ');
    out_ << "[" << type.id() << "]" << std::endl;
    return true;
  }

  std::string nodeId = "[" + std::to_string(type.id()) + "]";
  out_ << nodeId;
  indent -= nodeId.size();

  out_ << std::string(indent, ' ');
  return false;
}

void Printer::print_param(const TemplateParam& param) {
  depth_++;
  prefix();
  out_ << "Param" << std::endl;
  if (param.value) {
    print_value(*param.value);
  }
  print(param.type());
  print_qualifiers(param.qualifiers);
  depth_--;
}

void Printer::print_parent(const Parent& parent) {
  depth_++;
  prefix();
  out_ << "Parent (offset: " << static_cast<double>(parent.bitOffset) / 8 << ")"
       << std::endl;
  print(parent.type());
  depth_--;
}

void Printer::print_member(const Member& member) {
  depth_++;
  prefix();
  out_ << "Member: " << member.name;
  if (member.inputName != member.name && !member.inputName.empty())
    out_ << " [" << member.inputName << "]";
  out_ << " (offset: " << static_cast<double>(member.bitOffset) / 8;
  out_ << align_str(member.align);
  if (member.bitsize != 0) {
    out_ << ", bitsize: " << member.bitsize;
  }
  out_ << ")" << std::endl;
  print(member.type());
  depth_--;
}

void Printer::print_function(const Function& function) {
  depth_++;
  prefix();
  out_ << "Function: " << function.name;
  if (function.virtuality != 0)
    out_ << " (virtual)";
  out_ << std::endl;
  depth_--;
}

void Printer::print_type(std::string_view header, const Type& type) {
  depth_++;
  prefix();
  out_ << header << std::endl;
  print(type);
  depth_--;
}

void Printer::print_value(const std::string& value) {
  depth_++;
  prefix();
  out_ << "Value: " << value << std::endl;
  depth_--;
}

void Printer::print_qualifiers(const QualifierSet& qualifiers) {
  if (qualifiers.none()) {
    return;
  }
  depth_++;
  prefix();
  out_ << "Qualifiers:";
  if (qualifiers[Qualifier::Const]) {
    out_ << " const";
  }
  out_ << std::endl;
  depth_--;
}

void Printer::print_enumerator(int64_t val, const std::string& name) {
  depth_++;
  prefix();
  out_ << "Enumerator: " << val << ":" << name << std::endl;
  depth_--;
}

std::string Printer::align_str(uint64_t align) {
  if (align == 0)
    return "";
  return ", align: " + std::to_string(align);
}

}  // namespace oi::detail::type_graph
