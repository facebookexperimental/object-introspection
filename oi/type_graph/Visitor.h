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
 * A visitor simply walks over nodes in a type graph.
 */
template <typename T>
class Visitor {
 public:
  virtual ~Visitor() = default;

#define X(OI_TYPE_NAME) virtual T visit(OI_TYPE_NAME&) = 0;
  OI_TYPE_LIST
#undef X
};

/*
 * LazyVisitor
 *
 * Visitor base class which takes no action by default.
 */
class LazyVisitor : public Visitor<void> {
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
class RecursiveVisitor : public Visitor<void> {
 public:
  virtual ~RecursiveVisitor() = default;
  virtual void accept(Type&) = 0;
  virtual void accept(Type* type) {
    if (type)
      accept(*type);
  }
  virtual void visit(Incomplete&) {
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
    accept(c.underlying());
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
  virtual void visit(Reference& r) {
    accept(r.pointeeType());
  }
  virtual void visit(Dummy&) {
  }
  virtual void visit(DummyAllocator& d) {
    accept(d.allocType());
  }
  virtual void visit(CaptureKeys& c) {
    accept(c.underlyingType());
  }
};

/*
 * RecursiveMutator
 *
 * Mutator base class which recurses into types by default.
 */
class RecursiveMutator : public Visitor<Type&> {
 public:
  virtual ~RecursiveMutator() = default;
  virtual Type& mutate(Type&) = 0;
  virtual Type* mutate(Type* type) {
    if (type)
      return &mutate(*type);
    return nullptr;
  }
  virtual Type& visit(Incomplete& i) {
    return i;
  }
  virtual Type& visit(Class& c) {
    for (auto& param : c.templateParams) {
      param.setType(mutate(param.type()));
    }
    for (auto& parent : c.parents) {
      parent.setType(mutate(parent.type()));
    }
    for (auto& mem : c.members) {
      mem.setType(mutate(mem.type()));
    }
    for (auto& child : c.children) {
      child = dynamic_cast<Class&>(mutate(child));
    }
    return c;
  }
  virtual Type& visit(Container& c) {
    for (auto& param : c.templateParams) {
      param.setType(mutate(param.type()));
    }
    c.setUnderlying(mutate(c.underlying()));
    return c;
  }
  virtual Type& visit(Primitive& p) {
    return p;
  }
  virtual Type& visit(Enum& e) {
    return e;
  }
  virtual Type& visit(Array& a) {
    a.setElementType(mutate(a.elementType()));
    return a;
  }
  virtual Type& visit(Typedef& td) {
    td.setUnderlyingType(mutate(td.underlyingType()));
    return td;
  }
  virtual Type& visit(Pointer& p) {
    p.setPointeeType(mutate(p.pointeeType()));
    return p;
  }
  virtual Type& visit(Reference& p) {
    p.setPointeeType(mutate(p.pointeeType()));
    return p;
  }
  virtual Type& visit(Dummy& d) {
    return d;
  }
  virtual Type& visit(DummyAllocator& d) {
    d.setAllocType(mutate(d.allocType()));
    return d;
  }
  virtual Type& visit(CaptureKeys& c) {
    c.setUnderlyingType(mutate(c.underlyingType()));
    return c;
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
