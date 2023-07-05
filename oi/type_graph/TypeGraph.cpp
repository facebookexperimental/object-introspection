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
#include "TypeGraph.h"

namespace type_graph {

template <>
Primitive* TypeGraph::makeType<Primitive>(Primitive::Kind kind) {
  switch (kind) {
    case Primitive::Kind::Int8:
      static Primitive pInt8{kind};
      return &pInt8;
    case Primitive::Kind::Int16:
      static Primitive pInt16{kind};
      return &pInt16;
    case Primitive::Kind::Int32:
      static Primitive pInt32{kind};
      return &pInt32;
    case Primitive::Kind::Int64:
      static Primitive pInt64{kind};
      return &pInt64;
    case Primitive::Kind::UInt8:
      static Primitive pUInt8{kind};
      return &pUInt8;
    case Primitive::Kind::UInt16:
      static Primitive pUInt16{kind};
      return &pUInt16;
    case Primitive::Kind::UInt32:
      static Primitive pUInt32{kind};
      return &pUInt32;
    case Primitive::Kind::UInt64:
      static Primitive pUInt64{kind};
      return &pUInt64;
    case Primitive::Kind::Float32:
      static Primitive pFloat32{kind};
      return &pFloat32;
    case Primitive::Kind::Float64:
      static Primitive pFloat64{kind};
      return &pFloat64;
    case Primitive::Kind::Float80:
      static Primitive pFloat80{kind};
      return &pFloat80;
    case Primitive::Kind::Float128:
      static Primitive pFloat128{kind};
      return &pFloat128;
    case Primitive::Kind::Bool:
      static Primitive pBool{kind};
      return &pBool;
    case Primitive::Kind::UIntPtr:
      static Primitive pUIntPtr{kind};
      return &pUIntPtr;
    case Primitive::Kind::Void:
      static Primitive pVoid{kind};
      return &pVoid;
  }
}

}  // namespace type_graph
