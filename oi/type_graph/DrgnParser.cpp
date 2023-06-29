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

#include <regex>

namespace type_graph {
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

}  // namespace

// TODO type stubs

Type* DrgnParser::parse(struct drgn_type* root) {
  depth_ = 0;
  return enumerateType(root);
}

Type* DrgnParser::enumerateType(struct drgn_type* type) {
  // Avoid re-enumerating an already-processsed type
  if (auto it = drgn_types_.find(type); it != drgn_types_.end())
    return it->second;

  if (!drgn_utils::isSizeComplete(type)) {
    return make_type<Primitive>(nullptr, Primitive::Kind::Void);
  }

  enum drgn_type_kind kind = drgn_type_kind(type);
  Type* t = nullptr;
  depth_++;
  switch (kind) {
    case DRGN_TYPE_CLASS:
    case DRGN_TYPE_STRUCT:
    case DRGN_TYPE_UNION:
      t = enumerateClass(type);
      break;
    case DRGN_TYPE_ENUM:
      t = enumerateEnum(type);
      break;
    case DRGN_TYPE_TYPEDEF:
      t = enumerateTypedef(type);
      break;
    case DRGN_TYPE_POINTER:
      t = enumeratePointer(type);
      break;
    case DRGN_TYPE_ARRAY:
      t = enumerateArray(type);
      break;
    case DRGN_TYPE_INT:
    case DRGN_TYPE_BOOL:
    case DRGN_TYPE_FLOAT:
    case DRGN_TYPE_VOID:
      t = enumeratePrimitive(type);
      break;
    default:
      throw DrgnParserError{"Unknown drgn type kind: " + std::to_string(kind)};
  }
  depth_--;

  return t;
}

Container* DrgnParser::enumerateContainer(struct drgn_type* type,
                                          const std::string& fqName) {
  auto size = get_drgn_type_size(type);

  for (const auto& containerInfo : containers_) {
    if (!std::regex_search(fqName, containerInfo.matcher)) {
      continue;
    }

    VLOG(2) << "Matching container `" << containerInfo.typeName << "` from `"
            << fqName << "`" << std::endl;
    auto* c = make_type<Container>(type, containerInfo, size);
    enumerateClassTemplateParams(type, c->templateParams);
    return c;
  }
  return nullptr;
}

Type* DrgnParser::enumerateClass(struct drgn_type* type) {
  std::string fqName;
  char* nameStr = nullptr;
  size_t length = 0;
  auto* err = drgn_type_fully_qualified_name(type, &nameStr, &length);
  if (err == nullptr && nameStr != nullptr) {
    fqName = nameStr;
  }

  auto* container = enumerateContainer(type, fqName);
  if (container)
    return container;

  std::string name;
  const char* type_tag = drgn_type_tag(type);
  if (type_tag)
    name = std::string(type_tag);
  // else this is an anonymous type

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

  auto c = make_type<Class>(type, kind, std::move(name), std::move(fqName),
                            size, virtuality);

  enumerateClassTemplateParams(type, c->templateParams);
  enumerateClassParents(type, c->parents);
  enumerateClassMembers(type, c->members);
  enumerateClassFunctions(type, c->functions);

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
      throw DrgnParserError{
          "Error looking up parent type (" + std::to_string(i) + ")", err};
    }

    auto ptype = enumerateType(parent_qual_type.type);
    uint64_t poffset = drgn_parents[i].bit_offset;
    Parent p(ptype, poffset);
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
      throw DrgnParserError{
          "Error looking up member type (" + std::to_string(i) + ")", err};
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

    auto mtype = enumerateType(member_type);
    uint64_t moffset = drgn_members[i].bit_offset;

    Member m{mtype, member_name, moffset, bit_field_size};
    members.push_back(m);
  }

  std::sort(members.begin(), members.end(), [](const auto& a, const auto& b) {
    return a.bitOffset < b.bitOffset;
  });
}

