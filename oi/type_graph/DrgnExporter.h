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

#include <list>
#include <string>

#include "NodeTracker.h"
#include "PassManager.h"
#include "Types.h"
#include "Visitor.h"

extern "C" {
#include <drgn.h>
}

struct TypeHierarchy;

namespace oi::detail::type_graph {

/*
 * DrgnExporter
 *
 * Converts Type Graph nodes into minimal drgn_type structs and populates a
 * TypeHierarchy with them, for use by TreeBuilder v1.
 */
class DrgnExporter : public Visitor<drgn_type*> {
 public:
  static Pass createPass(TypeHierarchy& th, std::list<drgn_type>& drgnTypes);

  DrgnExporter(TypeHierarchy& th, std::list<drgn_type>& drgnTypes)
      : th_(th), drgnTypes_(drgnTypes) {
  }

  drgn_type* accept(Type&);
  drgn_type* visit(Incomplete&) override;
  drgn_type* visit(Class&) override;
  drgn_type* visit(Container&) override;
  drgn_type* visit(Primitive&) override;
  drgn_type* visit(Enum&) override;
  drgn_type* visit(Array&) override;
  drgn_type* visit(Typedef&) override;
  drgn_type* visit(Pointer&) override;
  drgn_type* visit(Reference&) override;
  drgn_type* visit(Dummy&) override;
  drgn_type* visit(DummyAllocator&) override;
  drgn_type* visit(CaptureKeys&) override;

 private:
  drgn_type* makeDrgnType(enum drgn_type_kind kind,
                          bool is_complete,
                          enum drgn_primitive_type primitive,
                          const Type& type);

  ResultTracker<drgn_type*> tracker_;
  TypeHierarchy& th_;
  std::list<drgn_type>& drgnTypes_;
};

}  // namespace oi::detail::type_graph
