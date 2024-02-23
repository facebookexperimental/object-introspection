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
#include "CodeGen.h"

#include <glog/logging.h>

#include <boost/format.hpp>
#include <iostream>
#include <numeric>
#include <set>
#include <string_view>

#include "oi/FuncGen.h"
#include "oi/Headers.h"
#include "oi/SymbolService.h"
#include "type_graph/AddChildren.h"
#include "type_graph/AddPadding.h"
#include "type_graph/AlignmentCalc.h"
#include "type_graph/DrgnExporter.h"
#include "type_graph/DrgnParser.h"
#include "type_graph/EnforceCompatibility.h"
#include "type_graph/Flattener.h"
#include "type_graph/IdentifyContainers.h"
#include "type_graph/KeyCapture.h"
#include "type_graph/NameGen.h"
#include "type_graph/Prune.h"
#include "type_graph/RemoveMembers.h"
#include "type_graph/RemoveTopLevelPointer.h"
#include "type_graph/TopoSorter.h"
#include "type_graph/TypeIdentifier.h"
#include "type_graph/Types.h"

template <typename T>
inline constexpr bool always_false_v = false;

namespace oi::detail {

using type_graph::AddChildren;
using type_graph::AddPadding;
using type_graph::AlignmentCalc;
using type_graph::CaptureKeys;
using type_graph::Class;
using type_graph::Container;
using type_graph::DrgnParser;
using type_graph::DrgnParserOptions;
using type_graph::EnforceCompatibility;
using type_graph::Enum;
using type_graph::Flattener;
using type_graph::IdentifyContainers;
using type_graph::Incomplete;
using type_graph::KeyCapture;
using type_graph::Member;
using type_graph::NameGen;
using type_graph::Primitive;
using type_graph::Prune;
using type_graph::RemoveMembers;
using type_graph::RemoveTopLevelPointer;
using type_graph::TemplateParam;
using type_graph::TopoSorter;
using type_graph::Type;
using type_graph::Typedef;
using type_graph::TypeGraph;
using type_graph::TypeIdentifier;

template <typename T>
using ref = std::reference_wrapper<T>;

namespace {

std::vector<std::string_view> enumerateTypeNames(Type& type) {
  std::vector<std::string_view> names;
  Type* t = &type;

  if (const auto* ck = dynamic_cast<CaptureKeys*>(t))
    t = &ck->underlyingType();

  while (const Typedef* td = dynamic_cast<Typedef*>(t)) {
    names.emplace_back(t->inputName());
    t = &td->underlyingType();
  }
  names.emplace_back(t->inputName());
  return names;
}

void defineMacros(std::string& code) {
  if (true /* TODO: config.useDataSegment*/) {
    code += R"(
#define SAVE_SIZE(val)
#define SAVE_DATA(val)    StoreData(val, returnArg)
)";
  } else {
    code += R"(
#define SAVE_SIZE(val)    AddData(val, returnArg)
#define SAVE_DATA(val)
)";
  }
}

void defineInternalTypes(std::string& code) {
  code += R"(
template<typename T, int N>
struct OIArray {
  T vals[N];
};

// Just here to give a different type name to containers whose keys we'll capture
template <typename T>
struct OICaptureKeys : public T {
};
)";
}

void addIncludes(const TypeGraph& typeGraph,
                 FeatureSet features,
                 std::string& code) {
  std::set<std::string_view> includes{"cstddef"};
  if (features[Feature::TreeBuilderV2]) {
    code += "#define DEFINE_DESCRIBE 1\n";  // added before all includes

    includes.emplace("functional");
    includes.emplace("oi/exporters/inst.h");
    includes.emplace("oi/types/dy.h");
    includes.emplace("oi/types/st.h");
  }
  if (features[Feature::Library]) {
    includes.emplace("memory");
    includes.emplace("oi/IntrospectionResult.h");
    includes.emplace("vector");
  }
  if (features[Feature::JitTiming]) {
    includes.emplace("chrono");
  }
  for (const Type& t : typeGraph.finalTypes) {
    if (const auto* c = dynamic_cast<const Container*>(&t)) {
      includes.emplace(c->containerInfo_.header);
    }
  }
  for (const auto& include : includes) {
    code += "#include <";
    code += include;
    code += ">\n";
  }
}

void genDeclsClass(const Class& c, std::string& code) {
  if (c.kind() == Class::Kind::Union)
    code += "union ";
  else
    code += "struct ";
  code += c.name() + ";\n";
}

void genDeclsEnum(const Enum& e, std::string& code) {
  code += "enum class ";
  code += e.name();
  code += " : ";
  switch (e.size()) {
    case 8:
      code += "uint64_t";
      break;
    case 4:
      code += "uint32_t";
      break;
    case 2:
      code += "uint16_t";
      break;
    case 1:
      code += "uint8_t";
      break;
    default:
      abort();  // TODO
  }
  code += " {};\n";
}

void genDecls(const TypeGraph& typeGraph, std::string& code) {
  for (const Type& t : typeGraph.finalTypes) {
    if (const auto* c = dynamic_cast<const Class*>(&t)) {
      genDeclsClass(*c, code);
    } else if (const auto* e = dynamic_cast<const Enum*>(&t)) {
      genDeclsEnum(*e, code);
    }
  }
}

namespace {

size_t calculateExclusiveSize(const Type& t) {
  const Type* finalType = &t;
  while (const auto* td = dynamic_cast<const Typedef*>(finalType)) {
    finalType = &td->underlyingType();
  }

  if (const auto* c = dynamic_cast<const Class*>(finalType)) {
    return std::accumulate(
        c->members.cbegin(), c->members.cend(), 0, [](size_t a, const auto& m) {
          if (m.name.starts_with(AddPadding::MemberPrefix))
            return a + m.type().size();
          return a;
        });
  }
  return finalType->size();
}

}  // namespace

void genNames(const TypeGraph& typeGraph, std::string& code) {
  code += R"(
template <typename T>
struct NameProvider;
)";

  code += R"(
