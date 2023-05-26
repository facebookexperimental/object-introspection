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
#include "Types.h"

#include "Visitor.h"

namespace type_graph {

#define X(OI_TYPE_NAME)                              \
  void OI_TYPE_NAME::accept(Visitor& v) {            \
    v.visit(*this);                                  \
  }                                                  \
  void OI_TYPE_NAME::accept(ConstVisitor& v) const { \
    v.visit(*this);                                  \
  }
OI_TYPE_LIST
#undef X

std::string Primitive::name() const {
  switch (kind_) {
    case Kind::Int8:
      return "int8_t";
    case Kind::Int16:
      return "int16_t";
    case Kind::Int32:
      return "int32_t";
    case Kind::Int64:
      return "int64_t";
    case Kind::UInt8:
      return "uint8_t";
    case Kind::UInt16:
      return "uint16_t";
    case Kind::UInt32:
      return "uint32_t";
    case Kind::UInt64:
      return "uint64_t";
    case Kind::Float32:
      return "float";
    case Kind::Float64:
      return "double";
    case Kind::Float80:
      abort();
    case Kind::Float128:
      return "long double";
    case Kind::Bool:
      return "bool";
    case Kind::UIntPtr:
      return "uintptr_t";
    case Kind::Void:
      return "void";
  }
}

std::size_t Primitive::size() const {
  switch (kind_) {
    case Kind::Int8:
      return 1;
    case Kind::Int16:
      return 2;
    case Kind::Int32:
      return 4;
    case Kind::Int64:
      return 8;
    case Kind::UInt8:
      return 1;
    case Kind::UInt16:
      return 2;
    case Kind::UInt32:
      return 4;
    case Kind::UInt64:
      return 8;
    case Kind::Float32:
      return 4;
    case Kind::Float64:
      return 8;
    case Kind::Float80:
      abort();
    case Kind::Float128:
      return 16;
    case Kind::Bool:
      return 1;
    case Kind::UIntPtr:
      return sizeof(uintptr_t);
    case Kind::Void:
      return 0;
  }
}

/*
 * Returns true if the provided class is "dynamic".
 *
 * From the Itanium C++ ABI, a dynamic class is defined as:
 *   A class requiring a virtual table pointer (because it or its bases have
 *   one or more virtual member functions or virtual base classes).
 */
bool Class::isDynamic() const {
  if (virtuality() != 0 /*DW_VIRTUALITY_none*/) {
    // Virtual class - not fully supported by OI yet
    return true;
  }

  for (const auto& func : functions) {
    if (func.virtuality != 0 /*DW_VIRTUALITY_none*/) {
      // Virtual function
      return true;
    }
  }

  return false;
}

}  // namespace type_graph
