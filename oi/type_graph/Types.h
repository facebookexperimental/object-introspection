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
 * This file contains the definitions for the classes which represent nodes
 * (types) in a type graph.
 *
 * Edges in the graph are represented by references held by nodes. This means
 * that once created, node addresses must be stable in order for the references
 * to remain valid (i.e. don't store nodes directly in vectors). It is
 * recommended to use the TypeGraph class when building a complete type graph as
 * this will the memory allocations safely.
 */

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "oi/ContainerInfo.h"
#include "oi/EnumBitset.h"

#define OI_TYPE_LIST \
  X(Class)           \
  X(Container)       \
  X(Primitive)       \
  X(Enum)            \
  X(Array)           \
  X(Typedef)         \
  X(Pointer)         \
  X(Dummy)           \
  X(DummyAllocator)

struct ContainerInfo;

namespace type_graph {

using NodeId = int32_t;

enum class Qualifier {
  Const,
  Max,
};
using QualifierSet = EnumBitset<Qualifier, static_cast<size_t>(Qualifier::Max)>;

class Visitor;
class ConstVisitor;
#define DECLARE_ACCEPT              \
  void accept(Visitor& v) override; \
  void accept(ConstVisitor& v) const override;

// TODO delete copy and move ctors

// TODO type qualifiers are needed for some stuff?
class Type {
 public:
  virtual ~Type() = default;
  virtual void accept(Visitor& v) = 0;
  virtual void accept(ConstVisitor& v) const = 0;

  // TODO don't always return a copy for name()
  virtual std::string name() const = 0;
  virtual size_t size() const = 0;
  virtual uint64_t align() const = 0;
};

class Member {
 public:
  Member(Type& type,
         const std::string& name,
         uint64_t bitOffset,
         uint64_t bitsize = 0)
      : type_(type), name(name), bitOffset(bitOffset), bitsize(bitsize) {
  }

  Type& type() const {
    return type_;
  }

 private:
  std::reference_wrapper<Type> type_;

 public:
  std::string name;
  uint64_t bitOffset;
  uint64_t bitsize;
  uint64_t align = 0;
};

struct Function {
  Function(const std::string& name, int virtuality = 0)
      : name(name), virtuality(virtuality) {
  }

  std::string name;
  int virtuality;
};

class Class;
struct Parent {
  Parent(Type& type, uint64_t bitOffset) : type_(type), bitOffset(bitOffset) {
  }

  Type& type() const {
    return type_;
  }

  //  uint64_t offset() const {
  //    return offset_;
  //  }

 private:
  std::reference_wrapper<Type> type_;

 public:
  uint64_t bitOffset;
};

class TemplateParam {
 public:
  // TODO make ctors explicit
  TemplateParam(Type& type) : type_(&type) {
  }
  TemplateParam(Type& type, QualifierSet qualifiers)
      : type_(&type), qualifiers(qualifiers) {
  }
  TemplateParam(std::string value) : value(std::move(value)) {
  }

  Type* type() const {
    return type_;
  }

 private:
  Type* type_ = nullptr;  // Note: type is not always set

 public:
  QualifierSet qualifiers;
  std::optional<std::string> value;
};

class Class : public Type {
 public:
  enum class Kind {
    Class,
    Struct,
    Union,
  };

  Class(NodeId id,
        Kind kind,
        std::string name,
        std::string fqName,
        size_t size,
        int virtuality = 0)
      : name_(std::move(name)),
        fqName_(std::move(fqName)),
        size_(size),
        kind_(kind),
        virtuality_(virtuality),
        id_(id) {
  }

  Class(NodeId id,
        Kind kind,
        const std::string& name,
        size_t size,
        int virtuality = 0)
      : Class(id, kind, name, name, size, virtuality) {
  }

  DECLARE_ACCEPT

  Kind kind() const {
    return kind_;
  }

  virtual std::string name() const override {
    return name_;
  }

  void setName(std::string name) {
    name_ = std::move(name);
  }

  virtual size_t size() const override {
    return size_;
  }

  virtual uint64_t align() const override {
    return align_;
  }

  void setAlign(uint64_t alignment) {
    align_ = alignment;
  }

  int virtuality() const {
    return virtuality_;
  }

  bool packed() const {
    return packed_;
  }

  void setPacked() {
    packed_ = true;
  }

  const std::string& fqName() const {
    return fqName_;
  }

  bool isDynamic() const;

  NodeId id() const {
    return id_;
  }

  std::vector<TemplateParam> templateParams;
  std::vector<Parent> parents;  // Sorted by offset
  std::vector<Member> members;  // Sorted by offset
  std::vector<Function> functions;
  std::vector<std::reference_wrapper<Class>>
      children;  // Only for dynamic classes

 private:
  std::string name_;
  std::string fqName_;
  size_t size_;
  uint64_t align_ = 0;
  Kind kind_;
  int virtuality_;
  NodeId id_ = -1;
  bool packed_ = false;
};

class Container : public Type {
 public:
  Container(NodeId id, const ContainerInfo& containerInfo, size_t size)
      : containerInfo_(containerInfo),
        name_(containerInfo.typeName),
        size_(size),
        id_(id) {
  }

  DECLARE_ACCEPT

  const std::string& containerName() const {
    return containerInfo_.typeName;
  }

  virtual std::string name() const override {
    return name_;
  }

  void setName(std::string name) {
    name_ = std::move(name);
  }

  virtual size_t size() const override {
    return size_;
  }

  virtual uint64_t align() const override {
    return 8;  // TODO not needed for containers?
  }

  NodeId id() const {
    return id_;
  }

  std::vector<TemplateParam> templateParams;
  const ContainerInfo& containerInfo_;

 private:
  std::string name_;
  size_t size_;
  NodeId id_ = -1;
};

class Enum : public Type {
 public:
  explicit Enum(const std::string& name, size_t size)
      : name_(name), size_(size) {
  }

  DECLARE_ACCEPT

  virtual std::string name() const override {
    return name_;
  }

  virtual size_t size() const override {
    return size_;
  }

  virtual uint64_t align() const override {
    return size();
  }

 private:
  std::string name_;
  size_t size_;
};

class Array : public Type {
 public:
  Array(NodeId id, Type& elementType, size_t len)
      : elementType_(elementType), len_(len), id_(id) {
  }

  DECLARE_ACCEPT

  virtual std::string name() const override {
    return "OIArray<" + elementType_.name() + ", " + std::to_string(len_) + ">";
  }

  virtual size_t size() const override {
    return len_ * elementType_.size();
  }

  virtual uint64_t align() const override {
    return elementType_.size();
  }

  Type& elementType() const {
    return elementType_;
  }

  size_t len() const {
    return len_;
  }

  NodeId id() const {
    return id_;
  }

 private:
  Type& elementType_;
  size_t len_;
  NodeId id_ = -1;
};

class Primitive : public Type {
 public:
  enum class Kind {
    Int8,
    Int16,
    Int32,
    Int64,
    UInt8,
    UInt16,
    UInt32,
    UInt64,
    Float32,
    Float64,
    Float80,   // TODO worth including?
    Float128,  // TODO can we generate this?
    Bool,

    UIntPtr,  // Really an alias, but useful to have as its own primitive

    Void,
  };

  explicit Primitive(Kind kind) : kind_(kind) {
  }

  DECLARE_ACCEPT

  virtual std::string name() const override;
  virtual size_t size() const override;
  virtual uint64_t align() const override {
    return size();
  }

 private:
  Kind kind_;
};

class Typedef : public Type {
 public:
  explicit Typedef(NodeId id, const std::string& name, Type& underlyingType)
      : name_(name), underlyingType_(underlyingType), id_(id) {
  }

  DECLARE_ACCEPT

  virtual std::string name() const override {
    return name_;
  }

  void setName(std::string name) {
    name_ = std::move(name);
  }

  virtual size_t size() const override {
    return underlyingType_.size();
  }

  virtual uint64_t align() const override {
    return underlyingType_.align();
  }

  Type& underlyingType() const {
    return underlyingType_;
  }

  NodeId id() const {
    return id_;
  }

 private:
  std::string name_;
  Type& underlyingType_;
  NodeId id_ = -1;
};

class Pointer : public Type {
 public:
  explicit Pointer(NodeId id, Type& pointeeType)
      : pointeeType_(pointeeType), id_(id) {
  }

  DECLARE_ACCEPT

  virtual std::string name() const override {
    return pointeeType_.name() + "*";
  }

  virtual size_t size() const override {
    return sizeof(uintptr_t);
  }

  virtual uint64_t align() const override {
    return size();
  }

  Type& pointeeType() const {
    return pointeeType_;
  }

  NodeId id() const {
    return id_;
  }

 private:
  Type& pointeeType_;
  NodeId id_ = -1;
};

class Dummy : public Type {
 public:
  explicit Dummy(size_t size, uint64_t align) : size_(size), align_(align) {
  }

  DECLARE_ACCEPT

  virtual std::string name() const override {
    return "DummySizedOperator<" + std::to_string(size_) + ", " +
           std::to_string(align_) + ">";
  }

  virtual size_t size() const override {
    return size_;
  }

  virtual uint64_t align() const override {
    return align_;
  }

 private:
  size_t size_;
  uint64_t align_;
};

class DummyAllocator : public Type {
 public:
  explicit DummyAllocator(Type& type, size_t size, uint64_t align)
      : type_(type), size_(size), align_(align) {
  }

  DECLARE_ACCEPT

  virtual std::string name() const override {
    return "DummyAllocator<" + type_.name() + ", " + std::to_string(size_) +
           ", " + std::to_string(align_) + ">";
  }

  virtual size_t size() const override {
    return size_;
  }

  virtual uint64_t align() const override {
    return align_;
  }

  Type& allocType() const {
    return type_;
  }

 private:
  Type& type_;
  size_t size_;
  uint64_t align_;
};

}  // namespace type_graph

#undef DECLARE_ACCEPT
