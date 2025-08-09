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
#include "DrgnParser.h"

#include <glog/logging.h>

#include "oi/ContainerInfo.h"
#include "oi/DrgnUtils.h"
#include "oi/SymbolService.h"

extern "C" {
#include <drgn.h>
}

namespace oi::detail::type_graph {
namespace {

uint64_t get_drgn_type_size(struct drgn_type* type) {
  uint64_t size;
  struct drgn_error* err = drgn_type_sizeof(type, &size);
  if (err)
    throw DrgnParserError{"Failed to get type size", err};
  return size;
}

Primitive::Kind primitiveIntKind(struct drgn_type* type) {
  auto size = get_drgn_type_size(type);

  bool is_signed = type->_private.is_signed;
  switch (size) {
    case 1:
      return is_signed ? Primitive::Kind::Int8 : Primitive::Kind::UInt8;
    case 2:
      return is_signed ? Primitive::Kind::Int16 : Primitive::Kind::UInt16;
    case 4:
      return is_signed ? Primitive::Kind::Int32 : Primitive::Kind::UInt32;
    case 8:
      return is_signed ? Primitive::Kind::Int64 : Primitive::Kind::UInt64;
    default:
      throw DrgnParserError{"Invalid integer size: " + std::to_string(size)};
  }
}

Primitive::Kind primitiveFloatKind(struct drgn_type* type) {
  auto size = get_drgn_type_size(type);

  switch (size) {
    case 4:
      return Primitive::Kind::Float32;
    case 8:
      return Primitive::Kind::Float64;
    case 16:
      return Primitive::Kind::Float128;
    default:
      throw DrgnParserError{"Invalid float size: " + std::to_string(size)};
  }
}

void warnForDrgnError(struct drgn_type* type,
                      const std::string& msg,
                      struct drgn_error* err);

std::string getDrgnFullyQualifiedName(struct drgn_type* type);

}  // namespace

Type& DrgnParser::parse(struct drgn_type* root) {
  depth_ = 0;
  return enumerateType(root);
}

Type& DrgnParser::enumerateType(struct drgn_type* type) {
  // Avoid re-enumerating an already-processsed type
  if (auto it = drgn_types_.find(type); it != drgn_types_.end())
    return it->second;

  bool isTypeIncomplete = !drgn_utils::isSizeComplete(type) &&
                          drgn_type_kind(type) != DRGN_TYPE_VOID;

  enum drgn_type_kind kind = drgn_type_kind(type);
  Type* t = nullptr;
  depth_++;
  try {
    switch (kind) {
      case DRGN_TYPE_CLASS:
      case DRGN_TYPE_STRUCT:
      case DRGN_TYPE_UNION:
        t = &enumerateClass(type);
        break;
      case DRGN_TYPE_ENUM:
        t = &enumerateEnum(type);
        break;
      case DRGN_TYPE_TYPEDEF:
        t = &enumerateTypedef(type);
        break;
      case DRGN_TYPE_POINTER:
        t = &enumeratePointer(type);
        break;
      case DRGN_TYPE_ARRAY:
        t = &enumerateArray(type);
        break;
      case DRGN_TYPE_INT:
      case DRGN_TYPE_BOOL:
      case DRGN_TYPE_FLOAT:
      case DRGN_TYPE_VOID:
        t = &enumeratePrimitive(type);
        break;
      case DRGN_TYPE_FUNCTION:
        t = &enumerateFunction(type);
        break;
      default:
        throw DrgnParserError{"Unknown drgn type kind: " +
                              std::to_string(kind)};
    }
  } catch (const DrgnParserError& e) {
    depth_--;
    if (isTypeIncomplete) {
      const char* typeName = nullptr;
      if (drgn_type_has_name(type)) {
        typeName = drgn_type_name(type);
      } else if (drgn_type_has_tag(type)) {
        typeName = drgn_type_tag(type);
      }
      if (typeName == nullptr)
        typeName = "<incomplete>";

      return makeType<Incomplete>(nullptr, typeName);
    } else {
      throw e;
    }
  }
  depth_--;

  if (isTypeIncomplete) {
    return makeType<Incomplete>(nullptr, *t);
  }

  return *t;
}

Class& DrgnParser::enumerateClass(struct drgn_type* type) {
  std::string fqName;
  char* nameStr = nullptr;
  size_t length = 0;
  // HACK: Leak this error. Freeing it causes a SEGV which suggests our
  // underlying implementation is bad.
  auto* err = drgn_type_fully_qualified_name(type, &nameStr, &length);
  if (err == nullptr && nameStr != nullptr) {
    fqName = nameStr;
  }

  const char* typeTag = drgn_type_tag(type);
  std::string name = typeTag ? typeTag : "";

  auto size = get_drgn_type_size(type);
  int virtuality = 0;
  if (drgn_type_has_virtuality(type)) {
    virtuality = drgn_type_virtuality(type);
  }

  Class::Kind kind;
  switch (drgn_type_kind(type)) {
    case DRGN_TYPE_CLASS:
      kind = Class::Kind::Class;
      break;
    case DRGN_TYPE_STRUCT:
      kind = Class::Kind::Struct;
      break;
    case DRGN_TYPE_UNION:
      kind = Class::Kind::Union;
      break;
    default:
      throw DrgnParserError{"Invalid drgn type kind for class: " +
                            std::to_string(drgn_type_kind(type))};
  }

  auto& c = makeType<Class>(
      type, kind, std::move(name), std::move(fqName), size, virtuality);

  enumerateClassTemplateParams(type, c.templateParams);
  enumerateClassParents(type, c.parents);
  enumerateClassMembers(type, c.members);
  enumerateClassFunctions(type, c.functions);

  return c;
}

void DrgnParser::enumerateClassParents(struct drgn_type* type,
                                       std::vector<Parent>& parents) {
  assert(parents.empty());
  size_t num_parents = drgn_type_num_parents(type);
  parents.reserve(num_parents);

  struct drgn_type_template_parameter* drgn_parents = drgn_type_parents(type);

  for (size_t i = 0; i < num_parents; i++) {
    struct drgn_qualified_type parent_qual_type;
    struct drgn_error* err =
        drgn_template_parameter_type(&drgn_parents[i], &parent_qual_type);
    if (err) {
      warnForDrgnError(
          type, "Error looking up parent (" + std::to_string(i) + ")", err);
      continue;
    }

    auto& ptype = enumerateType(parent_qual_type.type);
    uint64_t poffset = drgn_parents[i].bit_offset;
    Parent p{ptype, poffset};
    parents.push_back(p);
  }

  std::sort(parents.begin(), parents.end(), [](const auto& a, const auto& b) {
    return a.bitOffset < b.bitOffset;
  });
}

void DrgnParser::enumerateClassMembers(struct drgn_type* type,
                                       std::vector<Member>& members) {
  assert(members.empty());
  size_t num_members = drgn_type_num_members(type);
  members.reserve(num_members);

  struct drgn_type_member* drgn_members = drgn_type_members(type);
  for (size_t i = 0; i < num_members; i++) {
    struct drgn_qualified_type member_qual_type;
    uint64_t bit_field_size;
    struct drgn_error* err =
        drgn_member_type(&drgn_members[i], &member_qual_type, &bit_field_size);
    if (err) {
      warnForDrgnError(
          type, "Error looking up member (" + std::to_string(i) + ")", err);
      continue;
    }

    struct drgn_type* member_type = member_qual_type.type;

    //    if (err || !isDrgnSizeComplete(member_qual_type.type)) {
    //      if (err) {
    //        LOG(ERROR) << "Error when looking up member type " << err->code <<
    //        " "
    //                   << err->message << " " << typeName << " " <<
    //                   drgn_members[i].name;
    //      }
    //      VLOG(1) << "Type " << typeName
    //              << " has an incomplete member; stubbing...";
    //      knownDummyTypeList.insert(type);
    //      isStubbed = true;
    //      return;
    //    }
    std::string member_name = "";
    if (drgn_members[i].name)
      member_name = drgn_members[i].name;

    auto& mtype = enumerateType(member_type);
    uint64_t moffset = drgn_members[i].bit_offset;

    Member m{mtype, member_name, moffset, bit_field_size};
    members.push_back(m);
  }

  std::sort(members.begin(), members.end(), [](const auto& a, const auto& b) {
    return a.bitOffset < b.bitOffset;
  });
}

void DrgnParser::enumerateTemplateParam(struct drgn_type* type,
                                        drgn_type_template_parameter* tparams,
                                        size_t i,
                                        std::vector<TemplateParam>& params) {
  drgn_qualified_type tparamQualType;
  struct drgn_error* err =
      drgn_template_parameter_type(&tparams[i], &tparamQualType);
  if (err) {
    warnForDrgnError(
        type,
        "Error looking up template parameter type (" + std::to_string(i) + ")",
        err);
    return;
  }

  struct drgn_type* tparamType = tparamQualType.type;

  QualifierSet qualifiers;
  qualifiers[Qualifier::Const] =
      (tparamQualType.qualifiers & DRGN_QUALIFIER_CONST);

  auto& ttype = enumerateType(tparamType);

  const drgn_object* obj = nullptr;
  if (err = drgn_template_parameter_object(&tparams[i], &obj); err != nullptr) {
    warnForDrgnError(type,
                     "Error looking up template parameter object (" +
                         std::to_string(i) + ")",
                     err);
    return;
  }

  if (obj == nullptr) {
    params.emplace_back(ttype, qualifiers);
  } else {
    // This template parameter is a value
    std::string value;

    if (const auto* e = dynamic_cast<const Enum*>(&ttype)) {
      uint64_t enumVal;
      switch (obj->encoding) {
        case DRGN_OBJECT_ENCODING_SIGNED:
          enumVal = obj->value.svalue;
          break;
        case DRGN_OBJECT_ENCODING_UNSIGNED:
          enumVal = obj->value.uvalue;
          break;
        default:
          throw DrgnParserError{
              "Unknown template parameter object encoding format: " +
              std::to_string(obj->encoding)};
      }

      drgn_type_enumerator* enumerators = drgn_type_enumerators(obj->type);
      size_t numEnumerators = drgn_type_num_enumerators(obj->type);
      for (size_t j = 0; j < numEnumerators; j++) {
        if (enumerators[j].uvalue == enumVal) {
          value = e->inputName();
          value += "::";
          value += enumerators[j].name;
          break;
        }
      }

      if (value.empty()) {
        throw DrgnParserError{"Unable to find enum name for value: " +
                              std::to_string(enumVal)};
      }
    } else {
      switch (obj->encoding) {
        case DRGN_OBJECT_ENCODING_BUFFER: {
          uint64_t size = drgn_object_size(obj);
          char* buf = nullptr;
          if (size <= sizeof(obj->value.ibuf)) {
            buf = (char*)&(obj->value.ibuf);
          } else {
            buf = obj->value.bufp;
          }

          if (buf != nullptr) {
            value = std::string(buf);
          }
          break;
        }
        case DRGN_OBJECT_ENCODING_SIGNED:
          value = std::to_string(obj->value.svalue);
          break;
        case DRGN_OBJECT_ENCODING_UNSIGNED:
          value = std::to_string(obj->value.uvalue);
          break;
        case DRGN_OBJECT_ENCODING_FLOAT:
          value = std::to_string(obj->value.fvalue);
          break;
        default:
          throw DrgnParserError{
              "Unknown template parameter object encoding format: " +
              std::to_string(obj->encoding)};
      }
    }

    params.emplace_back(ttype, std::move(value));
  }
}

void DrgnParser::enumerateClassTemplateParams(
    struct drgn_type* type, std::vector<TemplateParam>& params) {
  assert(params.empty());
  size_t numParams = drgn_type_num_template_parameters(type);
  params.reserve(numParams);