template <unsigned int N, unsigned int align, int32_t Id>
struct NameProvider<DummySizedOperator<N, align, Id>> {
  static constexpr std::array<std::string_view, 0> names = { };
};
)";

  // TODO: stop types being duplicated at this point and remove this check
  std::unordered_set<std::string_view> emittedTypes;
  for (const Type& t : typeGraph.finalTypes) {
    if (dynamic_cast<const Typedef*>(&t))
      continue;
    if (!emittedTypes.emplace(t.name()).second)
      continue;

    code += "template <> struct NameProvider<";
    code += t.name();
    code += "> { static constexpr std::array<std::string_view, 1> names = {\"";
    code += t.inputName();
    code += "\"}; };\n";
  }
}

void genExclusiveSizes(const TypeGraph& typeGraph, std::string& code) {
  code += R"(
template <typename T>
struct ExclusiveSizeProvider {
  static constexpr size_t size = sizeof(T);
};
)";

  for (const Type& t : typeGraph.finalTypes) {
    if (dynamic_cast<const Typedef*>(&t))
      continue;

    size_t exclusiveSize = calculateExclusiveSize(t);
    if (exclusiveSize != t.size()) {
      code += "template <> struct ExclusiveSizeProvider<";
      code += t.name();
      code += "> { static constexpr size_t size = ";
      code += std::to_string(exclusiveSize);
      code += "; };\n";
    }
  }
}

/*
 * Generates a declaration for a given fully-qualified type.
 *
 * e.g. Given "nsA::nsB::Foo"
 *
 * The folowing is generated:
 *   namespace nsA::nsB {
 *   struct Foo;
 *   }  // namespace nsA::nsB
 */
void declareFullyQualifiedStruct(const std::string& name, std::string& code) {
  if (auto pos = name.rfind("::"); pos != name.npos) {
    auto ns = name.substr(0, pos);
    auto structName = name.substr(pos + 2);
    code += "namespace ";
    code += ns;
    code += " {\n";
    code += "struct " + structName + ";\n";
    code += "} // namespace ";
    code += ns;
    code += "\n";
  } else {
    code += "struct ";
    code += name;
    code += ";\n";
  }
}

void genDefsThriftClass(const Class& c, std::string& code) {
  declareFullyQualifiedStruct(c.fqName(), code);
  code += "namespace apache { namespace thrift {\n";
  code += "template <> struct TStructDataStorage<" + c.fqName() + "> {\n";
  code +=
      "  static constexpr const std::size_t fields_size = 1; // Invalid, do "
      "not use\n";
  code +=
      "  static const std::array<folly::StringPiece, fields_size> "
      "fields_names;\n";
  code += "  static const std::array<int16_t, fields_size> fields_ids;\n";
  code +=
      "  static const std::array<protocol::TType, fields_size> fields_types;\n";
  code += "\n";
  code +=
      "  static const std::array<folly::StringPiece, fields_size> "
      "storage_names;\n";
  code +=
      "  static const std::array<int, fields_size> __attribute__((weak)) "
      "isset_indexes;\n";
  code += "};\n";
  code += "}} // namespace thrift, namespace apache\n";
}

}  // namespace

CodeGen::CodeGen(const OICodeGen::Config& config) : config_(config) {
  DCHECK(!config.features[Feature::PolymorphicInheritance])
      << "polymorphic inheritance requires symbol service!";
}

void CodeGen::genDefsThrift(const TypeGraph& typeGraph, std::string& code) {
  for (const Type& t : typeGraph.finalTypes) {
    if (const auto* c = dynamic_cast<const Class*>(&t)) {
      const Member* issetMember = nullptr;
      for (const auto& member : c->members) {
        if (const auto* container =
                dynamic_cast<const Container*>(&member.type());
            container && container->containerInfo_.ctype == THRIFT_ISSET_TYPE) {
          issetMember = &member;
          break;
        }
      }
      if (issetMember) {
        genDefsThriftClass(*c, code);
        thriftIssetMembers_[c] = issetMember;
      }
    }
  }
}

namespace {

void genDefsClass(const Class& c, std::string& code) {
  if (c.kind() == Class::Kind::Union)
    code += "union ";
  else
    code += "struct ";

  if (c.packed()) {
    code += "__attribute__((__packed__)) ";
  }

  if (c.members.size() == 1 &&
      c.members[0].name.starts_with(AddPadding::MemberPrefix)) {
    // Need to specify alignment manually for types which have been stubbed.
    // It would be nice to do this for all types, but our alignment information
    // is not complete, so it would result in some errors.
    //
    // Once we are able to read alignment info from DWARF, then this should be
    // able to be applied to everything.
    code += "alignas(" + std::to_string(c.align()) + ") ";
  }

  code += c.name() + " {\n";
  for (const auto& mem : c.members) {
    code += "  " + mem.type().name() + " " + mem.name;
    if (mem.bitsize) {
      code += " : " + std::to_string(mem.bitsize);
    }
    code += ";\n";
  }
  code += "};\n\n";
}

void genDefsTypedef(const Typedef& td, std::string& code) {
  code += "using " + td.name() + " = " + td.underlyingType().name() + ";\n";
}

void genDefs(const TypeGraph& typeGraph, std::string& code) {
  for (const Type& t : typeGraph.finalTypes) {
    if (const auto* c = dynamic_cast<const Class*>(&t)) {
      genDefsClass(*c, code);
    } else if (const auto* td = dynamic_cast<const Typedef*>(&t)) {
      genDefsTypedef(*td, code);
    }
  }
}

void genStaticAssertsClass(const Class& c, std::string& code) {
  code += "static_assert(validate_size<" + c.name() + ", " +
          std::to_string(c.size()) + ">::value);\n";
  for (const auto& member : c.members) {
    if (member.bitsize > 0)
      continue;

    code += "static_assert(validate_offset<offsetof(" + c.name() + ", " +
            member.name + "), " + std::to_string(member.bitOffset / 8) +
            ">::value, \"Unexpected offset of " + c.name() +
            "::" + member.name + "\");\n";
  }
  code.push_back('\n');
}

void genStaticAssertsContainer(const Container& c, std::string& code) {
  code += "static_assert(validate_size<" + c.name() + ", " +
          std::to_string(c.size()) + ">::value);\n";
  code.push_back('\n');
}

void genStaticAsserts(const TypeGraph& typeGraph, std::string& code) {
  for (const Type& t : typeGraph.finalTypes) {
    if (const auto* c = dynamic_cast<const Class*>(&t)) {
      genStaticAssertsClass(*c, code);
    } else if (const auto* con = dynamic_cast<const Container*>(&t)) {
      genStaticAssertsContainer(*con, code);
    }
  }
}

void addStandardGetSizeFuncDecls(std::string& code) {
  code += R"(
    template <typename T>
    void getSizeType(const T &t, size_t& returnArg);

    template<typename T>
    void getSizeType(/*const*/ T* s_ptr, size_t& returnArg);

    template <typename T, int N>
    void getSizeType(const OIArray<T,N>& container, size_t& returnArg);
  )";
}

