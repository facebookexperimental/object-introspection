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
#include "DrgnExporter.h"

#include "AddPadding.h"
#include "TypeGraph.h"
#include "TypeIdentifier.h"
#include "oi/TypeHierarchy.h"

namespace oi::detail::type_graph {

Pass DrgnExporter::createPass(TypeHierarchy& th,
                              std::list<drgn_type>& drgnTypes) {
  auto fn = [&th, &drgnTypes](TypeGraph& typeGraph, NodeTracker&) {
    DrgnExporter pass{th, drgnTypes};
    for (auto& type : typeGraph.rootTypes()) {
      pass.accept(type);
    }
  };

  return Pass("DrgnExporter", fn);
}

drgn_type* DrgnExporter::accept(Type& type) {
  if (auto* t = tracker_.get(type))
    return t;

  auto* t = type.accept(*this);
  tracker_.set(type, t);
  return t;
}

drgn_type* DrgnExporter::visit(Incomplete& c) {
  return makeDrgnType(DRGN_TYPE_VOID, false, DRGN_C_TYPE_VOID, c);
}

drgn_type* DrgnExporter::visit(Class& c) {
  enum drgn_type_kind kind;
  switch (c.kind()) {
    case Class::Kind::Class:
      kind = DRGN_TYPE_CLASS;
      break;
    case Class::Kind::Struct:
      kind = DRGN_TYPE_STRUCT;
      break;
    case Class::Kind::Union:
      kind = DRGN_TYPE_UNION;
      break;
  }

  auto* drgnType = makeDrgnType(kind, true, DRGN_NOT_PRIMITIVE_TYPE, c);
  th_.classMembersMap.insert({drgnType, {}});

  for (const auto& mem : c.members) {
    if (mem.name.starts_with(AddPadding::MemberPrefix)) {
      continue;
    }

    drgn_type* memType = accept(mem.type());

    th_.classMembersMap[drgnType].push_back(DrgnClassMemberInfo{
        memType, mem.inputName, mem.bitOffset, mem.bitsize, false});

    if (const auto* container = dynamic_cast<const Container*>(&mem.type());
        container && container->containerInfo_.ctype == THRIFT_ISSET_TYPE) {
      th_.thriftIssetStructTypes.insert(drgnType);
    }

    if (dynamic_cast<const Incomplete*>(&mem.type())) {
      th_.knownDummyTypeList.insert(drgnType);
    }
  }

  return drgnType;
}

drgn_type* DrgnExporter::visit(Container& c) {
  auto* drgnType =
      makeDrgnType(DRGN_TYPE_CLASS, false, DRGN_NOT_PRIMITIVE_TYPE, c);

  // Do not add `shared_ptr<void>`, `unique_ptr<void>`, or `weak_ptr<void>` to
  // `containerTypeMap`
  if (c.containerInfo_.ctype == SHRD_PTR_TYPE ||
      c.containerInfo_.ctype == UNIQ_PTR_TYPE ||
      c.containerInfo_.ctype == WEAK_PTR_TYPE) {
    const auto& paramType = c.templateParams[0].type();
    if (auto* p = dynamic_cast<const Primitive*>(&paramType);
        p && p->kind() == Primitive::Kind::Void) {
      return drgnType;
    } else if (auto* i = dynamic_cast<const Incomplete*>(&paramType)) {
      return drgnType;
    }
  }

  std::vector<size_t> paramIdxs;
  if (c.containerInfo_.underlyingContainerIndex.has_value()) {
    paramIdxs.push_back(*c.containerInfo_.underlyingContainerIndex);
  } else {
    auto numTemplateParams = c.containerInfo_.numTemplateParams;
    if (!numTemplateParams.has_value())
      numTemplateParams = c.templateParams.size();
    for (size_t i = 0;
         i < std::min(*numTemplateParams, c.templateParams.size());
         i++) {
      paramIdxs.push_back(i);
    }
  }

  auto& templateTypes =
      th_.containerTypeMap
          .emplace(drgnType,
                   std::pair{c.containerInfo_.ctype,
                             std::vector<drgn_qualified_type>{}})
          .first->second.second;
  for (auto i : paramIdxs) {
    if (i >= c.templateParams.size()) {
      continue;
    }

    auto& param = c.templateParams[i];
    drgn_type* paramType = accept(param.type());
    templateTypes.push_back({paramType, drgn_qualifiers{}});

    if (dynamic_cast<const Incomplete*>(&param.type())) {
      th_.knownDummyTypeList.insert(drgnType);
    }
  }

  return drgnType;
}

drgn_type* DrgnExporter::visit(Primitive& p) {
  enum drgn_type_kind kind;
  switch (p.kind()) {
    case Primitive::Kind::Int8:
    case Primitive::Kind::Int16:
    case Primitive::Kind::Int32:
    case Primitive::Kind::Int64:
    case Primitive::Kind::UInt8:
    case Primitive::Kind::UInt16:
    case Primitive::Kind::UInt32:
    case Primitive::Kind::UInt64:
      kind = DRGN_TYPE_INT;
      break;
    case Primitive::Kind::Float32:
    case Primitive::Kind::Float64:
    case Primitive::Kind::Float80:
    case Primitive::Kind::Float128:
      kind = DRGN_TYPE_FLOAT;
      break;
    case Primitive::Kind::Bool:
      kind = DRGN_TYPE_BOOL;
      break;
    case Primitive::Kind::StubbedPointer:
    case Primitive::Kind::Void:
      kind = DRGN_TYPE_VOID;
      break;
  }
  // The exact drgn_primitive_type used doesn't matter for TreeBuilder. Just
  // pick DRGN_C_TYPE_INT for simplicity.
  return makeDrgnType(kind, false, DRGN_C_TYPE_INT, p);
}

drgn_type* DrgnExporter::visit(Enum& e) {
  return makeDrgnType(DRGN_TYPE_ENUM, false, DRGN_NOT_PRIMITIVE_TYPE, e);
}

drgn_type* DrgnExporter::visit(Array& a) {
  auto* drgnType =
      makeDrgnType(DRGN_TYPE_ARRAY, false, DRGN_NOT_PRIMITIVE_TYPE, a);
  drgnType->_private.length = a.len();
  drgnType->_private.type = accept(a.elementType());
  return drgnType;
}

drgn_type* DrgnExporter::visit(Typedef& td) {
  auto* drgnType =
      makeDrgnType(DRGN_TYPE_TYPEDEF, false, DRGN_NOT_PRIMITIVE_TYPE, td);
  auto* underlyingType = accept(td.underlyingType());
  th_.typedefMap[drgnType] = underlyingType;
  return drgnType;
}

drgn_type* DrgnExporter::visit(Pointer& p) {
  auto* drgnType =
      makeDrgnType(DRGN_TYPE_POINTER, false, DRGN_NOT_PRIMITIVE_TYPE, p);
  auto* pointeeType = accept(p.pointeeType());
  th_.pointerToTypeMap[drgnType] = pointeeType;
  return drgnType;
}

drgn_type* DrgnExporter::visit(Reference& p) {
  auto* drgnType =
      makeDrgnType(DRGN_TYPE_POINTER, false, DRGN_NOT_PRIMITIVE_TYPE, p);
  auto* pointeeType = accept(p.pointeeType());
  th_.pointerToTypeMap[drgnType] = pointeeType;
  return drgnType;
}

drgn_type* DrgnExporter::visit(Dummy& d) {
  return makeDrgnType(DRGN_TYPE_VOID, false, DRGN_C_TYPE_VOID, d);
}

drgn_type* DrgnExporter::visit(DummyAllocator& d) {
  return makeDrgnType(DRGN_TYPE_VOID, false, DRGN_C_TYPE_VOID, d);
}

drgn_type* DrgnExporter::visit(CaptureKeys&) {
  throw std::runtime_error("Feature not supported");
}

drgn_type* DrgnExporter::makeDrgnType(enum drgn_type_kind kind,
                                      bool is_complete,
                                      enum drgn_primitive_type primitive,
                                      const Type& type) {
  auto& drgnType = drgnTypes_.emplace_back();
  tracker_.set(type, &drgnType);

  drgnType._private.kind = kind;
  drgnType._private.is_complete = is_complete;
  drgnType._private.primitive = primitive;

  // Deliberately leaked to keep it alive for TreeBuilder
  char* name = strndup(type.inputName().data(), type.inputName().size());

  drgnType._private.name = name;
  drgnType._private.oi_size = type.size();
  drgnType._private.little_endian = true;
  drgnType._private.oi_name = name;

  return &drgnType;
}

}  // namespace oi::detail::type_graph
