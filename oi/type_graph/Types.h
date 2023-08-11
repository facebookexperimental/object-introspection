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
 *
 * All non-leaf nodes have IDs for efficient cycle detection and to assist
 * debugging.
 */

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
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

namespace oi::detail::type_graph {

// Must be signed. "-1" represents "leaf node"
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

/*
 * Type
 *
 * Abstract type base class. Attributes common to all types belong here.
 */
class Type {
 public:
  virtual ~Type() = default;
  virtual void accept(Visitor& v) = 0;
  virtual void accept(ConstVisitor& v) const = 0;

  virtual const std::string& name() const = 0;
  // Return the name as described in the input code for textual representation.
  virtual std::string_view inputName() const = 0;
  virtual size_t size() const = 0;
  virtual uint64_t align() const = 0;
  virtual NodeId id() const = 0;
};

/*
 * Member
 *
 * A class' member variable.
 */
class Member {
 public:
  Member(Type& type,
         const std::string& name_,
         uint64_t bitOffset,
         uint64_t bitsize = 0)
      : type_(type),
        name(name_),
        inputName(name_),
        bitOffset(bitOffset),
        bitsize(bitsize) {
  }

  Type& type() const {
    return type_;
  }

 private:
  std::reference_wrapper<Type> type_;

 public:
  std::string name;
  std::string inputName;
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

struct TemplateParam {
  // TODO make ctors explicit
  TemplateParam(Type& type) : type_(type) {
  }
  TemplateParam(Type& type, QualifierSet qualifiers)
      : type_(type), qualifiers(qualifiers) {
  }
  TemplateParam(Type& type, std::string value)
      : type_(type), value(std::move(value)) {
  }

  Type& type() const {
    return type_;
  }

 private:
  std::reference_wrapper<Type> type_;

 public:
  QualifierSet qualifiers;
  std::optional<std::string> value;
};

/*
 * Class
 *
 * A class, struct or union.
 */
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
      : name_(name),
        inputName_(std::move(name)),
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

  static inline constexpr bool has_node_id = true;

  DECLARE_ACCEPT

  Kind kind() const {
    return kind_;
  }

  virtual const std::string& name() const override {
    return name_;
  }

  virtual std::string_view inputName() const override {
    return inputName_;
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

  virtual NodeId id() const override {
    return id_;
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

  std::vector<TemplateParam> templateParams;
  std::vector<Parent> parents;  // Sorted by offset
  std::vector<Member> members;  // Sorted by offset
  std::vector<Function> functions;
  std::vector<std::reference_wrapper<Class>>
      children;  // Only for dynamic classes

 private:
  std::string name_;
  std::string inputName_;
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
        inputName_(containerInfo.typeName),
        size_(size),
        id_(id) {
  }

  static inline constexpr bool has_node_id = true;

  DECLARE_ACCEPT

  const std::string& containerName() const {
    return containerInfo_.typeName;
  }

  virtual const std::string& name() const override {
    return name_;
  }

  void setName(std::string name) {
    name_ = std::move(name);
  }

  virtual std::string_view inputName() const override {
    return inputName_;
  }

  void setInputName(std::string name) {
    inputName_ = std::move(name);
  }

  virtual size_t size() const override {
    return size_;
  }

  virtual uint64_t align() const override {
    // XXX Big hack!
    // We can not assume that all containers have 8-byte alignment. However, to
    // properly calculate alignment would require significant refactoring of
    // the container identification code:
    //  - DrgnParser needs to return Class objects instead of Containers
    //  - They must be flattened
    //  - We must calculate alignment based on their members
    //  - Then we are able to convert them into Container objects
    //
    // As a quick, hopefully temporary solution, hardcode some known container
    // alignments here
    if (containerInfo_.ctype == THRIFT_ISSET_TYPE)
      return 1;
    return 8;
  }

  virtual NodeId id() const override {
    return id_;
  }

  std::vector<TemplateParam> templateParams;
  const ContainerInfo& containerInfo_;

 private:
  std::string name_;
  std::string inputName_;
  size_t size_;
  NodeId id_ = -1;
};

class Enum : public Type {
 public:
  explicit Enum(std::string name,
                size_t size,
                std::map<int64_t, std::string> enumerators = {})
      : name_(name),
        inputName_(std::move(name)),
        size_(size),
        enumerators_(std::move(enumerators)) {
  }

  static inline constexpr bool has_node_id = false;

  DECLARE_ACCEPT

  virtual const std::string& name() const override {
    return name_;
  }

  virtual std::string_view inputName() const override {
    return inputName_;
  }

  void setName(std::string name) {
    name_ = std::move(name);
  }

  virtual size_t size() const override {
    return size_;
  }

  virtual uint64_t align() const override {
    return size();
  }

  virtual NodeId id() const override {
    return -1;
  }

  std::map<int64_t, std::string> enumerators() const {
    return enumerators_;
  }

 private:
  std::string name_;
  std::string inputName_;
  size_t size_;
  std::map<int64_t, std::string> enumerators_;
};

class Array : public Type {
 public:
  Array(NodeId id, Type& elementType, size_t len)
      : elementType_(elementType), len_(len), id_(id) {
    regenerateName();
  }

  static inline constexpr bool has_node_id = true;

  DECLARE_ACCEPT

  virtual const std::string& name() const override {
    return name_;
  }

  void regenerateName() {
    name_ = std::string{"OIArray<"} + elementType_.name() + ", " +
            std::to_string(len_) + ">";
  }

  virtual std::string_view inputName() const override {
    return inputName_;
  }

  void setInputName(std::string name) {
    inputName_ = std::move(name);
  }

  virtual size_t size() const override {
    return len_ * elementType_.size();
  }

  virtual uint64_t align() const override {
    return elementType_.align();
  }

  virtual NodeId id() const override {
    return id_;
  }

  Type& elementType() const {
    return elementType_;
  }

  size_t len() const {
    return len_;
  }

 private:
  Type& elementType_;
  std::string inputName_;
  size_t len_;
  NodeId id_ = -1;

  std::string name_;
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
    Incomplete,  // Behaves the same as Void, but alerts us that the type was
                 // stubbed out due to incomplete DWARF
  };

  explicit Primitive(Kind kind) : kind_(kind), name_(getName(kind)) {
  }

  static inline constexpr bool has_node_id = false;

  DECLARE_ACCEPT

  virtual const std::string& name() const override {
    return name_;
  }
  virtual std::string_view inputName() const override {
    return name_;
  }
  virtual size_t size() const override;
  virtual uint64_t align() const override {
    return size();
  }
  virtual NodeId id() const override {
    return -1;
  }
  Kind kind() const {
    return kind_;
  }

 private:
  Kind kind_;
  std::string name_;

  static std::string getName(Kind kind);
};

class Typedef : public Type {
 public:
  explicit Typedef(NodeId id, std::string name, Type& underlyingType)
      : name_(name),
        inputName_(std::move(name)),
        underlyingType_(underlyingType),
        id_(id) {
  }