void addStandardGetSizeFuncDefs(std::string& code) {
  // TODO use macros, not StoreData directly
  code += R"(
    template <typename T>
    void getSizeType(const T &t, size_t& returnArg) {
      JLOG("obj @");
      JLOGPTR(&t);
      SAVE_SIZE(sizeof(T));
    }
  )";
  // TODO const and non-const versions
  // OR maybe just remove const everywhere
  code += R"(
    template<typename T>
    void getSizeType(/*const*/ T* s_ptr, size_t& returnArg)
    {
      if constexpr (!oi_is_complete<T>) {
        JLOG("incomplete ptr @");
        JLOGPTR(s_ptr);
        StoreData((uintptr_t)(s_ptr), returnArg);
        return;
      } else {
        JLOG("ptr val @");
        JLOGPTR(s_ptr);
        StoreData((uintptr_t)(s_ptr), returnArg);
        if (s_ptr && ctx.pointers.add((uintptr_t)s_ptr)) {
          StoreData(1, returnArg);
          getSizeType(*(s_ptr), returnArg);
        } else {
          StoreData(0, returnArg);
        }
      }
    }

    template <typename T, int N>
    void getSizeType(const OIArray<T,N>& container, size_t& returnArg)
    {
      SAVE_DATA((uintptr_t)N);
      SAVE_SIZE(sizeof(container));

      for (size_t i=0; i<N; i++) {
          // undo the static size that has already been added per-element
          SAVE_SIZE(-sizeof(container.vals[i]));
          getSizeType(container.vals[i], returnArg);
      }
    }
  )";
}

void getClassSizeFuncDecl(const Class& c, std::string& code) {
  code += "void getSizeType(const " + c.name() + " &t, size_t &returnArg);\n";
}
}  // namespace

/*
 * Generates a getSizeType function for the given concrete class.
 *
 * Does not worry about polymorphism.
 */
void CodeGen::getClassSizeFuncConcrete(std::string_view funcName,
                                       const Class& c,
                                       std::string& code) const {
  code += "void " + std::string{funcName} + "(const " + c.name() +
          " &t, size_t &returnArg) {\n";

  const Member* thriftIssetMember = nullptr;
  if (const auto it = thriftIssetMembers_.find(&c);
      it != thriftIssetMembers_.end()) {
    thriftIssetMember = it->second;
  }

  if (thriftIssetMember) {
    code += "  using thrift_data = apache::thrift::TStructDataStorage<" +
            c.fqName() + ">;\n";
  }

  size_t thriftFieldIdx = 0;
  for (size_t i = 0; i < c.members.size(); i++) {
    const auto& member = c.members[i];
    if (member.name.starts_with(AddPadding::MemberPrefix))
      continue;

    if (thriftIssetMember && thriftIssetMember != &member) {
      // Capture Thrift's isset value for each field, except for __isset
      // itself
      std::string issetIdxStr = "thrift_data::isset_indexes[" +
                                std::to_string(thriftFieldIdx++) + "]";
      code += "  if (&thrift_data::isset_indexes != nullptr && " + issetIdxStr +
              " != -1) {\n";
      code += "    SAVE_DATA(t." + thriftIssetMember->name + ".get(" +
              issetIdxStr + "));\n";
      code += "  } else {\n";
      code += "    SAVE_DATA(-1);\n";
      code += "  }\n";
    }

    code += "  JLOG(\"" + member.name + " @\");\n";
    if (member.bitsize == 0)
      code += "  JLOGPTR(&t." + member.name + ");\n";
    code += "  getSizeType(t." + member.name + ", returnArg);\n";
  }
  code += "}\n";
}

