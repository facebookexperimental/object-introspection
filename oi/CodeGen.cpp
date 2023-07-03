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
#include <set>
#include <string_view>

#include "oi/FuncGen.h"
#include "oi/Headers.h"
#include "oi/SymbolService.h"
#include "type_graph/AddChildren.h"
#include "type_graph/AddPadding.h"
#include "type_graph/AlignmentCalc.h"
#include "type_graph/DrgnParser.h"
#include "type_graph/Flattener.h"
#include "type_graph/NameGen.h"
#include "type_graph/RemoveIgnored.h"
#include "type_graph/RemoveTopLevelPointer.h"
#include "type_graph/TopoSorter.h"
#include "type_graph/TypeGraph.h"
#include "type_graph/TypeIdentifier.h"
#include "type_graph/Types.h"

using type_graph::Class;
using type_graph::Container;
using type_graph::Enum;
using type_graph::Type;
using type_graph::Typedef;
using type_graph::TypeGraph;

template <typename T>
using ref = std::reference_wrapper<T>;

namespace {
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

void defineArray(std::string& code) {
  code += R"(
template<typename T, int N>
struct OIArray {
  T vals[N];
};
)";
}

void defineJitLog(FeatureSet features, std::string& code) {
  if (features[Feature::JitLogging]) {
    code += R"(
#define JLOG(str)                           \
  do {                                      \
    if (__builtin_expect(logFile, 0)) {     \
      write(logFile, str, sizeof(str) - 1); \
    }                                       \
  } while (false)

#define JLOGPTR(ptr)                    \
  do {                                  \
    if (__builtin_expect(logFile, 0)) { \
      __jlogptr((uintptr_t)ptr);        \
    }                                   \
  } while (false)
)";
  } else {
    code += R"(
#define JLOG(str)
#define JLOGPTR(ptr)
)";
  }
}

