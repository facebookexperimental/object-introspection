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

// TODO: use LLDB's includes
namespace lldb {
class SBType;
}

namespace oi::detail::type_graph {

struct LLDBParserOptions {
  bool readEnumValues = false;
};

class LLDBParser {
 public:
  LLDBParser(TypeGraph& typeGraph, LLDBParserOptions options)
      : typeGraph_(typeGraph), options_(options) {
  }
  Type& parse(lldb::SBType* root);

 private:
  Type& enumerateType(lldb::SBType& type);
  Enum& enumerateEnum(lldb::SBType& type);

    template <typename T, typename... Args>
  T& makeType(lldb::SBType *lldbType, Args&&... args) {
    auto& newType = typeGraph_.makeType<T>(std::forward<Args>(args)...);
    //drgn_types_.insert({drgnType, newType});
    return newType;
  }

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