void CodeGen::getClassSizeFuncDef(const Class& c, std::string& code) {
  if (!config_.features[Feature::PolymorphicInheritance] || !c.isDynamic()) {
    // Just directly use the concrete size function as this class' getSizeType()
    getClassSizeFuncConcrete("getSizeType", c, code);
    return;
  }

  getClassSizeFuncConcrete("getSizeTypeConcrete", c, code);

  std::vector<SymbolInfo> childVtableAddrs;
  childVtableAddrs.reserve(c.children.size());

  for (const Type& childType : c.children) {
    auto* childClass = dynamic_cast<const Class*>(&childType);
    if (childClass == nullptr) {
      abort();  // TODO
    }
    //      TODO:
    //      auto fqChildName = *fullyQualifiedName(child);
    auto fqChildName = "TODO - implement me";

    // We must split this assignment and append because the C++ standard lacks
    // an operator for concatenating std::string and std::string_view...
    std::string childVtableName = "vtable for ";
    childVtableName += fqChildName;

    auto optVtableSym = symbols_->locateSymbol(childVtableName, true);
    if (!optVtableSym) {
      //        LOG(ERROR) << "Failed to find vtable address for '" <<
      //        childVtableName; LOG(ERROR) << "Falling back to non dynamic
      //        mode";
      childVtableAddrs.clear();  // TODO why??
      break;
    }
    childVtableAddrs.push_back(*optVtableSym);
  }

  code += "void getSizeType(const " + c.name() + " &t, size_t &returnArg) {\n";
  code += "  auto *vptr = *reinterpret_cast<uintptr_t * const *>(&t);\n";
  code += "  uintptr_t topOffset = *(vptr - 2);\n";
  code += "  uintptr_t vptrVal = reinterpret_cast<uintptr_t>(vptr);\n";

  for (size_t i = 0; i < c.children.size(); i++) {
    // The vptr will point to *somewhere* in the vtable of this object's
    // concrete class. The exact offset into the vtable can vary based on a
    // number of factors, so we compare the vptr against the vtable range for
    // each possible class to determine the concrete type.
    //
    // This works for C++ compilers which follow the GNU v3 ABI, i.e. GCC and
    // Clang. Other compilers may differ.
    const Type& child = c.children[i];
    auto& vtableSym = childVtableAddrs[i];
    uintptr_t vtableMinAddr = vtableSym.addr;
    uintptr_t vtableMaxAddr = vtableSym.addr + vtableSym.size;
    code += "  if (vptrVal >= 0x" +
            (boost::format("%x") % vtableMinAddr).str() + " && vptrVal < 0x" +
            (boost::format("%x") % vtableMaxAddr).str() + ") {\n";
    code += "    SAVE_DATA(" + std::to_string(i) + ");\n";
    code +=
        "    uintptr_t baseAddress = reinterpret_cast<uintptr_t>(&t) + "
        "topOffset;\n";
    code += "    getSizeTypeConcrete(*reinterpret_cast<const " + child.name() +
            "*>(baseAddress), returnArg);\n";
    code += "    return;\n";
    code += "  }\n";
  }

  code += "  SAVE_DATA(-1);\n";
  code += "  getSizeTypeConcrete(t, returnArg);\n";
  code += "}\n";
}

namespace {
void getContainerSizeFuncDecl(const Container& c, std::string& code) {
  auto fmt =
      boost::format(c.containerInfo_.codegen.decl) % c.containerInfo_.typeName;
  code += fmt.str();
}

void getContainerSizeFuncDef(std::unordered_set<const ContainerInfo*>& used,
                             const Container& c,
                             std::string& code) {
  if (!used.insert(&c.containerInfo_).second) {
    return;
  }

  auto fmt =
      boost::format(c.containerInfo_.codegen.func) % c.containerInfo_.typeName;
  code += fmt.str();
}

void addGetSizeFuncDecls(const TypeGraph& typeGraph, std::string& code) {
  for (const Type& t : typeGraph.finalTypes) {
    if (const auto* c = dynamic_cast<const Class*>(&t)) {
      getClassSizeFuncDecl(*c, code);
    } else if (const auto* con = dynamic_cast<const Container*>(&t)) {
      getContainerSizeFuncDecl(*con, code);
    }
  }
}

}  // namespace

void CodeGen::addGetSizeFuncDefs(const TypeGraph& typeGraph,
                                 std::string& code) {
  for (const Type& t : typeGraph.finalTypes) {
    if (const auto* c = dynamic_cast<const Class*>(&t)) {
      getClassSizeFuncDef(*c, code);
    } else if (const auto* con = dynamic_cast<const Container*>(&t)) {
      getContainerSizeFuncDef(definedContainers_, *con, code);
    }
  }
}

namespace {

// Find the last member that isn't padding's index. Return -1 if no such member.
size_t getLastNonPaddingMemberIndex(const std::vector<Member>& members) {
  for (size_t i = members.size() - 1; i != (size_t)-1; --i) {
    const auto& el = members[i];
    if (!el.name.starts_with(AddPadding::MemberPrefix))
      return i;
  }
  return -1;
}

}  // namespace

// Generate the function body that walks the type. Uses the monadic
// `delegate()` form to handle each field except for the last. The last field
// instead uses `consume()` as we must not accidentally handle the first half
// of a pair as the last field.
void CodeGen::genClassTraversalFunction(const Class& c, std::string& code) {
  std::string funcName = "getSizeType";

  code += "  static types::st::Unit<DB> ";
  code += funcName;
  code += "(\n      Ctx& ctx,\n";
  code += "    const ";
  code += c.name();
  code += "& t,\n      typename TypeHandler<Ctx, ";
  code += c.name();
  code += ">::type returnArg) {\n";

  const Member* thriftIssetMember = nullptr;
  if (const auto it = thriftIssetMembers_.find(&c);
      it != thriftIssetMembers_.end()) {
    thriftIssetMember = it->second;
  }

  size_t emptySize = code.size();
  size_t lastNonPaddingElement = getLastNonPaddingMemberIndex(c.members);
  size_t thriftFieldIdx = 0;
  for (size_t i = 0; i < lastNonPaddingElement + 1; i++) {
    const auto& member = c.members[i];
    if (member.name.starts_with(AddPadding::MemberPrefix)) {
      continue;
    }

    if (code.size() == emptySize) {
      code += "    return returnArg";
    }

    if (thriftIssetMember != nullptr && thriftIssetMember != &member) {
      code += "\n      .write(getThriftIsset(t, ";
      code += std::to_string(thriftFieldIdx++);
      code += "))";
    }

    code += "\n      .";
    if (i == lastNonPaddingElement) {
      code += "consume";
    } else {
      code += "delegate";
    }
    code +=
        "([&ctx, &t](auto ret) { return OIInternal::getSizeType<Ctx>(ctx, t.";
    code += member.name;
    code += ", ret); })";
  }

  if (code.size() == emptySize) {
    code += "    return returnArg;";
  }
  code += ";\n  }\n";
}

