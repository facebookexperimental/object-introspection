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
#include <variant>
#include <vector>

#include "oi/ContainerInfo.h"
#include "oi/EnumBitset.h"

#define OI_TYPE_LIST \
  X(Incomplete)      \
  X(Class)           \
  X(Container)       \
  X(Primitive)       \
  X(Enum)            \
  X(Array)           \
  X(Typedef)         \
  X(Pointer)         \
  X(Reference)       \
  X(Dummy)           \
  X(DummyAllocator)  \
  X(CaptureKeys)

struct ContainerInfo;
struct drgn_type;

namespace oi::detail::type_graph {

// Must be signed. "-1" represents "leaf node"
using NodeId = int32_t;

enum class Qualifier {
  Const,
  Max,
};
using QualifierSet = EnumBitset<Qualifier, static_cast<size_t>(Qualifier::Max)>;

template <typename T>
class Visitor;
class ConstVisitor;
#define DECLARE_ACCEPT                                \
  void accept(Visitor<void>& v) override;             \
  drgn_type* accept(Visitor<drgn_type*>& v) override; \
  Type& accept(Visitor<Type&>& m) override;           \
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
  virtual void accept(Visitor<void>& v) = 0;
  virtual drgn_type* accept(Visitor<drgn_type*>& v) = 0;
  virtual Type& accept(Visitor<Type&>& m) = 0;
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

  Member(Type& type, const Member& other)
      : type_(type),
        name(other.name),
        inputName(other.inputName),
        bitOffset(other.bitOffset),
        bitsize(other.bitsize) {
  }

  Type& type() const {
    return type_;
  }