void addIncludes(const TypeGraph& typeGraph,
                 FeatureSet features,
                 std::string& code) {
  std::set<std::string_view> includes{"cstddef"};
  if (features[Feature::TypedDataSegment]) {
    includes.emplace("functional");
    includes.emplace("oi/types/st.h");
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
  code += "using " + e.name() + " = ";
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
  code += ";\n";
}

void genDeclsTypedef(const Typedef& td, std::string& code) {
  code += "using " + td.name() + " = " + td.underlyingType()->name() + ";\n";
}

void genDecls(const TypeGraph& typeGraph, std::string& code) {
  for (const Type& t : typeGraph.finalTypes) {
    if (const auto* c = dynamic_cast<const Class*>(&t)) {
      genDeclsClass(*c, code);
    } else if (const auto* e = dynamic_cast<const Enum*>(&t)) {
      genDeclsEnum(*e, code);
    } else if (const auto* td = dynamic_cast<const Typedef*>(&t)) {
      genDeclsTypedef(*td, code);
    }
  }
}

void genDefsClass(const Class& c, std::string& code) {
  if (c.kind() == Class::Kind::Union)
    code += "union ";
  else
    code += "struct ";

  if (c.packed()) {
    code += "__attribute__((__packed__)) ";
  }

  code += c.name() + " {\n";
  for (const auto& mem : c.members) {
    code += "  " + mem.type->name() + " " + mem.name + ";\n";
  }
  code += "};\n\n";
}

void genDefsTypedef(const Typedef& td, std::string& code) {
  code += "using " + td.name() + " = " + td.underlyingType()->name() + ";\n";
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
  code += "static_assert(sizeof(" + c.name() +
          ") == " + std::to_string(c.size()) +
          ", \"Unexpected size of struct " + c.name() + "\");\n";
  for (const auto& member : c.members) {
    code += "static_assert(offsetof(" + c.name() + ", " + member.name +
            ") == " + std::to_string(member.offset) +
            ", \"Unexpected offset of " + c.name() + "::" + member.name +
            "\");\n";
  }
  code.push_back('\n');
}

void genStaticAssertsContainer(const Container& c, std::string& code) {
  code += "static_assert(sizeof(" + c.name() +
          ") == " + std::to_string(c.size()) +
          ", \"Unexpected size of container " + c.name() + "\");\n";
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

    void getSizeType(/*const*/ void *s_ptr, size_t& returnArg);

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
      JLOG("ptr val @");
      JLOGPTR(s_ptr);
      StoreData((uintptr_t)(s_ptr), returnArg);
      if (s_ptr && pointers.add((uintptr_t)s_ptr)) {
        StoreData(1, returnArg);
        getSizeType(*(s_ptr), returnArg);
      } else {
        StoreData(0, returnArg);
      }
    }

    void getSizeType(/*const*/ void *s_ptr, size_t& returnArg)
    {
      JLOG("void ptr @");
      JLOGPTR(s_ptr);
      StoreData((uintptr_t)(s_ptr), returnArg);
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

void getClassSizeFuncDef(const Class& c,
                         SymbolService& symbols,
                         bool polymorphicInheritance,
                         std::string& code) {
  bool enablePolymorphicInheritance = polymorphicInheritance && c.isDynamic();

  std::string funcName = "getSizeType";
  if (enablePolymorphicInheritance) {
    funcName = "getSizeTypeConcrete";
  }

  code +=
      "void " + funcName + "(const " + c.name() + " &t, size_t &returnArg) {\n";
  for (const auto& member : c.members) {
    if (member.name.starts_with(type_graph::AddPadding::MemberPrefix))
      continue;
    code += "  JLOG(\"" + member.name + " @\");\n";
    code += "  JLOGPTR(&t." + member.name + ");\n";
    code += "  getSizeType(t." + member.name + ", returnArg);\n";
  }
  code += "}\n";

  if (enablePolymorphicInheritance) {
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

      auto optVtableSym = symbols.locateSymbol(childVtableName, true);
      if (!optVtableSym) {
        //        LOG(ERROR) << "Failed to find vtable address for '" <<
        //        childVtableName; LOG(ERROR) << "Falling back to non dynamic
        //        mode";
        childVtableAddrs.clear();  // TODO why??
        break;
      }
      childVtableAddrs.push_back(*optVtableSym);
    }

    code +=
        "void getSizeType(const " + c.name() + " &t, size_t &returnArg) {\n";
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
      code += "    getSizeTypeConcrete(*reinterpret_cast<const " +
              child.name() + "*>(baseAddress), returnArg);\n";
      code += "    return;\n";
      code += "  }\n";
    }

    code += "  SAVE_DATA(-1);\n";
    code += "  getSizeTypeConcrete(t, returnArg);\n";
    code += "}\n";
  }
}

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

void addGetSizeFuncDefs(
    const TypeGraph& typeGraph,
    SymbolService& symbols,
    std::unordered_set<const ContainerInfo*>& definedContainers,
    bool polymorphicInheritance,
    std::string& code) {
  for (const Type& t : typeGraph.finalTypes) {
    if (const auto* c = dynamic_cast<const Class*>(&t)) {
      getClassSizeFuncDef(*c, symbols, polymorphicInheritance, code);
    } else if (const auto* con = dynamic_cast<const Container*>(&t)) {
      getContainerSizeFuncDef(definedContainers, *con, code);
    }
  }
}

void addStandardTypeHandlers(std::string& code) {
  code += R"(
    template <typename DB, typename T>
    types::st::Unit<DB>
    getSizeType(const T &t, typename TypeHandler<DB, T>::type returnArg) {
      JLOG("obj @");
      JLOGPTR(&t);
      return TypeHandler<DB, T>::getSizeType(t, returnArg);
    }
  )";

  code += R"(
    template<typename DB, typename T0, long unsigned int N>
    struct TypeHandler<DB, OIArray<T0, N>> {
      using type = types::st::List<DB, typename TypeHandler<DB, T0>::type>;
      static types::st::Unit<DB> getSizeType(
          const OIArray<T0, N> &container,
          typename TypeHandler<DB, OIArray<T0,N>>::type returnArg) {
        auto tail = returnArg.write(N);
        for (size_t i=0; i<N; i++) {
          tail = tail.delegate([&container, i](auto ret) {
              return TypeHandler<DB, T0>::getSizeType(container.vals[i], ret);
          });
        }
        return tail.finish();
      }
    };
  )";
}