// Generate the static type for the class's representation in the data buffer.
// For `class { int a,b,c; }` we generate (Ctx/DB omitted for clarity):
// Pair<TypeHandler<int>::type,
//   Pair<TypeHandler<int>::type,
//     TypeHandler<int>::type
// >>
void CodeGen::genClassStaticType(const Class& c, std::string& code) {
  const Member* thriftIssetMember = nullptr;
  if (const auto it = thriftIssetMembers_.find(&c);
      it != thriftIssetMembers_.end()) {
    thriftIssetMember = it->second;
  }

  size_t lastNonPaddingElement = getLastNonPaddingMemberIndex(c.members);
  size_t pairs = 0;

  size_t emptySize = code.size();
  for (size_t i = 0; i < lastNonPaddingElement + 1; i++) {
    const auto& member = c.members[i];
    if (member.name.starts_with(AddPadding::MemberPrefix)) {
      continue;
    }

    if (i != lastNonPaddingElement) {
      code += "types::st::Pair<DB, ";
      pairs++;
    }

    if (thriftIssetMember != nullptr && thriftIssetMember != &member) {
      // Return an additional VarInt before every field except for __isset
      // itself.
      pairs++;
      if (i == lastNonPaddingElement) {
        code += "types::st::Pair<DB, types::st::VarInt<DB>, ";
      } else {
        code += "types::st::VarInt<DB>, types::st::Pair<DB, ";
      }
    }

    code +=
        (boost::format("typename TypeHandler<Ctx, decltype(%1%::%2%)>::type") %
         c.name() % member.name)
            .str();

    if (i != lastNonPaddingElement) {
      code += ", ";
    }
  }
  code += std::string(pairs, '>');

  if (code.size() == emptySize) {
    code += "types::st::Unit<DB>";
  }
}

void CodeGen::genClassTreeBuilderInstructions(const Class& c,
                                              std::string& code) {
  const Member* thriftIssetMember = nullptr;
  if (const auto it = thriftIssetMembers_.find(&c);
      it != thriftIssetMembers_.end()) {
    thriftIssetMember = it->second;
  }

  code += " private:\n";
  size_t index = 0;
  for (const auto& m : c.members) {
    ++index;
    if (m.name.starts_with(AddPadding::MemberPrefix))
      continue;

    auto names = enumerateTypeNames(m.type());
    code += "  static constexpr std::array<std::string_view, " +
            std::to_string(names.size()) + "> member_" + std::to_string(index) +
            "_type_names = {";
    for (const auto& name : names) {
      code += "\"";
      code += name;
      code += "\",";
    }
    code += "};\n";
  }

  code += " public:\n";
  size_t numFields =
      std::count_if(c.members.cbegin(), c.members.cend(), [](const auto& m) {
        return !m.name.starts_with(AddPadding::MemberPrefix);
      });
  code += "  static constexpr std::array<inst::Field, ";
  code += std::to_string(numFields);
  code += "> fields{\n";
  index = 0;
  for (const auto& m : c.members) {
    ++index;
    if (m.name.starts_with(AddPadding::MemberPrefix))
      continue;
    std::string fullName = c.name() + "::" + m.name;
    bool isPrimitive = dynamic_cast<const Primitive*>(&m.type());
    code += "      inst::Field{sizeof(";
    code += fullName;
    code += "), ";
    code += std::to_string(calculateExclusiveSize(m.type()));
    code += ",\"";
    code += m.inputName;
    code += "\", member_";
    code += std::to_string(index);
    code += "_type_names, TypeHandler<Ctx, decltype(";
    code += fullName;
    code += ")>::fields, ";

    if (thriftIssetMember != nullptr && thriftIssetMember != &m) {
      code += "ThriftIssetHandler<";
    }
    code += "TypeHandler<Ctx, decltype(";
    code += fullName;
    code += ")>";
    if (thriftIssetMember != nullptr && thriftIssetMember != &m) {
      code += '>';
    }
    code += "::processors, ";

    code += isPrimitive ? "true" : "false";
    code += "},\n";
  }
  code += "  };\n";
  code +=
      "static constexpr std::array<exporters::inst::ProcessorInst, 0> "
      "processors{};\n";
}

void CodeGen::genClassTypeHandler(const Class& c, std::string& code) {
  std::string helpers;

  if (const auto it = thriftIssetMembers_.find(&c);
      it != thriftIssetMembers_.end()) {
    const Member& thriftIssetMember = *it->second;

    helpers += (boost::format(R"(
  static int getThriftIsset(const %1%& t, size_t i) {
    using thrift_data = apache::thrift::TStructDataStorage<%2%>;

    if (&thrift_data::isset_indexes == nullptr) return 2;

    auto idx = thrift_data::isset_indexes[i];
    if (idx == -1) return 2;

    return t.%3%.get(idx);
  }
)") % c.name() % c.fqName() %
                thriftIssetMember.name)
                   .str();
  }

  code += "template <typename Ctx>\n";
  code += "class TypeHandler<Ctx, ";
  code += c.name();
  code += "> {\n";
  code += "  using DB = typename Ctx::DataBuffer;\n";
  code += helpers;
  code += " public:\n";
  code += "  using type = ";
  genClassStaticType(c, code);
  code += ";\n";
  genClassTreeBuilderInstructions(c, code);
  genClassTraversalFunction(c, code);
  code += "};\n";
}

