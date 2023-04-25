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
#include "oi/Serialize.h"

#include <boost/format.hpp>
#include <boost/serialization/serialization.hpp>
#include <boost/serialization/version.hpp>
#include <stdexcept>

#include "oi/DrgnUtils.h"

namespace boost::serialization {

template <typename T>
void verify_version(const unsigned int version) {
  const auto expected_version = boost::serialization::version<T>::value;
  if (expected_version != version) {
    auto error = (boost::format("Failed to serialize type `%1%`, as the class "
                                "version did not match "
                                "(cache had version %2%"
                                ", but OID expected version %3%)") %
                  typeid(T).name() % version % expected_version)
                     .str();
    throw std::runtime_error(error);
  }
}

using iarchive = boost::archive::text_iarchive;
using oarchive = boost::archive::text_oarchive;

// The default value for `boost::serialization::version` for a class is 0
// if it is not specified via `BOOST_CLASS_VERSION`. Therefore the
// `static_assert` in the below macro prevents us from accidentally
// serializing a new class without explicitly setting a version for it.
#define INSTANCIATE_SERIALIZE(Type)                                    \
  static_assert(                                                       \
      boost::serialization::version<Type>::value > 0,                  \
      "No class version was defined for type `" #Type                  \
      "`, please add an invocation of `DEFINE_TYPE_VERSION` for this " \
      "type.");                                                        \
  template void serialize(iarchive&, Type&, const unsigned int);       \
  template void serialize(oarchive&, Type&, const unsigned int);

template <class Archive>
void serialize(Archive& ar, PaddingInfo& p, const unsigned int version) {
  verify_version<PaddingInfo>(version);
  ar& p.structSize;
  ar& p.paddingSize;
  ar& p.definition;
  ar& p.instancesCnt;
  ar& p.savingSize;
}

INSTANCIATE_SERIALIZE(PaddingInfo)

template <class Archive>
void serialize(Archive& ar, struct drgn_location_description& location,
               const unsigned int version) {
  verify_version<struct drgn_location_description>(version);
  ar& location.start;
  ar& location.end;
  ar& location.expr_size;
  if (Archive::is_loading::value) {
    // It is important to call `malloc` here instead of allocating with `new`
    // since these structs are usually allocated and deallocated directly by
    // `drgn`, which is written in C.
    location.expr =
        (const char*)malloc(sizeof(*location.expr) * location.expr_size);
  }
  ar& make_array<char>(const_cast<char*>(location.expr), location.expr_size);
}

INSTANCIATE_SERIALIZE(struct drgn_location_description)

template <class Archive>
void serialize(Archive& ar, struct drgn_object_locator& locator,
               const unsigned int version) {
  verify_version<struct drgn_object_locator>(version);
  ar& locator.module_start;
  ar& locator.module_end;
  ar& locator.module_bias;
  ar& locator.locations_size;
  ar& locator.frame_base_locations_size;
  if (Archive::is_loading::value) {
    // It is important to call `malloc` here instead of allocating with `new`
    // since these structs are usually allocated and deallocated directly by
    // `drgn`, which is written in C.
    locator.locations = (struct drgn_location_description*)malloc(
        sizeof(*locator.locations) * locator.locations_size);
    locator.frame_base_locations = (struct drgn_location_description*)malloc(
        sizeof(*locator.frame_base_locations) *
        locator.frame_base_locations_size);
  }
  ar& make_array<struct drgn_location_description>(locator.locations,
                                                   locator.locations_size);
  ar& make_array<struct drgn_location_description>(
      locator.frame_base_locations, locator.frame_base_locations_size);
  ar& locator.qualified_type;
}

INSTANCIATE_SERIALIZE(struct drgn_object_locator)

template <class Archive>
void serialize(Archive& ar, FuncDesc::Arg& arg, const unsigned int version) {
  verify_version<FuncDesc::Arg>(version);
  ar& arg.typeName;
  ar& arg.valid;
  ar& arg.locator;
}

INSTANCIATE_SERIALIZE(FuncDesc::Arg)

template <class Archive>
void serialize(Archive& ar, FuncDesc::Retval& retval,
               const unsigned int version) {
  verify_version<FuncDesc::Retval>(version);
  ar& retval.typeName;
  ar& retval.valid;
}

INSTANCIATE_SERIALIZE(FuncDesc::Retval)

template <class Archive>
void serialize(Archive& ar, FuncDesc::Range& range,
               const unsigned int version) {
  verify_version<FuncDesc::Range>(version);
  ar& range.start;
  ar& range.end;
}

INSTANCIATE_SERIALIZE(FuncDesc::Range)

template <class Archive>
void serialize(Archive& ar, FuncDesc& fd, const unsigned int version) {
  verify_version<FuncDesc>(version);
  ar& fd.symName;
  ar& fd.ranges;
  ar& fd.isMethod;
  ar& fd.arguments;
  ar& fd.retval;
}

INSTANCIATE_SERIALIZE(FuncDesc)

template <class Archive>
void serialize(Archive& ar, GlobalDesc& gd, const unsigned int version) {
  verify_version<GlobalDesc>(version);
  ar& gd.symName;
  ar& gd.typeName;
  ar& gd.baseAddr;
}

INSTANCIATE_SERIALIZE(GlobalDesc)

template <class Archive>
static void serialize_c_string(Archive& ar, char** string) {
  size_t length;
  if (Archive::is_saving::value) {
    length = *string ? strlen(*string) : 0;
  }
  ar& length;
  if (Archive::is_loading::value) {
    *string = length ? (char*)malloc(sizeof(char) * (length + 1)) : NULL;
  }
  if (length > 0) {
    ar& make_array<char>(*string, length + 1);
  }
}

// ################################# CAUTION #################################
// The below code is *very* defensive and *very* precisely structured. Please
// DO NOT modify it without careful consideration and deliberation, as you
// are liable to break things otherwise. Something as simple as changing the
// order of two lines can cause cache corruption, so please make sure you know
// what you're doing (or ask someone who does) before touching anything.
// ###########################################################################
template <class Archive>
void serialize(Archive& ar, struct drgn_type& type,
               const unsigned int version) {
#define assert_in_same_union(member_1, member_2)               \
  do {                                                         \
    _Pragma("GCC diagnostic push");                            \
    _Pragma("GCC diagnostic ignored \"-Wpedantic\"");          \
    static_assert(offsetof(typeof(type._private), member_1) == \
                  offsetof(typeof(type._private), member_2));  \
    _Pragma("GCC diagnostic pop");                             \
  } while (0)

  verify_version<struct drgn_type>(version);

  // Ensure any unused fields are zeroed out for safety, as to avoid subtle
  // bugs resulting from mistakenly unserialized fields containing garbage.
  if (Archive::is_loading::value) {
    memset(&type, 0, sizeof(type));
  }

  // For the most part, we serialize fields in the order they are declared to
  // make it easy to visually confirm that we haven't missed anything.
  //
  // `.kind` MUST be serialized first, not just because it's declared first,
  // but because all of the `drgn_type_has_*` functions rely on the value of
  // `.kind`
  ar& type._private.kind;
  ar& type._private.is_complete;
  ar& type._private.primitive;
  ar& type._private.qualifiers;
  // `.program` is NULL, per the initial `memset`

  if (Archive::is_loading::value) {
    type._private.language = &drgn_language_cpp;
  }

  // AVOIDING OVERSERIALIZATION:
  // Many drgn structures contain pointers to `struct drgn_type`. We avoid
  // serializing these structures (and therefore avoid recursively serializing
  // `struct drgn_type`s in order to avoid serializing massive amounts of data.
  // In other words, our serialization of `struct drgn_type` is shallow.

  // First union: `name`, `tag`
  assert_in_same_union(name, tag);
  if (drgn_type_has_name(&type)) {
    serialize_c_string(ar, const_cast<char**>(&type._private.name));
  } else if (drgn_type_has_tag(&type)) {
    serialize_c_string(ar, const_cast<char**>(&type._private.tag));
  }

  // Second union: `size`, `length`, `num_enumerators`, `is_variadic`
  assert_in_same_union(size, length);
  assert_in_same_union(size, num_enumerators);
  assert_in_same_union(size, is_variadic);
  if (drgn_type_has_size(&type)) {
    ar& type._private.size;
  } else if (drgn_type_has_length(&type)) {
    ar& type._private.length;
  } else if (drgn_type_has_enumerators(&type)) {
    ar& type._private.num_enumerators;
  } else if (drgn_type_has_is_variadic(&type)) {
    ar& type._private.is_variadic;
  }

  // Third union: `is_signed`, `num_members`, `num_paramters`
  // Leave set to 0 per the initial `memset`, see "AVOIDING OVERSERIALIZATION"
  // comment above

  // Fourth union: `little_endian`, `members`, `enumerators`, `parameters`
  assert_in_same_union(little_endian, members);
  assert_in_same_union(little_endian, enumerators);
  assert_in_same_union(little_endian, parameters);
  if (drgn_type_has_little_endian(&type)) {
    ar& type._private.little_endian;
  } else if (drgn_type_has_members(&type)) {
    // Leave `members` set to NULL per the initial `memset`,
    // see "AVOIDING OVERSERIALIZATION" comment above
  } else if (drgn_type_has_enumerators(&type)) {
    // Leave `enumerators` set to NULL per the initial `memset`,
    // see "AVOIDING OVERSERIALIZATION" comment above
  } else if (drgn_type_has_parameters(&type)) {
    // Leave `parameters` set to NULL per the initial `memset`,
    // see "AVOIDING OVERSERIALIZATION" comment above
  }

  // Leave `template_parameters`, `parents`, `num_template_parameters`,
  // and `num_parents` set to NULL/0 per the initial `memset`, see
  // "AVOIDING OVERSERIALIZATION" comment above

  ar& type._private.die_addr;
  // `.module` is NULL, per the initial `memset`
  if (Archive::is_saving::value) {
    struct drgn_error* err = drgn_type_sizeof(&type, &type._private.oi_size);
    if (err) {
      drgn_error_destroy(err);
      type._private.oi_size =
          std::numeric_limits<decltype(type._private.oi_size)>::max();
    }
  }
  ar& type._private.oi_size;

  // It's important that `oi_name` is declared here and not inside the
  // if statement so that its data isn't freed when we call
  // `serialize_c_string`.
  std::string oi_name;
  if (Archive::is_saving::value) {
    oi_name = drgn_utils::typeToName(&type);
    type._private.oi_name = oi_name.c_str();
  }
  serialize_c_string(ar, const_cast<char**>(&type._private.oi_name));

  if (drgn_type_kind(&type) == DRGN_TYPE_ARRAY) {
    ar& type._private.type;
  }
#undef assert_in_same_union
}

INSTANCIATE_SERIALIZE(struct drgn_type)

template <class Archive>
void serialize(Archive& ar, struct DrgnClassMemberInfo& m,
               const unsigned int version) {
  verify_version<struct DrgnClassMemberInfo>(version);
  ar& m.type;
  ar& m.member_name;
  ar& m.bit_offset;
  ar& m.bit_field_size;
}

INSTANCIATE_SERIALIZE(DrgnClassMemberInfo)

template <class Archive>
void serialize(Archive& ar, struct drgn_qualified_type& type,
               const unsigned int version) {
  verify_version<struct drgn_qualified_type>(version);
  ar& type.type;
  ar& type.qualifiers;
}

INSTANCIATE_SERIALIZE(struct drgn_qualified_type)

template <class Archive>
void serialize(Archive& ar, RootInfo& rootInfo, const unsigned int version) {
  verify_version<RootInfo>(version);
  ar& rootInfo.varName;
  ar& rootInfo.type;
}

INSTANCIATE_SERIALIZE(RootInfo)

template <class Archive>
void serialize(Archive& ar, struct TypeHierarchy& th,
               const unsigned int version) {
  verify_version<TypeHierarchy>(version);
  ar& th.classMembersMap;
  ar& th.containerTypeMap;
  ar& th.typedefMap;
  ar& th.sizeMap;
  ar& th.knownDummyTypeList;
  ar& th.pointerToTypeMap;
  ar& th.thriftIssetStructTypes;
  ar& th.descendantClasses;
}

INSTANCIATE_SERIALIZE(struct TypeHierarchy)
// INSTANCIATE_SERIALIZE(std::map<struct drgn_type *, struct drgn_type *>)

}  // namespace boost::serialization
