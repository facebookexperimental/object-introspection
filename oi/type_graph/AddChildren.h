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
#include <vector>

#include "PassManager.h"
#include "Types.h"
#include "Visitor.h"

class SymbolService;
struct drgn_type;

namespace type_graph {

class DrgnParser;
class TypeGraph;

/*
 * AddChildren
 *
 * Populates the "children" field of Class types.
 *
 * DWARF only stores a mapping of [child -> parent], but sometimes we want the
 * invserse: [parent -> child]. We must iterate over every single type to build
 * up our own inverse mapping, before applying it to the relevant type nodes.
 *
 * This is expensive and only useful for types which make use of dynamic
 * inheritance hierarchies (e.g. polymorphism), so is not done as part of the
 * standard DrgnParser stage.
 */
class AddChildren final : public RecursiveVisitor {
 public:
  static Pass createPass(DrgnParser& drgnParser, SymbolService& symbols);

  AddChildren(TypeGraph& typeGraph, DrgnParser& drgnParser)
      : typeGraph_(typeGraph), drgnParser_(drgnParser) {
  }

  using RecursiveVisitor::accept;

  void accept(Type& type) override;
  void visit(Class& c) override;

 private:
  void enumerateChildClasses(SymbolService& symbols);
  void enumerateClassChildren(
      struct drgn_type* type,
      std::vector<std::reference_wrapper<Class>>& children);
  void recordChildren(drgn_type* type);

  std::unordered_set<Type*> visited_;
  TypeGraph& typeGraph_;
  DrgnParser& drgnParser_;

  // Mapping of parent classes to child classes, using names for keys, as drgn
  // pointers returned from a type iterator will not match those returned from
  // enumerating types in the normal way.
  std::unordered_map<std::string, std::vector<drgn_type*>> childClasses_;
};

}  // namespace type_graph