void DrgnParser::enumerateTemplateParam(drgn_type_template_parameter* tparams,
                                        size_t i,
                                        std::vector<TemplateParam>& params) {
  const drgn_object* obj = nullptr;
  if (auto* err = drgn_template_parameter_object(&tparams[i], &obj)) {
    throw DrgnParserError{"Error looking up template parameter object (" +
                              std::to_string(i) + ")",
                          err};
  }

  struct drgn_qualified_type tparamQualType;
  if (obj == nullptr) {
    // This template parameter is a typename
    struct drgn_error* err =
        drgn_template_parameter_type(&tparams[i], &tparamQualType);
    if (err) {
      throw DrgnParserError{"Error looking up template parameter type (" +
                                std::to_string(i) + ")",
                            err};
    }

    struct drgn_type* tparamType = tparamQualType.type;

    QualifierSet qualifiers;
    qualifiers[Qualifier::Const] =
        (tparamQualType.qualifiers & DRGN_QUALIFIER_CONST);

    auto ttype = enumerateType(tparamType);
    params.emplace_back(ttype, qualifiers);
  } else {
    // This template parameter is a value
    std::string value;

    if (drgn_type_kind(obj->type) == DRGN_TYPE_ENUM) {
      char* nameStr = nullptr;
      size_t length = 0;
      auto* err = drgn_type_fully_qualified_name(obj->type, &nameStr, &length);
      if (err != nullptr || nameStr == nullptr) {
        throw DrgnParserError{"Failed to get enum's fully qualified name", err};
      }

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
          value = std::string{nameStr} + "::" + enumerators[j].name;
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

    params.emplace_back(std::move(value));
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
    enumerateTemplateParam(tparams, i, params);
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
      LOG(WARNING) << "Error looking up member function (" + std::to_string(i) +
                          "): " + std::to_string(err->code) + " " +
                          err->message;
      drgn_error_destroy(err);
      continue;
    }

    auto virtuality = drgn_type_virtuality(t.type);
    std::string name = drgn_type_tag(t.type);
    Function f(name, virtuality);
    functions.push_back(f);
  }
}

Enum* DrgnParser::enumerateEnum(struct drgn_type* type) {
  // TODO anonymous enums
  // TODO incomplete enum?
  std::string name = drgn_type_tag(type);
  uint64_t size = get_drgn_type_size(type);
  ;
  return make_type<Enum>(type, name, size);
}

Typedef* DrgnParser::enumerateTypedef(struct drgn_type* type) {
  std::string name = drgn_type_name(type);
  // TODO anonymous typedefs?

  struct drgn_type* underlyingType = drgn_type_type(type).type;
  auto t = enumerateType(underlyingType);
  return make_type<Typedef>(type, name, t);
}

Type* DrgnParser::enumeratePointer(struct drgn_type* type) {
  if (!chasePointer()) {
    // TODO dodgy nullptr - primitives should be handled as singletons
    return make_type<Primitive>(nullptr, Primitive::Kind::UIntPtr);
  }

  struct drgn_type* pointeeType = drgn_type_type(type).type;

  // TODO why was old CodeGen following funciton pointers?

  Type* t = enumerateType(pointeeType);
  return make_type<Pointer>(type, t);
}

Array* DrgnParser::enumerateArray(struct drgn_type* type) {
  struct drgn_type* elementType = drgn_type_type(type).type;
  uint64_t len = drgn_type_length(type);
  auto t = enumerateType(elementType);
  return make_type<Array>(type, t, len);
}

// TODO deduplication of primitive types (also remember they're not only created
// here)
Primitive* DrgnParser::enumeratePrimitive(struct drgn_type* type) {
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
  return make_type<Primitive>(type, kind);
}

bool DrgnParser::chasePointer() const {
  // Always chase top-level pointers
  if (depth_ == 1)
    return true;
  return chaseRawPointers_;
}

DrgnParserError::DrgnParserError(const std::string& msg, struct drgn_error* err)
    : std::runtime_error{msg + ": " + std::to_string(err->code) + " " +
                         err->message},
      err_(err) {
}

DrgnParserError::~DrgnParserError() {
  drgn_error_destroy(err_);
}

}  // namespace type_graph