void getClassTypeHandler(const Class& c, std::string& code) {
  std::string funcName = "getSizeType";

  std::string typeStaticType;
  {
    size_t pairs = 0;

    for (size_t i = 0; i < c.members.size(); i++) {
      const auto& member = c.members[i];

      if (i != c.members.size() - 1) {
        typeStaticType += "types::st::Pair<DB, ";
        pairs++;
      }

      typeStaticType +=
          (boost::format("typename TypeHandler<DB, decltype(%1%::%2%)>::type") %
           c.name() % member.name)
              .str();

      if (i != c.members.size() - 1) {
        typeStaticType += ", ";
      }
    }
    typeStaticType += std::string(pairs, '>');

    if (typeStaticType.empty()) {
      typeStaticType = "types::st::Unit<DB>";
    }
  }

  std::string traverser;
  {
    if (!c.members.empty()) {
      traverser = "auto ret = returnArg";
    }
    for (size_t i = 0; i < c.members.size(); i++) {
      const auto& member = c.members[i];

      if (i != c.members.size() - 1) {
        traverser += "\n  .delegate([&t](auto ret) {";
        traverser += "\n    return OIInternal::getSizeType<DB>(t." +
                     member.name + ", ret);";
        traverser += "\n})";
      } else {
        traverser += ";";
        traverser +=
            "\nreturn OIInternal::getSizeType<DB>(t." + member.name + ", ret);";
      }
    }

    if (traverser.empty()) {
      traverser = "return returnArg;";
    }
  }

  code += (boost::format(R"(
template <typename DB>
class TypeHandler<DB, %1%> {
 public:
  using type = %2%;
  static types::st::Unit<DB> %3%(
      const %1%& t,
      typename TypeHandler<DB, %1%>::type returnArg) {
    %4%
  }
};
)") % c.name() %
           typeStaticType % funcName % traverser)
              .str();
}

void getContainerTypeHandler(std::unordered_set<const ContainerInfo*>& used,
                             const Container& c,
                             std::string& code) {
  if (!used.insert(&c.containerInfo_).second) {
    return;
  }

  const auto& handler = c.containerInfo_.codegen.handler;
  if (handler.empty()) {
    LOG(ERROR) << "`codegen.handler` must be specified for all containers "
                  "under \"-ftyped-data-segment\", not specified for \"" +
                      c.containerInfo_.typeName + "\"";
    throw std::runtime_error("missing `codegen.handler`");
  }
  auto fmt = boost::format(c.containerInfo_.codegen.handler) %
             c.containerInfo_.typeName;
  code += fmt.str();
}

void addTypeHandlers(
    std::unordered_set<const ContainerInfo*>& definedContainers,
    const TypeGraph& typeGraph,
    std::string& code) {
  for (const Type& t : typeGraph.finalTypes) {
    if (const auto* c = dynamic_cast<const Class*>(&t)) {
      getClassTypeHandler(*c, code);
    } else if (const auto* con = dynamic_cast<const Container*>(&t)) {
      getContainerTypeHandler(definedContainers, *con, code);
    }
  }
}
}  // namespace

bool CodeGen::codegenFromDrgn(struct drgn_type* drgnType, std::string& code) {
  try {
    containerInfos_.reserve(config_.containerConfigPaths.size());
    for (const auto& path : config_.containerConfigPaths) {
      registerContainer(path);
    }
  } catch (const ContainerInfoError& err) {
    LOG(ERROR) << "Error reading container TOML file " << err.what();
    return false;
  }

  type_graph::TypeGraph typeGraph;
  try {
    addDrgnRoot(drgnType, typeGraph);
  } catch (const type_graph::DrgnParserError& err) {
    LOG(ERROR) << "Error parsing DWARF: " << err.what();
    return false;
  }

  transform(typeGraph);
  generate(typeGraph, code, drgnType);
  return true;
}

void CodeGen::registerContainer(const fs::path& path) {
  const auto& info = containerInfos_.emplace_back(path);
  VLOG(1) << "Registered container: " << info.typeName;
}

