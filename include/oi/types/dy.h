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

#ifndef OI_TYPES_DY_H
#define OI_TYPES_DY_H 1

/*
 * Dynamic Types
 *
 * These types are a dynamic (runtime) description of the types in
 * <oi/include/types/st.h>. We use static types to ensure that what is written
 * in the data segment is a known type. Dynamic types extend this to runtime,
 * allowing TreeBuilder to check that what it is reading out of the data segment
 * is what went in. Static types are written with heavy usage of templating so
 * they can be inlined and compiled to nothing in the JIT output. When
 * translating these types to OID/OITB they cannot be compiled in, so we
 * represent the template arguments with member fields instead.
 *
 * Each type in this namespace corresponds 1-1 with a type in oi::types::st,
 * except Dynamic which references them all. See the types in st.h for the
 * description of what each type contains.
 *
 * All types in this file include a constexpr constructor. This allows a single
 * extern const variable in the JIT code to include pointer references to other
 * dynamic elements. To map from a Static Type to a Dynamic Type, we store each
 * template parameter as a field or std::span in a field.
 */

#include <functional>
#include <span>
#include <variant>

namespace oi::types::dy {

class Unit;
class VarInt;
class Pair;
class Sum;
class List;

/*
 * Dynamic
 *
 * The cornerstone of the dynamic types is the Dynamic type, a variant which
 * holds a pointer of each different type. This is essentially a tagged pointer.
 */
using Dynamic = std::variant<std::reference_wrapper<const Unit>,
                             std::reference_wrapper<const VarInt>,
                             std::reference_wrapper<const Pair>,
                             std::reference_wrapper<const Sum>,
                             std::reference_wrapper<const List> >;

class Unit {};
class VarInt {};

class Pair {
 public:
  constexpr Pair(Dynamic first_, Dynamic second_)
      : first(first_), second(second_) {
  }

  Dynamic first;
  Dynamic second;
};

class Sum {
 public:
  template <size_t N>
  constexpr Sum(const std::array<Dynamic, N>& variants_) : variants(variants_) {
  }

  std::span<const Dynamic> variants;
};

class List {
 public:
  constexpr List(Dynamic element_) : element(element_) {
  }

  Dynamic element;
};

}  // namespace oi::types::dy

#endif
