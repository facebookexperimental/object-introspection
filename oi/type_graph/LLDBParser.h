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

#include "TypeGraph.h"
#include "Types.h"

#include <lldb/API/SBType.h>

namespace std {
/* lldb::SBType doesn't provide a hash and equality operators.
 * So we define our own specialization of them here.
 */
template <>
struct hash<lldb::SBType> {
  size_t operator()(const lldb::SBType& type) const;
};

template <>
struct equal_to<lldb::SBType> {
  bool operator()(const lldb::SBType& lhs, const lldb::SBType& rhs) const;
};
}  // namespace std

namespace oi::detail::type_graph {

struct LLDBParserOptions {
  bool chaseRawPointers = false;
  bool readEnumValues = false;
};

class LLDBParser {
 public:
  LLDBParser(TypeGraph& typeGraph, LLDBParserOptions options)
      : typeGraph_(typeGraph), options_(options) {
  }
  Type& parse(lldb::SBType& root);

 private:
  Type& enumerateType(lldb::SBType& type);
  Class& enumerateClass(lldb::SBType& type);
  Enum& enumerateEnum(lldb::SBType& type);
  Typedef& enumerateTypedef(lldb::SBType& type);
  Type& enumeratePointer(lldb::SBType& type);
  Array& enumerateArray(lldb::SBType& type);
  Primitive& enumeratePrimitive(lldb::SBType& type);

  Primitive::Kind primitiveIntKind(lldb::SBType& type, bool is_signed);
  Primitive::Kind primitiveFloatKind(lldb::SBType& type);

  void enumerateClassParents(lldb::SBType& type, std::vector<Parent>& parents);
  void enumerateClassMembers(lldb::SBType& type, std::vector<Member>& members);
  void enumerateClassFunctions(lldb::SBType &type, std::vector<Function>& functions);

  bool chasePointer() const;

  template <typename T, typename... Args>
  T& makeType(lldb::SBType& lldbType, Args&&... args) {
    auto& newType = typeGraph_.makeType<T>(std::forward<Args>(args)...);
    lldb_types_.emplace(lldbType, newType);
    return newType;
  }

  std::unordered_map<lldb::SBType, std::reference_wrapper<Type>> lldb_types_;

  TypeGraph& typeGraph_;
  int depth_;
  LLDBParserOptions options_;
};

class LLDBParserError : public std::runtime_error {
 public:
  LLDBParserError(const std::string& msg) : std::runtime_error{msg} {
  }
};

}  // namespace oi::detail::type_graph
