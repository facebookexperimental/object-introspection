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

namespace type_graph {

Printer::Printer(std::ostream& out, size_t numTypes) : out_(out) {
  // Enough space for "[XYZ] ", where XYZ is the largest node number:
  baseIndent_ = static_cast<int>(log10(static_cast<double>(numTypes)) + 1) + 3;
}

void Printer::print(Type& type) {
  depth_++;
  type.accept(*this);
  depth_--;
}

void Printer::visit(const Class& c) {
  if (prefix(&c))
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
  out_ << kind << ": " << c.name() << " (size: " << c.size()
       << align_str(c.align());
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
    print_child(child);
  }
}

void Printer::visit(const Container& c) {
  if (prefix(&c))
    return;

  out_ << "Container: " << c.name() << " (size: " << c.size() << ")"
       << std::endl;
  for (const auto& param : c.templateParams) {
    print_param(param);
  }
}

void Printer::visit(const Primitive& p) {
  prefix();
  out_ << "Primitive: " << p.name() << std::endl;
}

void Printer::visit(const Enum& e) {
  prefix();
  out_ << "Enum: " << e.name() << " (size: " << e.size() << ")" << std::endl;
}

void Printer::visit(const Array& a) {
  if (prefix(&a))
    return;

  out_ << "Array: (length: " << a.len() << ")" << std::endl;
  print(*a.elementType());
}

void Printer::visit(const Typedef& td) {
  if (prefix(&td))
    return;

  out_ << "Typedef: " << td.name() << std::endl;
  print(*td.underlyingType());
}

void Printer::visit(const Pointer& p) {
  if (prefix(&p))
    return;

  out_ << "Pointer" << std::endl;
  print(*p.pointeeType());
}

void Printer::visit(const Dummy& d) {
  prefix();
  out_ << "Dummy (size: " << d.size() << align_str(d.align()) << ")"
       << std::endl;
}

void Printer::visit(const DummyAllocator& d) {
  prefix();
  out_ << "DummyAllocator (size: " << d.size() << align_str(d.align()) << ")"
       << std::endl;
  print(d.allocType());
}

bool Printer::prefix(const Type* type) {
  int indent = baseIndent_ + depth_ * 2;

  if (type) {
    if (auto it = nodeNums_.find(type); it != nodeNums_.end()) {
      // Node has already been printed - print a reference to it this time
      out_ << std::string(indent, ' ');
      int nodeNum = it->second;
      out_ << "[" << nodeNum << "]" << std::endl;
      return true;
    }

    int nodeNum = nextNodeNum_++;
    std::string nodeId = "[" + std::to_string(nodeNum) + "]";
    out_ << nodeId;
    indent -= nodeId.size();
    nodeNums_.insert({type, nodeNum});
  }

  out_ << std::string(indent, ' ');
  return false;
}

void Printer::print_param(const TemplateParam& param) {
  depth_++;
  prefix();
  out_ << "Param" << std::endl;
  if (param.value) {
    print_value(*param.value);
  } else {
    print(*param.type);
  }
  depth_--;
}

void Printer::print_parent(const Parent& parent) {
  depth_++;
  prefix();
  out_ << "Parent (offset: " << parent.offset << ")" << std::endl;
  print(*parent.type);
  depth_--;
}

void Printer::print_member(const Member& member) {
  depth_++;
  prefix();
  out_ << "Member: " << member.name << " (offset: " << member.offset
       << align_str(member.align) << ")" << std::endl;
  print(*member.type);
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

void Printer::print_child(Type& child) {
  depth_++;
  prefix();
  out_ << "Child:" << std::endl;
  print(child);
  depth_--;
}

void Printer::print_value(const std::string& value) {
  depth_++;
  prefix();
  out_ << "Value: " << value << std::endl;
  depth_--;
}

std::string Printer::align_str(uint64_t align) {
  if (align == 0)
    return "";
  return ", align: " + std::to_string(align);
}

}  // namespace type_graph
