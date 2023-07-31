#include <gtest/gtest.h>

#include <functional>
#include <sstream>
#include <string_view>
#include <vector>

#include "TypeGraphParser.h"
#include "mocks.h"
#include "oi/CodeGen.h"
#include "oi/type_graph/Printer.h"
#include "oi/type_graph/TypeGraph.h"
#include "oi/type_graph/Types.h"
#include "type_graph_utils.h"

using namespace type_graph;

template <typename T>
using ref = std::reference_wrapper<T>;

namespace {
void testTransform(Type& type,
                   std::string_view expectedBefore,
                   std::string_view expectedAfter) {
  check({type}, expectedBefore, "before transform");

  type_graph::TypeGraph typeGraph;
  typeGraph.addRoot(type);

  OICodeGen::Config config;
  MockSymbolService symbols;
  CodeGen codegen{config, symbols};
  codegen.transform(typeGraph);

  check({type}, expectedAfter, "after transform");
}

void testTransform(OICodeGen::Config& config,
                   std::string_view input,
                   std::string_view expectedAfter) {
  input.remove_prefix(1);  // Remove initial '\n'
  TypeGraph typeGraph;
  TypeGraphParser parser{typeGraph};
  try {
    parser.parse(input);
  } catch (const TypeGraphParserError& err) {
    FAIL() << "Error parsing input graph: " << err.what();
  }

  // Validate input formatting
  check(typeGraph.rootTypes(), input, "parsing input graph");

  MockSymbolService symbols;
  CodeGen codegen{config, symbols};
  codegen.transform(typeGraph);

  check(typeGraph.rootTypes(), expectedAfter, "after transform");
}

void testTransform(std::string_view input, std::string_view expectedAfter) {
  OICodeGen::Config config;
  testTransform(config, input, expectedAfter);
}
}  // namespace

TEST(CodeGenTest, TransformContainerAllocator) {
  auto myint = Primitive{Primitive::Kind::Int32};

  auto myalloc = Class{1, Class::Kind::Struct, "MyAlloc", 8};
  myalloc.templateParams.push_back(TemplateParam{myint});
  myalloc.functions.push_back(Function{"allocate"});
  myalloc.functions.push_back(Function{"deallocate"});

  auto container = getVector();
  container.templateParams.push_back(TemplateParam{myint});
  container.templateParams.push_back(TemplateParam{myalloc});

  testTransform(container, R"(
[0] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Param
[1]     Struct: MyAlloc (size: 8)
          Param
            Primitive: int32_t
          Function: allocate
          Function: deallocate
)",
                R"(
[0] Container: std::vector<int32_t, DummyAllocator<int32_t, 8, 0>> (size: 24)
      Param
        Primitive: int32_t
      Param
        DummyAllocator (size: 8)
          Primitive: int32_t
)");
}

TEST(CodeGenTest, TransformContainerAllocatorParamInParent) {
  ContainerInfo pairInfo{"std::pair", SEQ_TYPE, "utility"};

  ContainerInfo mapInfo{"std::map", STD_MAP_TYPE, "utility"};
  mapInfo.stubTemplateParams = {2, 3};

  Primitive myint{Primitive::Kind::Int32};

  Container pair{3, pairInfo, 8};
  pair.templateParams.push_back(TemplateParam{myint, {Qualifier::Const}});
  pair.templateParams.push_back(TemplateParam{myint});

  Class myallocBase{2, Class::Kind::Struct,
                    "MyAllocBase<std::pair<const int, int>>", 1};
  myallocBase.templateParams.push_back(TemplateParam{pair});
  myallocBase.functions.push_back(Function{"allocate"});
  myallocBase.functions.push_back(Function{"deallocate"});

  Class myalloc{1, Class::Kind::Struct, "MyAlloc<std::pair<const int, int>>",
                1};
  myalloc.parents.push_back(Parent{myallocBase, 0});
  myalloc.functions.push_back(Function{"allocate"});
  myalloc.functions.push_back(Function{"deallocate"});

  Container map{0, mapInfo, 24};
  map.templateParams.push_back(TemplateParam{myint});
  map.templateParams.push_back(TemplateParam{myint});
  map.templateParams.push_back(TemplateParam{myalloc});

  testTransform(map, R"(
[0] Container: std::map (size: 24)
      Param
        Primitive: int32_t
      Param
        Primitive: int32_t
      Param
[1]     Struct: MyAlloc<std::pair<const int, int>> (size: 1)
          Parent (offset: 0)
[2]         Struct: MyAllocBase<std::pair<const int, int>> (size: 1)
              Param
[3]             Container: std::pair (size: 8)
                  Param
                    Primitive: int32_t
                    Qualifiers: const
                  Param
                    Primitive: int32_t
              Function: allocate
              Function: deallocate
          Function: allocate
          Function: deallocate
)",
                R"(
[0] Container: std::map<int32_t, int32_t, DummyAllocator<std::pair<int32_t const, int32_t>, 0, 0>> (size: 24)
      Param
        Primitive: int32_t
      Param
        Primitive: int32_t
      Param
        DummyAllocator (size: 0)
[1]       Container: std::pair<int32_t const, int32_t> (size: 8)
            Param
              Primitive: int32_t
              Qualifiers: const
            Param
              Primitive: int32_t
)");
}

TEST(CodeGenTest, RemovedMemberAlignment) {
  OICodeGen::Config config;
  config.membersToStub = {{"MyClass", "b"}};
  testTransform(config, R"(
[0] Class: MyClass (size: 24)
      Member: a (offset: 0)
        Primitive: int8_t
      Member: b (offset: 8)
        Primitive: int64_t
      Member: c (offset: 16)
        Primitive: int8_t
)",
                R"(
[0] Class: MyClass_0 (size: 24, align: 8)
      Member: a_0 (offset: 0, align: 1)
        Primitive: int8_t
      Member: __oi_padding_1 (offset: 1)
[1]     Array: (length: 15)
          Primitive: int8_t
      Member: c_2 (offset: 16, align: 1)
        Primitive: int8_t
      Member: __oi_padding_3 (offset: 17)
[2]     Array: (length: 7)
          Primitive: int8_t
)");
}

TEST(CodeGenTest, UnionMembersAlignment) {
  testTransform(R"(
[0] Union: MyUnion (size: 8)
      Member: a (offset: 0)
        Primitive: int8_t
      Member: b (offset: 8)
        Primitive: int64_t
)",
                R"(
[0] Union: MyUnion_0 (size: 8, align: 8)
      Member: __oi_padding_0 (offset: 0)
[1]     Array: (length: 8)
          Primitive: int8_t
)");
}
