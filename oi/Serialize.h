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

#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/set.hpp>
#include <boost/serialization/shared_ptr.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/unordered_map.hpp>
#include <boost/serialization/vector.hpp>

#include "oi/PaddingHunter.h"
#include "oi/SymbolService.h"
#include "oi/TypeHierarchy.h"

#define DEFINE_TYPE_VERSION(Type, size, version)                             \
  static_assert(                                                             \
      sizeof(Type) == size,                                                  \
      "Type `" #Type                                                         \
      "` has changed, please update the `size` parameter and increment the " \
      "`version` parameter of the corresponding invocation "                 \
      "of `DEFINE_TYPE_VERSION` in " __FILE__);                              \
  BOOST_CLASS_VERSION(Type, version)

DEFINE_TYPE_VERSION(PaddingInfo, 120, 3)
DEFINE_TYPE_VERSION(struct drgn_location_description, 32, 2)
DEFINE_TYPE_VERSION(struct drgn_object_locator, 72, 2)
DEFINE_TYPE_VERSION(FuncDesc::Arg, 136, 3)
DEFINE_TYPE_VERSION(FuncDesc::Retval, 56, 2)
DEFINE_TYPE_VERSION(FuncDesc::Range, 16, 2)
DEFINE_TYPE_VERSION(FuncDesc, 104, 4)
DEFINE_TYPE_VERSION(GlobalDesc, 72, 4)
DEFINE_TYPE_VERSION(struct drgn_type, 160, 4)
DEFINE_TYPE_VERSION(DrgnClassMemberInfo, 64, 3)
DEFINE_TYPE_VERSION(struct drgn_qualified_type, 16, 2)
DEFINE_TYPE_VERSION(RootInfo, 48, 2)
DEFINE_TYPE_VERSION(TypeHierarchy, 384, 7)

#undef DEFINE_TYPE_VERSION

namespace boost::serialization {

#define DECL_SERIALIZE(Type) \
  template <class Archive>   \
  void serialize(Archive&, Type&, const unsigned int)

DECL_SERIALIZE(PaddingInfo);

DECL_SERIALIZE(FuncDesc::Arg);
DECL_SERIALIZE(FuncDesc);
DECL_SERIALIZE(GlobalDesc);

DECL_SERIALIZE(struct drgn_type);
DECL_SERIALIZE(struct drgn_qualified_type);
DECL_SERIALIZE(RootInfo);

DECL_SERIALIZE(DrgnClassMemberInfo);
DECL_SERIALIZE(TypeHierarchy);

#undef DECL_SERIALIZE

}  // namespace boost::serialization
