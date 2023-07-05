#include <gtest/gtest.h>

#include <functional>
#include <sstream>
#include <string_view>
#include <vector>

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
}  // namespace

TEST(CodeGenTest, TransformContainerAllocator) {
  auto myint = Primitive{Primitive::Kind::Int32};

  auto myalloc = Class{1, Class::Kind::Struct, "MyAlloc", 8};
  myalloc.templateParams.push_back(TemplateParam{&myint});
  myalloc.functions.push_back(Function{"allocate"});
  myalloc.functions.push_back(Function{"deallocate"});

  auto container = getVector();
  container.templateParams.push_back(TemplateParam{&myint});
  container.templateParams.push_back(TemplateParam{&myalloc});

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
  pair.templateParams.push_back(TemplateParam{&myint, {Qualifier::Const}});
  pair.templateParams.push_back(TemplateParam{&myint});

  Class myallocBase{2, Class::Kind::Struct,
                    "MyAllocBase<std::pair<const int, int>>", 1};
  myallocBase.templateParams.push_back(TemplateParam{&pair});
  myallocBase.functions.push_back(Function{"allocate"});
  myallocBase.functions.push_back(Function{"deallocate"});

  Class myalloc{1, Class::Kind::Struct, "MyAlloc<std::pair<const int, int>>",
                1};
  myalloc.parents.push_back(Parent{&myallocBase, 0});
  myalloc.functions.push_back(Function{"allocate"});
  myalloc.functions.push_back(Function{"deallocate"});

  Container map{0, mapInfo, 24};
  map.templateParams.push_back(TemplateParam{&myint});
  map.templateParams.push_back(TemplateParam{&myint});
  map.templateParams.push_back(TemplateParam{&myalloc});

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
[0] Container: std::map<int32_t, int32_t, DummyAllocator<std::pair<const int32_t, int32_t>, 0, 0>> (size: 24)
      Param
        Primitive: int32_t
      Param
        Primitive: int32_t
      Param
        DummyAllocator (size: 0)
[1]       Container: std::pair<const int32_t, int32_t> (size: 8)
            Param
              Primitive: int32_t
              Qualifiers: const
            Param
              Primitive: int32_t
)");
}