namespace {

void genContainerTypeHandler(std::unordered_set<const ContainerInfo*>& used,
                             const ContainerInfo& c,
                             std::span<const TemplateParam> templateParams,
                             std::string& code) {
  if (!used.insert(&c).second)
    return;

  code += c.codegen.extra;

  // TODO: Move this check into the ContainerInfo parsing once always enabled.
  const auto& func = c.codegen.traversalFunc;
  const auto& processors = c.codegen.processors;

  if (func.empty()) {
    LOG(ERROR)
        << "`codegen.traversal_func` must be specified for all containers "
           "under \"-ftree-builder-v2\", not specified for \"" +
               c.typeName + "\"";
    throw std::runtime_error("missing `codegen.traversal_func`");
  }

  std::string containerWithTypes = c.typeName;
  if (!templateParams.empty())
    containerWithTypes += '<';
  size_t types = 0, values = 0;
  for (const auto& p : templateParams) {
    if (types > 0 || values > 0)
      containerWithTypes += ", ";
    if (p.value) {
      containerWithTypes += "N" + std::to_string(values++);
    } else {
      containerWithTypes += "T" + std::to_string(types++);
    }
  }
  if (!templateParams.empty())
    containerWithTypes += '>';

  if (c.captureKeys) {
    containerWithTypes = "OICaptureKeys<" + containerWithTypes + ">";
  }

  // TODO: This is tech debt. This should be moved to a field in the container
  // spec/`.toml` called something like `codegen.handler_header` or have an
  // explicit option for variable template parameters. However I'm landing it
  // anyway to demonstrate how to handle tagged unions in TreeBuilder-v2.
  if (c.typeName == "std::variant") {
    code += R"(
template <typename Ctx, typename... Types>
struct TypeHandler<Ctx, std::variant<Types...>> {
  using container_type = std::variant<Types...>;
)";
  } else {
    code += "template <typename Ctx";
    types = 0, values = 0;
    for (const auto& p : templateParams) {
      if (p.value) {
        code += ", ";

        // HACK: forward all enums directly. this might turn out to be a problem
        // if there are enums we should be regenerating/use in the body.
        if (const auto* e = dynamic_cast<const Enum*>(&p.type())) {
          code += e->inputName();
        } else {
          code += p.type().name();
        }

        code += " N" + std::to_string(values++);
      } else {
        code += ", typename T" + std::to_string(types++);
      }
    }
    code += ">\n";
    code += "struct TypeHandler<Ctx, ";
    code += containerWithTypes;
    code += "> {\n";
    code += "  using container_type = ";
    code += containerWithTypes;
    code += ";\n";
  }

  code += "  using DB = typename Ctx::DataBuffer;\n";

  if (c.captureKeys) {
    code += "  static constexpr bool captureKeys = true;\n";
  } else {
    code += "  static constexpr bool captureKeys = false;\n";
  }

  code += "  using type = ";
  if (processors.empty()) {
    code += "types::st::Unit<DB>";
  } else {
    for (auto it = processors.cbegin(); it != processors.cend(); ++it) {
      if (it != processors.cend() - 1)
        code += "types::st::Pair<DB, ";
      code += it->type;
      if (it != processors.cend() - 1)
        code += ", ";
    }
    code += std::string(processors.size() - 1, '>');
  }
  code += ";\n";

  code += c.codegen.scopedExtra;

  code += "  static types::st::Unit<DB> getSizeType(\n";
  code += "      Ctx& ctx,\n";
  code += "      const container_type& container,\n";
  code +=
      "      typename TypeHandler<Ctx, container_type>::type returnArg) {\n";
  code += func;  // has rubbish indentation
  code += "  }\n";

  code += " private:\n";
  size_t count = 0;
  for (const auto& pr : processors) {
    code += "  static void processor_";
    code += std::to_string(count++);
    code +=
        "(result::Element& el, std::function<void(inst::Inst)> stack_ins, "
        "ParsedData d) {\n";
    code += pr.func;  // bad indentation
    code += "  }\n";
  }

  code += " public:\n";
  code +=
      "  static constexpr std::array<exporters::inst::Field, 0> fields{};\n";
  code += "  static constexpr std::array<exporters::inst::ProcessorInst, ";
  code += std::to_string(processors.size());
  code += "> processors{\n";
  count = 0;
  for (const auto& pr : processors) {
    code += "    exporters::inst::ProcessorInst{";
    code += pr.type;
    code += "::describe, &processor_";
    code += std::to_string(count++);
    code += "},\n";
  }
  code += "  };\n";
  code += "};\n\n";
}

void addCaptureKeySupport(std::string& code) {
  code += R"(
    template <typename Ctx, typename T>
    class CaptureKeyHandler {
      using DB = typename Ctx::DataBuffer;
     public:
      using type = types::st::Sum<DB, types::st::VarInt<DB>, types::st::VarInt<DB>>;

      static auto captureKey(const T& key, auto returnArg) {
        // Save scalars keys directly, otherwise save pointers for complex types
        if constexpr (std::is_scalar_v<T>) {
          return returnArg.template write<0>().write(static_cast<uint64_t>(key));
        }
        return returnArg.template write<1>().write(reinterpret_cast<uintptr_t>(&key));
      }
    };

    template <bool CaptureKeys, typename Ctx, typename T>
    auto maybeCaptureKey(Ctx& ctx, const T& key, auto returnArg) {
      if constexpr (CaptureKeys) {
        return returnArg.delegate([&key](auto ret) {
          return CaptureKeyHandler<Ctx, T>::captureKey(key, ret);
        });
      } else {
        return returnArg;
      }
    }

    template <typename Ctx, typename T>
    static constexpr inst::ProcessorInst CaptureKeysProcessor{
      CaptureKeyHandler<Ctx, T>::type::describe,
      [](result::Element& el, std::function<void(inst::Inst)> stack_ins, ParsedData d) {
        if constexpr (std::is_same_v<
            typename CaptureKeyHandler<Ctx, T>::type,
            types::st::List<typename Ctx::DataBuffer, types::st::VarInt<typename Ctx::DataBuffer>>>) {
          // String
          auto& str = el.data.emplace<std::string>();
          auto list = std::get<ParsedData::List>(d.val);
          size_t strlen = list.length;
          for (size_t i = 0; i < strlen; i++) {
            auto value = list.values().val;
            auto c = std::get<ParsedData::VarInt>(value).value;
            str.push_back(c);
          }
        } else {
          auto sum = std::get<ParsedData::Sum>(d.val);
          if (sum.index == 0) {
            el.data = oi::result::Element::Scalar{std::get<ParsedData::VarInt>(sum.value().val).value};
          } else {
            el.data = oi::result::Element::Pointer{std::get<ParsedData::VarInt>(sum.value().val).value};
          }
        }
      }
    };

    template <bool CaptureKeys, typename Ctx, typename T>
    static constexpr auto maybeCaptureKeysProcessor() {
      if constexpr (CaptureKeys) {
        return std::array<inst::ProcessorInst, 1>{
          CaptureKeysProcessor<Ctx, T>,
        };
      }
      else {
        return std::array<inst::ProcessorInst, 0>{};
      }
    }
  )";
}

