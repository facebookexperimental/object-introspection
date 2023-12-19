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
#include "NameGen.h"

#include "TypeGraph.h"

template <typename T>
using ref = std::reference_wrapper<T>;

namespace oi::detail::type_graph {

Pass NameGen::createPass() {
  auto fn = [](TypeGraph& typeGraph, NodeTracker&) {
    NameGen nameGen;
    nameGen.generateNames(typeGraph.rootTypes());
  };

  return Pass("NameGen", fn);
}

void NameGen::generateNames(const std::vector<ref<Type>>& types) {
  for (auto& type : types) {
    accept(type);
  }
};

void NameGen::accept(Type& type) {
  if (visited_.count(&type) != 0)
    return;

  visited_.insert(&type);
  type.accept(*this);
}

namespace {
/*
 * Remove template parameters from the type name
 *
 * "std::vector<int>" -> "std::vector"
 */
void removeTemplateParams(std::string& name) {
  auto template_start_pos = name.find('<');
  if (template_start_pos != std::string::npos)
    name.erase(template_start_pos);
}
}  // namespace

void NameGen::deduplicate(std::string& name) {
  if (name == "")
    name = AnonPrefix;

  // Append an incrementing number to ensure we don't get duplicates
  name += "_";
  name += std::to_string(n++);
}

void NameGen::visit(Class& c) {
  std::string name = c.name();
  removeTemplateParams(name);
  deduplicate(name);
  if (c.name().empty())
    c.setInputName(name);
  c.setName(name);

  // Deduplicate member names. Duplicates may be present after flattening.
  for (size_t i = 0; i < c.members.size(); i++) {
    if (c.members[i].name.empty())
      c.members[i].name = AnonPrefix;

    c.members[i].name += "_" + std::to_string(i);

    if (c.members[i].inputName.empty())
      c.members[i].inputName = c.members[i].name;

    // GCC includes dots in vptr member names, e.g. "_vptr.MyClass"
    // These aren't valid in C++, so we must replace them
    std::replace(c.members[i].name.begin(), c.members[i].name.end(), '.', '$');
  }

  for (const auto& param : c.templateParams) {
    accept(param.type());
  }
  for (const auto& parent : c.parents) {
    accept(parent.type());
  }
  for (const auto& member : c.members) {
    accept(member.type());
  }
  for (const auto& child : c.children) {
    accept(child);
  }
}

void NameGen::visit(Container& c) {
  if (c.templateParams.empty()) {
    return;
  }

  for (const auto& template_param : c.templateParams) {
    accept(template_param.type());
  }

  std::string name = c.name();
  std::string inputName{c.inputName()};

  removeTemplateParams(name);
  removeTemplateParams(inputName);

  name += '<';
  inputName += '<';

  bool first = true;
  for (const auto& param : c.templateParams) {
    if (!first) {
      name += ", ";
      inputName += ", ";
    }
    first = false;

    if (param.value) {
      name += *param.value;
      inputName += *param.value;
    } else {
      name += param.type().name();
      inputName += param.type().inputName();
      // The "const" keyword must come after the type name so that pointers are
      // handled correctly.
      //
      // When we're told that we have a const member with type "Foo*", we want
      // "Foo* const", meaning const pointer to Foo, rather than "const Foo*",
      // meaning pointer to const Foo.
      if (param.qualifiers[Qualifier::Const]) {
        name += " const";
        inputName += " const";
      }
    }
  }

  name += '>';
  inputName += '>';

  c.setName(name);
  c.setInputName(inputName);
}

void NameGen::visit(Enum& e) {
  std::string name = e.name();
  deduplicate(name);
  if (e.name().empty())
    e.setInputName(name);
  e.setName(name);
}

void NameGen::visit(Array& a) {
  RecursiveVisitor::visit(a);

  a.regenerateName();
  std::string name{a.elementType().inputName()};
  name += "[" + std::to_string(a.len()) + "]";
  a.setInputName(name);
}

void NameGen::visit(Typedef& td) {
  /*
   * Treat like class names.
   *
   * We must remove template parameters from typedef names because, although
   * they won't have real (in DWARF) template parameters themselves, their names
   * may still contain other type names surrounded by angle brackets.
   *
   * An alias template, e.g. std::conditional_t, will have this behaviour:
   *     conditional_t<B,T,F> is a typedef for conditional<B,T,F>::type
   */
  std::string name = td.name();
  removeTemplateParams(name);
  deduplicate(name);
  td.setName(name);

  accept(td.underlyingType());
}

void NameGen::visit(Pointer& p) {
  RecursiveVisitor::visit(p);
  p.regenerateName();
  std::string inputName{p.pointeeType().inputName()};
  inputName += '*';
  p.setInputName(inputName);
}

void NameGen::visit(Reference& r) {
  RecursiveVisitor::visit(r);

  r.regenerateName();
  std::string inputName{r.pointeeType().inputName()};
  inputName += '&';
  r.setInputName(inputName);
}

void NameGen::visit(DummyAllocator& d) {
  RecursiveVisitor::visit(d);
  d.regenerateName();
}

void NameGen::visit(CaptureKeys& c) {
  RecursiveVisitor::visit(c);
  c.regenerateName();
}

}  // namespace oi::detail::type_graph
