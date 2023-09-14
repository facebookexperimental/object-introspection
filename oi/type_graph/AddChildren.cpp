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
#include "AddChildren.h"

#include <cassert>

#include "DrgnParser.h"
#include "TypeGraph.h"
#include "oi/DrgnUtils.h"
#include "oi/SymbolService.h"

extern "C" {
#include <drgn.h>
}

template <typename T>
using ref = std::reference_wrapper<T>;

namespace oi::detail::type_graph {

Pass AddChildren::createPass(DrgnParser& drgnParser, SymbolService& symbols) {
  auto fn = [&drgnParser, &symbols](TypeGraph& typeGraph, NodeTracker&) {
    AddChildren pass(typeGraph, drgnParser);
    pass.enumerateChildClasses(symbols);
    for (auto& type : typeGraph.rootTypes()) {
      pass.accept(type);
    }
  };

  return Pass("AddChildren", fn);
}

void AddChildren::accept(Type& type) {
  if (visited_.count(&type) != 0)
    return;

  visited_.insert(&type);
  type.accept(*this);
}

void AddChildren::visit(Class& c) {
  for (auto& param : c.templateParams) {
    accept(param.type());
  }
  for (auto& member : c.members) {
    accept(member.type());
  }

  if (!c.isDynamic()) {
    return;
  }

  auto it = childClasses_.find(c.name());
  if (it == childClasses_.end()) {
    return;
  }

  const auto& drgnChildren = it->second;
  for (drgn_type* drgnChild : drgnChildren) {
    Type& childType = drgnParser_.parse(drgnChild);
    auto* childClass =
        dynamic_cast<Class*>(&childType);  // TODO don't use dynamic_cast
    if (!childClass)                       // TODO dodgy error handling
      abort();

    /*
     * Confirm that this child is actually our child.
     *
     * We previously used unqualified names for speed, so types with the same
     * name in different namespaces would have been grouped together.
     */
    bool isActuallyChild = false;
    for (const auto& parent : childClass->parents) {
      // TODO support parent containers?
      auto* parentClass = dynamic_cast<Class*>(&stripTypedefs(parent.type()));
      if (!parentClass)
        continue;

      if (parentClass->fqName() == c.fqName()) {
        isActuallyChild = true;
        break;
      }
    }

    if (isActuallyChild)
      c.children.push_back(*childClass);
  }

  // Recurse to find children-of-children
  for (const auto& child : c.children) {
    accept(child);
  }
}

void AddChildren::recordChildren(drgn_type* type) {
  drgn_type_template_parameter* parents = drgn_type_parents(type);

  for (size_t i = 0; i < drgn_type_num_parents(type); i++) {
    drgn_qualified_type t{};

    if (auto* err = drgn_template_parameter_type(&parents[i], &t);
        err != nullptr) {
      //      TODO useful error:
      //      LOG(ERROR) << "Error when looking up parent class for type " <<
      //      type
      //                 << " err " << err->code << " " << err->message;
      drgn_error_destroy(err);
      continue;
    }

    drgn_type* parent = drgn_utils::underlyingType(t.type);
    if (!drgn_utils::isSizeComplete(parent)) {
      //      VLOG(1) << "Incomplete size for parent class (" <<
      //      drgn_type_tag(parent)
      //              << ") of " << drgn_type_tag(type);
      continue;
    }

    const char* parentName = drgn_type_tag(parent);
    if (!parentName) {
      continue;
    }

    /*
     * drgn pointers are not stable, so use string representation for reverse
     * mapping for now. We need to find a better way of creating this
     * childClasses map - ideally drgn would do this for us.
     *
     * Use unqualified names because fully-qualified names are too slow. We'll
     * check against fully-qualified names once we've narrowed down the number
     * of types to compare.
     */
    childClasses_[parentName].push_back(type);
  }
}

/*
 * Build a mapping of Class -> Children
 *
 * drgn only gives us the mapping Class -> Parents, so we must iterate over all
 * types in the program to build the reverse mapping.
 */
void AddChildren::enumerateChildClasses(SymbolService& symbols) {
  if ((setenv("DRGN_ENABLE_TYPE_ITERATOR", "1", 1)) < 0) {
    //    LOG(ERROR)
    //        << "Could not set DRGN_ENABLE_TYPE_ITERATOR environment variable";
    abort();
  }

  drgn_type_iterator* typesIterator;
  auto* prog = symbols.getDrgnProgram();
  drgn_error* err = drgn_type_iterator_create(prog, &typesIterator);
  if (err) {
    //    LOG(ERROR) << "Error initialising drgn_type_iterator: " << err->code
    //    << ", "
    //               << err->message;
    drgn_error_destroy(err);
    abort();
  }

  while (true) {
    drgn_qualified_type* t;
    err = drgn_type_iterator_next(typesIterator, &t);
    if (err) {
      //      TODO usful error:
      //      LOG(ERROR) << "Error from drgn_type_iterator_next: " << err->code
      //      << ", "
      //                 << err->message;
      drgn_error_destroy(err);
      continue;
    }

    if (!t) {
      break;
    }

    auto kind = drgn_type_kind(t->type);
    if (kind != DRGN_TYPE_CLASS && kind != DRGN_TYPE_STRUCT) {
      continue;
    }

    recordChildren(t->type);
  }

  drgn_type_iterator_destroy(typesIterator);
}

}  // namespace oi::detail::type_graph