  struct drgn_type_template_parameter* tparams =
      drgn_type_template_parameters(type);
  for (size_t i = 0; i < numParams; i++) {
    enumerateTemplateParam(type, tparams, i, params);
  }
}

void DrgnParser::enumerateClassFunctions(struct drgn_type* type,
                                         std::vector<Function>& functions) {
  assert(functions.empty());
  size_t num_functions = drgn_type_num_functions(type);
  functions.reserve(num_functions);

  drgn_type_member_function* drgn_functions = drgn_type_functions(type);
  for (size_t i = 0; i < num_functions; i++) {
    drgn_qualified_type t{};
    if (auto* err = drgn_member_function_type(&drgn_functions[i], &t)) {
      warnForDrgnError(
          type,
          "Error looking up member function (" + std::to_string(i) + ")",
          err);
      continue;
    }

    auto virtuality = drgn_type_virtuality(t.type);
    std::string name = drgn_type_tag(t.type);
    Function f(name, virtuality);
    functions.push_back(f);
  }
}

Enum& DrgnParser::enumerateEnum(struct drgn_type* type) {
  std::string fqName = getDrgnFullyQualifiedName(type);

  const char* typeTag = drgn_type_tag(type);
  std::string name = typeTag ? typeTag : "";
  uint64_t size = get_drgn_type_size(type);

  std::map<int64_t, std::string> enumeratorMap;
  if (options_.readEnumValues) {
    drgn_type_enumerator* enumerators = drgn_type_enumerators(type);
    size_t numEnumerators = drgn_type_num_enumerators(type);
    for (size_t i = 0; i < numEnumerators; i++) {
      // Treat all enum values as signed, under the assumption that -1 is more
      // likely to appear in practice than UINT_MAX
      int64_t val = enumerators[i].svalue;
      enumeratorMap[val] = enumerators[i].name;
    }
  }

  return makeType<Enum>(
      type, std::move(name), std::move(fqName), size, std::move(enumeratorMap));
}

Typedef& DrgnParser::enumerateTypedef(struct drgn_type* type) {
  std::string name = drgn_type_name(type);

  struct drgn_type* underlyingType = drgn_type_type(type).type;
  auto& t = enumerateType(underlyingType);
  return makeType<Typedef>(type, name, t);
}

static drgn_type* getPtrUnderlyingType(drgn_type* type) {
  drgn_type* underlyingType = type;

  while (drgn_type_kind(underlyingType) == DRGN_TYPE_POINTER ||
         drgn_type_kind(underlyingType) == DRGN_TYPE_TYPEDEF) {
    underlyingType = drgn_type_type(underlyingType).type;
  }

  return underlyingType;
}

Type& DrgnParser::enumeratePointer(struct drgn_type* type) {
  if (!chasePointer()) {
    return makeType<Primitive>(type, Primitive::Kind::StubbedPointer);
  }

  struct drgn_type* pointeeType = drgn_type_type(type).type;

  if (drgn_type_kind(getPtrUnderlyingType(type)) == DRGN_TYPE_FUNCTION) {
    return makeType<Primitive>(type, Primitive::Kind::StubbedPointer);
  }

  Type& t = enumerateType(pointeeType);
  return makeType<Pointer>(type, t);
}

Type& DrgnParser::enumerateFunction(struct drgn_type* type) {
  return makeType<Primitive>(type, Primitive::Kind::StubbedPointer);
}

Array& DrgnParser::enumerateArray(struct drgn_type* type) {
  struct drgn_type* elementType = drgn_type_type(type).type;
  uint64_t len = drgn_type_length(type);
  auto& t = enumerateType(elementType);
  return makeType<Array>(type, t, len);
}

Primitive& DrgnParser::enumeratePrimitive(struct drgn_type* type) {
  Primitive::Kind kind;
  switch (drgn_type_kind(type)) {
    case DRGN_TYPE_INT:
      kind = primitiveIntKind(type);
      break;
    case DRGN_TYPE_FLOAT:
      kind = primitiveFloatKind(type);
      break;
    case DRGN_TYPE_BOOL:
      kind = Primitive::Kind::Bool;
      break;
    case DRGN_TYPE_VOID:
      kind = Primitive::Kind::Void;
      break;
    default:
      throw DrgnParserError{"Invalid drgn type kind for primitive: " +
                            std::to_string(drgn_type_kind(type))};
  }
  return makeType<Primitive>(type, kind);
}

bool DrgnParser::chasePointer() const {
  // Always chase top-level pointers
  if (depth_ == 1)
    return true;
  return options_.chaseRawPointers;
}

DrgnParserError::DrgnParserError(const std::string& msg, struct drgn_error* err)
    : std::runtime_error{msg + ": " + std::to_string(err->code) + " " +
                         err->message},
      err_(err) {
}

DrgnParserError::~DrgnParserError() {
  drgn_error_destroy(err_);
}

namespace {
void warnForDrgnError(struct drgn_type* type,
                      const std::string& msg,
                      struct drgn_error* err) {
  const char* name = nullptr;
  if (drgn_type_has_tag(type))
    name = drgn_type_tag(type);
  else if (drgn_type_has_name(type))
    name = drgn_type_name(type);
  LOG(WARNING) << msg << (name ? std::string{" for type '"} + name + "'" : "")
               << ": " << err->code << " " << err->message;
  drgn_error_destroy(err);
}

std::string getDrgnFullyQualifiedName(drgn_type* type) {
  char* nameStr = nullptr;
  size_t length = 0;
  auto* err = drgn_type_fully_qualified_name(type, &nameStr, &length);
  if (err != nullptr)
    throw DrgnParserError("failed to get fully qualified name!", err);
  if (nameStr != nullptr)
    return nameStr;
  return {};
}

}  // namespace

}  // namespace oi::detail::type_graph