  void setType(Type& type) {
    type_ = type;
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

struct Parent {
  Parent(Type& type, uint64_t bitOffset) : type_(type), bitOffset(bitOffset) {
  }

  Type& type() const {
    return type_;
  }

  void setType(Type& type) {
    type_ = type;
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

  void setType(Type& type) {
    type_ = type;
  }

 private:
  std::reference_wrapper<Type> type_;

 public:
  QualifierSet qualifiers;
  std::optional<std::string> value;
};

/*
 * Incomplete
 *
 * A wrapper around a type we couldn't determine the size of.
 */
class Incomplete : public Type {
 public:
  Incomplete(Type& underlyingType) : underlyingType_(underlyingType) {
  }

  Incomplete(std::string underlyingTypeName)
      : underlyingType_(std::move(underlyingTypeName)) {
  }

  static inline constexpr bool has_node_id = false;

  DECLARE_ACCEPT

  const std::string& name() const override {
    return kName;
  }

  std::string_view inputName() const override {
    if (std::holds_alternative<std::string>(underlyingType_)) {
      return std::get<std::string>(underlyingType_);
    }

    return std::get<std::reference_wrapper<Type>>(underlyingType_)
        .get()
        .inputName();
  }

  size_t size() const override {
    return 0;
  }

  uint64_t align() const override {
    return 0;
  }

  NodeId id() const override {
    return -1;
  }

  std::optional<std::reference_wrapper<Type>> underlyingType() const {
    if (std::holds_alternative<std::string>(underlyingType_)) {
      return std::nullopt;
    }

    return std::get<std::reference_wrapper<Type>>(underlyingType_);
  }

 private:
  std::variant<std::string, std::reference_wrapper<Type>> underlyingType_;
  static const std::string kName;
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
        std::string inputName,
        size_t size,
        int virtuality = 0)
      : name_(name),
        inputName_(std::move(inputName)),
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

  virtual const std::string& name() const override {
    return name_;
  }

  virtual std::string_view inputName() const override {
    return inputName_;
  }

  void setName(std::string name) {
    name_ = std::move(name);
  }

  void setInputName(std::string name) {
    inputName_ = std::move(name);
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

  virtual NodeId id() const override {
    return id_;
  }

  Kind kind() const {
    return kind_;
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
    return inputName_;
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
  size_t size_;
  uint64_t align_ = 0;
  Kind kind_;
  int virtuality_;
  NodeId id_ = -1;
  bool packed_ = false;
};

/*
 * Container
 *
 * A type of class for which we can do special processing.
 */
class Container : public Type {
 public:
  Container(NodeId id,
            const ContainerInfo& containerInfo,
            size_t size,
            Type* underlying)
      : containerInfo_(containerInfo),
        underlying_(underlying),
        name_(containerInfo.typeName),
        inputName_(containerInfo.typeName),
        size_(size),
        id_(id) {
  }

  Container(NodeId id,
            const Container& other,
            const ContainerInfo& containerInfo)
      : templateParams(other.templateParams),
        containerInfo_(containerInfo),
        underlying_(other.underlying_),
        name_(other.name_),
        inputName_(other.inputName_),
        size_(other.size_),
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

  void setInputName(std::string name) {
    inputName_ = std::move(name);
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

  virtual NodeId id() const override {
    return id_;
  }

  const std::string& containerName() const {
    return containerInfo_.typeName;
  }

  Type* underlying() const {
    return underlying_;
  }

  void setUnderlying(Type* underlying) {
    underlying_ = underlying;
  }

  std::vector<TemplateParam> templateParams;
  const ContainerInfo& containerInfo_;

 private:
  Type* underlying_;
  std::string name_;
  std::string inputName_;
  size_t size_;
  uint64_t align_ = 0;
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

  void setInputName(std::string name) {
    inputName_ = std::move(name);
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
    name_ = std::string{"OIArray<"} + elementType_.get().name() + ", " +
            std::to_string(len_) + ">";
  }

  virtual std::string_view inputName() const override {
    return inputName_;
  }

  void setInputName(std::string name) {
    inputName_ = std::move(name);
  }

  virtual size_t size() const override {
    return len_ * elementType_.get().size();
  }

  virtual uint64_t align() const override {
    return elementType_.get().align();
  }

  virtual NodeId id() const override {
    return id_;
  }

  Type& elementType() const {
    return elementType_;
  }

  void setElementType(Type& type) {
    elementType_ = type;
  }

  size_t len() const {
    return len_;
  }

 private:
  std::reference_wrapper<Type> elementType_;
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

    StubbedPointer,
    Void,
  };

  explicit Primitive(Kind kind) : kind_(kind), name_(getName(kind)) {
  }

  static inline constexpr bool has_node_id = false;

  DECLARE_ACCEPT

  virtual const std::string& name() const override {
    return name_;
  }
  virtual std::string_view inputName() const override;
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
    return underlyingType_.get().size();
  }

  virtual uint64_t align() const override {
    return underlyingType_.get().align();
  }

  virtual NodeId id() const override {
    return id_;
  }

  Type& underlyingType() const {
    return underlyingType_;
  }

  void setUnderlyingType(Type& type) {
    underlyingType_ = type;
  }

 private:
  std::string name_;
  std::string inputName_;
  std::reference_wrapper<Type> underlyingType_;
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
    name_ = pointeeType_.get().name() + "*";
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

  void setPointeeType(Type& type) {
    pointeeType_ = type;
  }

 private:
  std::reference_wrapper<Type> pointeeType_;
  std::string inputName_;
  NodeId id_ = -1;

  std::string name_;
};

class Reference : public Type {
 public:
  explicit Reference(NodeId id, Type& pointeeType)
      : pointeeType_(pointeeType), id_(id) {
    regenerateName();
  }

  static inline constexpr bool has_node_id = true;

  DECLARE_ACCEPT

  virtual const std::string& name() const override {
    return name_;
  }

  void regenerateName() {
    // Following a reference wouldn't trigger cycle checking, as it would look
    // like anything else we're sure is there. Generate as a pointer. It will be
    // followed regardless of `ChaseRawPointers` because that affects whether a
    // type becomes a `StubbedPointer` and not whether pointers are followed in
    // the generated code.
    name_ = pointeeType_.get().name() + "*";
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

  void setPointeeType(Type& type) {
    pointeeType_ = type;
  }

 private:
  std::reference_wrapper<Type> pointeeType_;
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
  explicit Dummy(NodeId id, size_t size, uint64_t align, std::string inputName)
      : size_(size),
        align_(align),
        id_(id),
        name_(std::string{"DummySizedOperator<"} + std::to_string(size) + ", " +
              std::to_string(align) + ", " + std::to_string(id_) + ">"),
        inputName_(std::move(inputName)) {
  }

  static inline constexpr bool has_node_id = true;

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
    return id_;
  }

 private:
  size_t size_;
  uint64_t align_;
  NodeId id_ = -1;

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
  explicit DummyAllocator(
      NodeId id, Type& type, size_t size, uint64_t align, std::string inputName)
      : type_(type),
        size_(size),
        align_(align),
        id_(id),
        inputName_(std::move(inputName)) {
    regenerateName();
  }

  static inline constexpr bool has_node_id = true;

  DECLARE_ACCEPT

  virtual const std::string& name() const override {
    return name_;
  }

  void regenerateName() {
    name_ = std::string{"DummyAllocator<"} + type_.get().name() + ", " +
            std::to_string(size_) + ", " + std::to_string(align_) + ", " +
            std::to_string(id_) + ">";
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
    return id_;
  }

  Type& allocType() const {
    return type_;
  }

  void setAllocType(Type& type) {
    type_ = type;
  }

 private:
  std::reference_wrapper<Type> type_;
  size_t size_;
  uint64_t align_;
  NodeId id_ = -1;

  std::string name_;
  std::string inputName_;
};

/*
 * CaptureKeys
 *
 * The held Container will have its keys captured.
 */
class CaptureKeys : public Type {
 public:
  explicit CaptureKeys(Container& c, const ContainerInfo& info)
      : container_(c), containerInfo_(info) {
    regenerateName();
  }

  static inline constexpr bool has_node_id = false;

  DECLARE_ACCEPT

  virtual const std::string& name() const override {
    return name_;
  }

  virtual std::string_view inputName() const override {
    return container_.get().inputName();
  }

  void regenerateName() {
    name_ = "OICaptureKeys<" + container_.get().name() + ">";
  }

  virtual size_t size() const override {
    return container_.get().size();
  }

  virtual uint64_t align() const override {
    return container_.get().align();
  }

  virtual NodeId id() const override {
    return -1;
  }

  const ContainerInfo& containerInfo() const {
    return containerInfo_;
  }

  Container& container() const {
    return container_;
  }

  void setContainer(Container& container) {
    container_ = container;
  }

 private:
  std::reference_wrapper<Container> container_;
  const ContainerInfo& containerInfo_;
  std::string name_;
};

Type& stripTypedefs(Type& type);

}  // namespace oi::detail::type_graph

#undef DECLARE_ACCEPT