void addThriftIssetSupport(std::string& code) {
  code += R"(
void processThriftIsset(result::Element& el, std::function<void(inst::Inst)> stack_ins, ParsedData d) {
  auto v = std::get<ParsedData::VarInt>(d.val).value;
  if (v <= 1) {
    el.is_set_stats.emplace(result::Element::IsSetStats { v == 1 });
  }
}
static constexpr exporters::inst::ProcessorInst thriftIssetProcessor{
  types::st::VarInt<int>::describe,
  &processThriftIsset,
};

template <typename Handler>
struct ThriftIssetHandler {
  static constexpr auto processors = arrayPrepend(Handler::processors, thriftIssetProcessor);
};
)";
}

void addStandardTypeHandlers(TypeGraph& typeGraph,
                             FeatureSet features,
                             std::string& code) {
  addCaptureKeySupport(code);
  if (features[Feature::CaptureThriftIsset])
    addThriftIssetSupport(code);

  // Provide a wrapper function, getSizeType, to infer T instead of having to
  // explicitly specify it with TypeHandler<Ctx, T>::getSizeType every time.
  code += R"(
    template <typename Ctx, typename T>
    types::st::Unit<typename Ctx::DataBuffer>
    getSizeType(Ctx& ctx, const T &t, typename TypeHandler<Ctx, T>::type returnArg) {
      JLOG("obj @");
      JLOGPTR(&t);
      return TypeHandler<Ctx, T>::getSizeType(ctx, t, returnArg);
    }
)";

  // TODO: bit of a hack - making ContainerInfo a node in the type graph and
  // traversing for it would remove the need for this set altogether.
  std::unordered_set<const ContainerInfo*> used{};
  std::vector<TemplateParam> arrayParams{
      TemplateParam{typeGraph.makeType<Primitive>(Primitive::Kind::UInt64)},
      TemplateParam{typeGraph.makeType<Primitive>(Primitive::Kind::UInt64),
                    "0"},
  };
  genContainerTypeHandler(
      used, FuncGen::GetOiArrayContainerInfo(), arrayParams, code);
}

}  // namespace

void CodeGen::addTypeHandlers(const TypeGraph& typeGraph, std::string& code) {
  for (const Type& t : typeGraph.finalTypes) {
    if (const auto* c = dynamic_cast<const Class*>(&t)) {
      genClassTypeHandler(*c, code);
    } else if (const auto* con = dynamic_cast<const Container*>(&t)) {
      genContainerTypeHandler(
          definedContainers_, con->containerInfo_, con->templateParams, code);
    } else if (const auto* cap = dynamic_cast<const CaptureKeys*>(&t)) {
      auto* container =
          dynamic_cast<Container*>(&(stripTypedefs(cap->underlyingType())));
      if (!container)
        throw std::runtime_error("KaptureKeys requires a container");

      genContainerTypeHandler(definedContainers_,
                              cap->containerInfo(),
                              container->templateParams,
                              code);
    }
  }
}

bool CodeGen::codegenFromDrgn(struct drgn_type* drgnType,
                              std::string linkageName,
                              std::string& code) {
  return codegenFromDrgn(drgnType, code, ExactName{std::move(linkageName)});
}

bool CodeGen::codegenFromDrgn(struct drgn_type* drgnType, std::string& code) {
  return codegenFromDrgn(
      drgnType, code, HashedComponent{SymbolService::getTypeName(drgnType)});
}

bool CodeGen::codegenFromDrgn(struct drgn_type* drgnType,
                              std::string& code,
                              RootFunctionName name) {
  if (!registerContainers())
    return false;

  try {
    addDrgnRoot(drgnType, typeGraph_);
  } catch (const type_graph::DrgnParserError& err) {
    LOG(ERROR) << "Error parsing DWARF: " << err.what();
    return false;
  }

  transform(typeGraph_);
  generate(typeGraph_, code, std::move(name));
  return true;
}

void CodeGen::exportDrgnTypes(TypeHierarchy& th,
                              std::list<drgn_type>& drgnTypes,
                              drgn_type** rootType) const {
  assert(typeGraph_.rootTypes().size() == 1);

  type_graph::DrgnExporter drgnExporter{th, drgnTypes};
  for (auto& type : typeGraph_.rootTypes()) {
    *rootType = drgnExporter.accept(type);
  }
}

bool CodeGen::registerContainers() {
  try {
    containerInfos_.reserve(config_.containerConfigPaths.size());
    for (const auto& path : config_.containerConfigPaths) {
      registerContainer(path);
    }
    return true;
  } catch (const ContainerInfoError& err) {
    LOG(ERROR) << "Error reading container TOML file " << err.what();
    return false;
  }
}

void CodeGen::registerContainer(std::unique_ptr<ContainerInfo> info) {
  VLOG(1) << "Registered container: " << info->typeName;
  containerInfos_.emplace_back(std::move(info));
}

void CodeGen::registerContainer(const fs::path& path) {
  auto info = std::make_unique<ContainerInfo>(path);
  if (info->requiredFeatures != (config_.features & info->requiredFeatures)) {
    VLOG(1) << "Skipping container (feature conflict): " << info->typeName;
    return;
  }
  registerContainer(std::move(info));
}

void CodeGen::addDrgnRoot(struct drgn_type* drgnType, TypeGraph& typeGraph) {
  DrgnParserOptions options{
      .chaseRawPointers = config_.features[Feature::ChaseRawPointers],
  };
  DrgnParser drgnParser{typeGraph, options};
  Type& parsedRoot = drgnParser.parse(drgnType);
  typeGraph.addRoot(parsedRoot);
}

