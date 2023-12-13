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

  Type *t = nullptr;
  switch (auto kind = type.GetTypeClass()) {
    case lldb::eTypeClassEnumeration:
      t = &enumerateEnum(type);
      break;
    case lldb::eTypeClassPointer:
    case lldb::eTypeClassReference:
      t = &enumeratePointer(type);
      break;
    case lldb::eTypeClassInvalid:
    case lldb::eTypeClassArray:
    case lldb::eTypeClassBlockPointer:
    case lldb::eTypeClassBuiltin:
    case lldb::eTypeClassClass:
    case lldb::eTypeClassComplexFloat:
    case lldb::eTypeClassComplexInteger:
    case lldb::eTypeClassFunction:
    case lldb::eTypeClassMemberPointer:
    case lldb::eTypeClassObjCObject:
    case lldb::eTypeClassObjCInterface:
    case lldb::eTypeClassObjCObjectPointer:
    case lldb::eTypeClassStruct:
    case lldb::eTypeClassTypedef:
    case lldb::eTypeClassUnion:
    case lldb::eTypeClassVector:
    case lldb::eTypeClassOther:
    case lldb::eTypeClassAny:
      throw std::runtime_error("Unhandled type class: " + std::to_string(kind));
  }

  return *t;
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


}  // namespace oi::detail::type_graph