  static inline constexpr bool has_node_id = true;

  DECLARE_ACCEPT

  virtual const std::string& name() const override {
    return name_;
  }

  virtual std::string_view inputName() const override {
    return inputName_;
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

  virtual NodeId id() const override {
    return id_;
  }

  Type& underlyingType() const {
    return underlyingType_;
  }

 private:
  std::string name_;
  std::string inputName_;
  Type& underlyingType_;
  NodeId id_ = -1;
};

class Pointer : public Type {
 public:
  explicit Pointer(NodeId id, Type& pointeeType)
      : pointeeType_(pointeeType), id_(id) {
    regenerateName();
  }

  static inline constexpr bool has_node_id = true;

  DECLARE_ACCEPT

  virtual const std::string& name() const override {
    return name_;
  }

  void regenerateName() {
    name_ = pointeeType_.name() + "*";
  }

  virtual std::string_view inputName() const override {
    return inputName_;
  }

  void setInputName(std::string name) {
    inputName_ = std::move(name);
  }

  virtual size_t size() const override {
    return sizeof(uintptr_t);
  }

  virtual uint64_t align() const override {
    return size();
  }

  virtual NodeId id() const override {
    return id_;
  }

  Type& pointeeType() const {
    return pointeeType_;
  }

 private:
  Type& pointeeType_;
  std::string inputName_;
  NodeId id_ = -1;

  std::string name_;
};

/*
 * Dummy
 *
 * A type that just has a given size and alignment.
 */
class Dummy : public Type {
 public:
  explicit Dummy(size_t size, uint64_t align, std::string inputName)
      : size_(size),
        align_(align),
        name_(std::string{"DummySizedOperator<"} + std::to_string(size) + ", " +
              std::to_string(align) + ">"),
        inputName_(std::move(inputName)) {
  }

  static inline constexpr bool has_node_id = false;

  DECLARE_ACCEPT

  virtual const std::string& name() const override {
    return name_;
  }

  virtual std::string_view inputName() const override {
    return inputName_;
  }

  virtual size_t size() const override {
    return size_;
  }

  virtual uint64_t align() const override {
    return align_;
  }

  virtual NodeId id() const override {
    return -1;
  }

 private:
  size_t size_;
  uint64_t align_;

  std::string name_;
  std::string inputName_;
};

/*
 * DummyAllocator
 *
 * A type with a given size and alignment, which also satisfies the C++
 * allocator completeness requirements for a given type.
 */
class DummyAllocator : public Type {
 public:
  explicit DummyAllocator(Type& type,
                          size_t size,
                          uint64_t align,
                          std::string inputName)
      : type_(type),
        size_(size),
        align_(align),
        inputName_(std::move(inputName)) {
    regenerateName();
  }

  static inline constexpr bool has_node_id = false;

  DECLARE_ACCEPT

  virtual const std::string& name() const override {
    return name_;
  }

  void regenerateName() {
    name_ = std::string{"DummyAllocator<"} + type_.name() + ", " +
            std::to_string(size_) + ", " + std::to_string(align_) + ">";
  }

  virtual std::string_view inputName() const override {
    return inputName_;
  }

  virtual size_t size() const override {
    return size_;
  }

  virtual uint64_t align() const override {
    return align_;
  }

  virtual NodeId id() const override {
    return -1;
  }

  Type& allocType() const {
    return type_;
  }

 private:
  Type& type_;
  size_t size_;
  uint64_t align_;

  std::string name_;
  std::string inputName_;
};

Type& stripTypedefs(Type& type);

}  // namespace oi::detail::type_graph

#undef DECLARE_ACCEPT