void CodeGen::transform(TypeGraph& typeGraph) {
  type_graph::PassManager pm;

  // Simplify the type graph first so there is less work for later passes
  pm.addPass(RemoveTopLevelPointer::createPass());
  pm.addPass(IdentifyContainers::createPass(containerInfos_));
  pm.addPass(Flattener::createPass());
  pm.addPass(AlignmentCalc::createPass());
  pm.addPass(TypeIdentifier::createPass(config_.passThroughTypes));
  if (config_.features[Feature::PruneTypeGraph])
    pm.addPass(Prune::createPass());

  if (config_.features[Feature::PolymorphicInheritance]) {
    // Parse new children nodes
    DrgnParserOptions options{
        .chaseRawPointers = config_.features[Feature::ChaseRawPointers],
    };
    DrgnParser drgnParser{typeGraph, options};
    pm.addPass(AddChildren::createPass(drgnParser, *symbols_));

    // Re-run passes over newly added children
    pm.addPass(IdentifyContainers::createPass(containerInfos_));
    pm.addPass(Flattener::createPass());
    pm.addPass(AlignmentCalc::createPass());
    pm.addPass(TypeIdentifier::createPass(config_.passThroughTypes));
    if (config_.features[Feature::PruneTypeGraph])
      pm.addPass(Prune::createPass());
  }

  pm.addPass(RemoveMembers::createPass(config_.membersToStub));
  if (!config_.features[Feature::TreeBuilderV2])
    pm.addPass(EnforceCompatibility::createPass());
  if (config_.features[Feature::TreeBuilderV2] &&
      !config_.keysToCapture.empty())
    pm.addPass(KeyCapture::createPass(config_.keysToCapture, containerInfos_));

  // Add padding to fill in the gaps of removed members and ensure their
  // alignments
  pm.addPass(AddPadding::createPass());

  pm.addPass(NameGen::createPass());
  pm.addPass(TopoSorter::createPass());

  pm.run(typeGraph);

  LOG(INFO) << "Sorted types:\n";
  for (Type& t : typeGraph.finalTypes) {
    LOG(INFO) << "  " << t.name() << std::endl;
  };
}

void CodeGen::generate(TypeGraph& typeGraph,
                       std::string& code,
                       RootFunctionName rootName) {
  code = headers::oi_OITraceCode_cpp;
  if (!config_.features[Feature::Library]) {
    FuncGen::DeclareExterns(code);
  }
  if (!config_.features[Feature::TreeBuilderV2]) {
    defineMacros(code);
  }
  addIncludes(typeGraph, config_.features, code);
  defineInternalTypes(code);
  FuncGen::DefineJitLog(code, config_.features);

  if (config_.features[Feature::TreeBuilderV2]) {
    if (config_.features[Feature::Library]) {
      FuncGen::DefineBackInserterDataBuffer(code);
    } else {
      FuncGen::DefineDataSegmentDataBuffer(code);
    }
    code += "using namespace oi;\n";
    code += "using namespace oi::detail;\n";
    code += "using oi::exporters::ParsedData;\n";
    code += "using namespace oi::exporters;\n";
  }

  if (config_.features[Feature::CaptureThriftIsset]) {
    genDefsThrift(typeGraph, code);
  }
  if (!config_.features[Feature::TreeBuilderV2]) {
    code += "namespace {\n";
    code += "static struct Context {\n";
    code += "  PointerHashSet<> pointers;\n";
    code += "} ctx;\n";
    code += "} // namespace\n";
  }

  /*
   * The purpose of the anonymous namespace within `OIInternal` is that
   * anything defined within an anonymous namespace has internal-linkage,
   * and therefore won't appear in the symbol table of the resulting object
   * file. Both OIL and OID do a linear search through the symbol table for
   * the top-level `getSize` function to locate the probe entry point, so
   * by keeping the contents of the symbol table to a minimum, we make that
   * process faster.
   */
  code += "namespace OIInternal {\nnamespace {\n";
  if (!config_.features[Feature::TreeBuilderV2]) {
    FuncGen::DefineEncodeData(code);
    FuncGen::DefineEncodeDataSize(code);
    FuncGen::DefineStoreData(code);
  }
  FuncGen::DeclareGetContainer(code);

  genDecls(typeGraph, code);
  genDefs(typeGraph, code);
  genStaticAsserts(typeGraph, code);
  if (config_.features[Feature::TreeBuilderV2]) {
    genNames(typeGraph, code);
    genExclusiveSizes(typeGraph, code);
  }

  if (config_.features[Feature::TreeBuilderV2]) {
    FuncGen::DefineBasicTypeHandlers(code);
    addStandardTypeHandlers(typeGraph, config_.features, code);
    addTypeHandlers(typeGraph, code);
  } else {
    addStandardGetSizeFuncDecls(code);
    addGetSizeFuncDecls(typeGraph, code);

    addStandardGetSizeFuncDefs(code);
    addGetSizeFuncDefs(typeGraph, code);
  }

  assert(typeGraph.rootTypes().size() == 1);
  Type& rootType = typeGraph.rootTypes()[0];
  code += "\nusing __ROOT_TYPE__ = " + rootType.name() + ";\n";
  code += "} // namespace\n} // namespace OIInternal\n";

  const auto& typeToHash = std::visit(
      [](const auto& v) -> const std::string& {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<ExactName, T> ||
                      std::is_same_v<HashedComponent, T>) {
          return v.name;
        } else {
          static_assert(always_false_v<T>, "missing visit");
        }
      },
      rootName);

  if (config_.features[Feature::TreeBuilderV2]) {
    FuncGen::DefineTopLevelIntrospect(code, typeToHash);
  } else {
    FuncGen::DefineTopLevelGetSizeRef(code, typeToHash, config_.features);
  }

  if (config_.features[Feature::TreeBuilderV2]) {
    FuncGen::DefineTreeBuilderInstructions(code,
                                           typeToHash,
                                           calculateExclusiveSize(rootType),
                                           enumerateTypeNames(rootType));
  }

  if (auto* n = std::get_if<ExactName>(&rootName))
    FuncGen::DefineTopLevelIntrospectNamed(code, typeToHash, n->name);

  if (VLOG_IS_ON(3)) {
    VLOG(3) << "Generated trace code:\n";
    // VLOG truncates output, so use std::cerr
    std::cerr << code;
  }
}

}  // namespace oi::detail