void CodeGen::addDrgnRoot(struct drgn_type* drgnType,
                          type_graph::TypeGraph& typeGraph) {
  type_graph::DrgnParser drgnParser{
      typeGraph, containerInfos_, config_.features[Feature::ChaseRawPointers]};
  Type* parsedRoot = drgnParser.parse(drgnType);
  typeGraph.addRoot(*parsedRoot);
}

void CodeGen::transform(type_graph::TypeGraph& typeGraph) {
  type_graph::PassManager pm;
  pm.addPass(type_graph::Flattener::createPass());
  pm.addPass(type_graph::TypeIdentifier::createPass());
  if (config_.features[Feature::PolymorphicInheritance]) {
    type_graph::DrgnParser drgnParser{
        typeGraph, containerInfos_,
        config_.features[Feature::ChaseRawPointers]};
    pm.addPass(type_graph::AddChildren::createPass(drgnParser, symbols_));
    // Re-run passes over newly added children
    pm.addPass(type_graph::Flattener::createPass());
    pm.addPass(type_graph::TypeIdentifier::createPass());
  }
  pm.addPass(type_graph::RemoveIgnored::createPass(config_.membersToStub));
  pm.addPass(type_graph::AddPadding::createPass());
  pm.addPass(type_graph::NameGen::createPass());
  pm.addPass(type_graph::AlignmentCalc::createPass());
  pm.addPass(type_graph::RemoveTopLevelPointer::createPass());
  pm.addPass(type_graph::TopoSorter::createPass());
  pm.run(typeGraph);

  LOG(INFO) << "Sorted types:\n";
  for (Type& t : typeGraph.finalTypes) {
    LOG(INFO) << "  " << t.name() << std::endl;
  };
}

void CodeGen::generate(
    type_graph::TypeGraph& typeGraph,
    std::string& code,
    struct drgn_type* drgnType /* TODO: this argument should not be required */
) {
  code = headers::oi_OITraceCode_cpp;
  if (!config_.features[Feature::TypedDataSegment]) {
    defineMacros(code);
  }
  addIncludes(typeGraph, config_.features, code);
  defineArray(code);
  defineJitLog(config_.features, code);

  if (config_.features[Feature::TypedDataSegment]) {
    FuncGen::DefineDataSegmentDataBuffer(code);
    code += "using namespace ObjectIntrospection;\n";

    code += "namespace OIInternal {\nnamespace {\n";
    FuncGen::DefineBasicTypeHandlers(code);
    code += "} // namespace\n} // namespace OIInternal\n";
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
  if (!config_.features[Feature::TypedDataSegment]) {
    FuncGen::DefineEncodeData(code);
    FuncGen::DefineEncodeDataSize(code);
    FuncGen::DefineStoreData(code);
    FuncGen::DefineAddData(code);
  }
  FuncGen::DeclareGetContainer(code);

  genDecls(typeGraph, code);
  genDefs(typeGraph, code);
  genStaticAsserts(typeGraph, code);

  if (config_.features[Feature::TypedDataSegment]) {
    addStandardTypeHandlers(code);
    addTypeHandlers(definedContainers_, typeGraph, code);
  } else {
    addStandardGetSizeFuncDecls(code);
    addGetSizeFuncDecls(typeGraph, code);

    addStandardGetSizeFuncDefs(code);
    addGetSizeFuncDefs(typeGraph, symbols_, definedContainers_,
                       config_.features[Feature::PolymorphicInheritance], code);
  }

  assert(typeGraph.rootTypes().size() == 1);
  Type& rootType = typeGraph.rootTypes()[0];
  code += "\nusing __ROOT_TYPE__ = " + rootType.name() + ";\n";
  code += "} // namespace\n} // namespace OIInternal\n";

  if (config_.features[Feature::TypedDataSegment]) {
    FuncGen::DefineTopLevelGetSizeRefTyped(
        code, SymbolService::getTypeName(drgnType), config_.features);
  } else {
    FuncGen::DefineTopLevelGetSizeRef(
        code, SymbolService::getTypeName(drgnType), config_.features);
  }

  if (VLOG_IS_ON(3)) {
    VLOG(3) << "Generated trace code:\n";
    // VLOG truncates output, so use std::cout
    std::cout << code;
  }
}
