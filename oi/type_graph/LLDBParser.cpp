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
#include "LLDBParser.h"

#include <glog/logging.h>
#include <lldb/API/LLDB.h>
#include <cassert>

namespace oi::detail::type_graph {

Type& LLDBParser::parse(lldb::SBType& root) {
  depth_ = 0;
  return enumerateType(root);
}

struct DepthGuard {
  explicit DepthGuard(int& depth) : depth_(depth) {
    ++depth_;
  }
  ~DepthGuard() {
    --depth_;
  }
 private:
  int& depth_;
};

Type& LLDBParser::enumerateType(lldb::SBType& type) {
  // Avoid re-enumerating an already-processsed type
  if (auto it = lldb_types_.find(type); it != lldb_types_.end())
    return it->second;

  DepthGuard guard(depth_);

  bool isTypeIncomplete = !type.IsTypeComplete();

  std::optional<std::reference_wrapper<Type>> t;
  try {
    switch (auto kind = type.GetTypeClass()) {
      case lldb::eTypeClassClass:
      case lldb::eTypeClassStruct:
      case lldb::eTypeClassUnion:
        t = enumerateClass(type);
        break;
      case lldb::eTypeClassEnumeration:
        t = enumerateEnum(type);
        break;
      case lldb::eTypeClassTypedef:
        t = enumerateTypedef(type);
        break;
      case lldb::eTypeClassPointer:
      case lldb::eTypeClassReference:
        t = enumeratePointer(type);
        break;
      case lldb::eTypeClassBuiltin:
        t = enumeratePrimitive(type);
        break;
      case lldb::eTypeClassArray:
        t = enumerateArray(type);
        break;
      case lldb::eTypeClassInvalid:
      case lldb::eTypeClassBlockPointer:
      case lldb::eTypeClassComplexFloat:
      case lldb::eTypeClassComplexInteger:
      case lldb::eTypeClassFunction:
      case lldb::eTypeClassMemberPointer:
      case lldb::eTypeClassObjCObject:
      case lldb::eTypeClassObjCInterface:
      case lldb::eTypeClassObjCObjectPointer:
      case lldb::eTypeClassVector:
      case lldb::eTypeClassOther:
      case lldb::eTypeClassAny:
        throw LLDBParserError{"Unhandled type class: " + std::to_string(kind)};
    }
  } catch (const LLDBParserError& e) {
    if (!isTypeIncomplete)
      throw;
  }

  if (isTypeIncomplete) {
    return makeType<Incomplete>(type, *t);
  }

  return t.value();
}

Class& LLDBParser::enumerateClass(lldb::SBType& type) {
  auto name = type.GetName();
  auto displayName = type.GetUnqualifiedType().GetDisplayTypeName();
  auto size = type.GetByteSize();
  auto virtuality = type.IsPolymorphicClass();

  Class::Kind kind;
  switch (auto typeClass = type.GetTypeClass()) {
    case lldb::eTypeClassClass:
      kind = Class::Kind::Class;
      break;
    case lldb::eTypeClassStruct:
      kind = Class::Kind::Struct;
      break;
    case lldb::eTypeClassUnion:
      kind = Class::Kind::Union;
      break;
    default:
      throw LLDBParserError{"Invalid type class for compound type: " + std::to_string(typeClass)};
  }

  auto &c = makeType<Class>(type, kind, displayName, name, size, virtuality);

  enumerateClassMembers(type, c.members);
  enumerateClassFunctions(type, c.functions);

  return c;
}

void LLDBParser::enumerateClassMembers(lldb::SBType& type, std::vector<Member>& members) {
  assert(members.empty());
  members.reserve(type.GetNumberOfFields());

  /* TODO: We are missing the _vptr */
  for (uint32_t i = 0; i < type.GetNumberOfFields(); i++) {
    auto field = type.GetFieldAtIndex(i);
    if (field.GetName() == nullptr)
      continue; // Skip anonymous fields (TODO: Why?)
    auto fieldName = field.GetName();
    auto fieldType = field.GetType();
    auto bitFieldSize = field.GetBitfieldSizeInBits();
    auto bitOffset = field.GetOffsetInBits();

    auto& enumeratedField = enumerateType(fieldType);
    members.emplace_back(enumeratedField, fieldName, bitOffset, bitFieldSize);
  }

  std::sort(members.begin(), members.end(), [](const auto& a, const auto& b) {
    return a.bitOffset < b.bitOffset;
  });
}

void LLDBParser::enumerateClassFunctions(lldb::SBType &type, std::vector<Function>& functions) {
  assert(functions.empty());
  functions.reserve(type.GetNumberOfMemberFunctions());

  /* TODO: We are missing the default constructors */
  for (uint32_t i = 0; i < type.GetNumberOfMemberFunctions(); i++) {
    auto function = type.GetMemberFunctionAtIndex(i);

    /* TODO: We don't know if the function is virtual */
    functions.emplace_back(function.GetName(), false);
  }
}

Enum& LLDBParser::enumerateEnum(lldb::SBType& type) {
  const char *typeName = type.GetName();
  std::string name = typeName ? typeName : "";
  uint64_t size = type.GetByteSize();

  std::map<int64_t, std::string> enumeratorMap;
  if (options_.readEnumValues) {
    auto members = type.GetEnumMembers();
    for (uint32_t i = 0; i < members.GetSize(); i++) {
      auto member = members.GetTypeEnumMemberAtIndex(i);
      enumeratorMap[member.GetValueAsSigned()] = member.GetName();
    }
  }

  return makeType<Enum>(type, name, size, std::move(enumeratorMap));
}

Typedef& LLDBParser::enumerateTypedef(lldb::SBType& type) {
  std::string name = type.GetName();

  lldb::SBType underlyingType = type.GetTypedefedType();
  auto& t = enumerateType(underlyingType);
  return makeType<Typedef>(type, name, t);
}

Type& LLDBParser::enumeratePointer(lldb::SBType& type) {
 if (!chasePointer()) {
  return makeType<Primitive>(type, Primitive::Kind::StubbedPointer);
 }

  lldb::SBType pointeeType;
  switch (auto kind = type.GetTypeClass()) {
    case lldb::eTypeClassPointer:
      pointeeType = type.GetPointeeType();
      break;
    case lldb::eTypeClassReference:
      pointeeType = type.GetDereferencedType();
      break;
    default:
      throw LLDBParserError{"Invalid type class for pointer type: " + std::to_string(kind)};
  }

  if (pointeeType.IsFunctionType()) {
   return makeType<Primitive>(type, Primitive::Kind::StubbedPointer);
  }

  Type& t = enumerateType(pointeeType);
  return makeType<Pointer>(type, t);
}

bool LLDBParser::chasePointer() const {
  // Always chase top-level pointers.
  if (depth_ == 1)
    return true;
  return options_.chaseRawPointers;
}

Primitive::Kind LLDBParser::primitiveIntKind(lldb::SBType& type, bool is_signed) {
  switch (auto size = type.GetByteSize()) {
    case 1:
      return is_signed ? Primitive::Kind::Int8 : Primitive::Kind::UInt8;
    case 2:
      return is_signed ? Primitive::Kind::Int16 : Primitive::Kind::UInt16;
    case 4:
      return is_signed ? Primitive::Kind::Int32 : Primitive::Kind::UInt32;
    case 8:
      return is_signed ? Primitive::Kind::Int64 : Primitive::Kind::UInt64;
    default:
      throw LLDBParserError{"Invalid integer size: " + std::to_string(size)};
  }
}
Primitive::Kind LLDBParser::primitiveFloatKind(lldb::SBType& type) {
  switch (auto size = type.GetByteSize()) {
    case 4:
      return Primitive::Kind::Float32;
    case 8:
      return Primitive::Kind::Float64;
    case 16:
      return Primitive::Kind::Float128;
    default:
      throw LLDBParserError{"Invalid float size: " + std::to_string(size)};
  }
}

Array& LLDBParser::enumerateArray(lldb::SBType& type) {
  auto elementType = type.GetArrayElementType();
  /* TODO: Is there a better way to get the array size? */
  auto len = type.GetByteSize() / elementType.GetByteSize();
  auto& t = enumerateType(elementType);
  return makeType<Array>(type, t, len);
}

Primitive& LLDBParser::enumeratePrimitive(lldb::SBType& type) {
  Primitive::Kind primitiveKind = Primitive::Kind::Void;

  switch (auto kind = type.GetBasicType()) {
    /* TODO: Do we need to handle char differently from int? */
    case lldb::eBasicTypeChar:
    case lldb::eBasicTypeSignedChar:
    case lldb::eBasicTypeWChar:
    case lldb::eBasicTypeSignedWChar:
    case lldb::eBasicTypeChar16:
    case lldb::eBasicTypeChar32:
    case lldb::eBasicTypeChar8:
    case lldb::eBasicTypeShort:
    case lldb::eBasicTypeInt:
    case lldb::eBasicTypeLong:
    case lldb::eBasicTypeLongLong:
    case lldb::eBasicTypeInt128:
      primitiveKind = primitiveIntKind(type, true);
      break;
    case lldb::eBasicTypeUnsignedChar:
    case lldb::eBasicTypeUnsignedWChar:
    case lldb::eBasicTypeUnsignedShort:
    case lldb::eBasicTypeUnsignedInt:
    case lldb::eBasicTypeUnsignedLong:
    case lldb::eBasicTypeUnsignedLongLong:
    case lldb::eBasicTypeUnsignedInt128:
      primitiveKind = primitiveIntKind(type, false);
      break;
    case lldb::eBasicTypeHalf:
    case lldb::eBasicTypeFloat:
    case lldb::eBasicTypeDouble:
    case lldb::eBasicTypeLongDouble:
      primitiveKind = primitiveFloatKind(type);
      break;
    case lldb::eBasicTypeBool:
      primitiveKind = Primitive::Kind::Bool;
      break;
    case lldb::eBasicTypeVoid:
    case lldb::eBasicTypeNullPtr:
      primitiveKind = Primitive::Kind::Void;
      break;
    case lldb::eBasicTypeInvalid:
    case lldb::eBasicTypeFloatComplex:
    case lldb::eBasicTypeDoubleComplex:
    case lldb::eBasicTypeLongDoubleComplex:
    case lldb::eBasicTypeObjCID:
    case lldb::eBasicTypeObjCClass:
    case lldb::eBasicTypeObjCSel:
    case lldb::eBasicTypeOther:
    default:
      throw LLDBParserError{"Unhandled primitive type: " + std::to_string(kind)};
  }

  return makeType<Primitive>(type, primitiveKind);
}

}  // namespace oi::detail::type_graph
