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

/*
 * Visitors are used to walk over graphs of Type nodes.
 *
 * To implement a new visitor:
 * 1. Inherit from one of the base visitor classes in this file
 * 2. Override the `visit()` functions for the types your visitor must deal with
 * 3. Implement `accept(Type&)` to perform double-dispatch and cycle detection
 */

#include "Types.h"

namespace oi::detail::type_graph {

/*
 * Visitor
 *
 * Abstract visitor base class.
 */
class Visitor {
 public:
  virtual ~Visitor() = default;

#define X(OI_TYPE_NAME) virtual void visit(OI_TYPE_NAME&) = 0;
  OI_TYPE_LIST
#undef X
};

/*
 * LazyVisitor
 *
 * Visitor base class which takes no action by default.
 */
class LazyVisitor : public Visitor {
 public:
  virtual ~LazyVisitor() = default;

#define X(OI_TYPE_NAME)               \
  virtual void visit(OI_TYPE_NAME&) { \
  }
  OI_TYPE_LIST
#undef X
};

/*
 * RecursiveVisitor
 *
 * Visitor base class which recurses into types by default.
 */
class RecursiveVisitor : public Visitor {
 public:
  virtual ~RecursiveVisitor() = default;
  virtual void accept(Type&) = 0;
  virtual void accept(Type* type) {
    if (type)
      accept(*type);
  }
  virtual void visit(Class& c) {
    for (const auto& param : c.templateParams) {
      accept(param.type());
    }
    for (const auto& parent : c.parents) {
      accept(parent.type());
    }
    for (const auto& mem : c.members) {
      accept(mem.type());
    }
    for (const auto& child : c.children) {
      accept(child);
    }
  }
  virtual void visit(Container& c) {
    for (const auto& param : c.templateParams) {
      accept(param.type());
    }
  }
  virtual void visit(Primitive&) {
  }
  virtual void visit(Enum&) {
  }
  virtual void visit(Array& a) {
    accept(a.elementType());
  }
  virtual void visit(Typedef& td) {
    accept(td.underlyingType());
  }
  virtual void visit(Pointer& p) {
    accept(p.pointeeType());
  }
  virtual void visit(Dummy&) {
  }
  virtual void visit(DummyAllocator& d) {
    accept(d.allocType());
  }
};

/*
 * ConstVisitor
 *
 * Abstract visitor base class for walking a type graph without modifying it.
 */
class ConstVisitor {
 public:
  virtual ~ConstVisitor() = default;

#define X(OI_TYPE_NAME) virtual void visit(const OI_TYPE_NAME&) = 0;
  OI_TYPE_LIST
#undef X
};

/*
 * LazyConstVisitor
 *
 * Const visitor base class which takes no action by default.
 */
class LazyConstVisitor : public ConstVisitor {
 public:
  virtual ~LazyConstVisitor() = default;

  // TODO work out how to get rid of this "2"
  void visit2(const Type& type) {
    type.accept(*this);
  }

#define X(OI_TYPE_NAME)                     \
  virtual void visit(const OI_TYPE_NAME&) { \
  }
  OI_TYPE_LIST
#undef X
};

}  // namespace oi::detail::type_graph
