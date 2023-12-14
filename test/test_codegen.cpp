#include <gtest/gtest.h>

#include <functional>
#include <sstream>
#include <string_view>
#include <vector>

#include "TypeGraphParser.h"
#include "mocks.h"
#include "oi/CodeGen.h"
#include "oi/type_graph/TypeGraph.h"
#include "oi/type_graph/Types.h"
#include "type_graph_utils.h"

using namespace type_graph;

template <typename T>
using ref = std::reference_wrapper<T>;

namespace {
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
  check(typeGraph, input, "parsing input graph");

  MockSymbolService symbols;
  CodeGen codegen{config, symbols};
  for (auto& info : getContainerInfos()) {
    codegen.registerContainer(std::move(info));
  }
  codegen.transform(typeGraph);

  check(typeGraph, expectedAfter, "after transform");
}

void testTransform(std::string_view input, std::string_view expectedAfter) {
  OICodeGen::Config config;
  config.features[Feature::PruneTypeGraph] = true;
  config.features[Feature::TreeBuilderV2] = true;
  testTransform(config, input, expectedAfter);
}
}  // namespace

TEST(CodeGenTest, TransformContainerAllocator) {
  testTransform(R"(
[0] Class: std::vector (size: 24)
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
[2] Container: std::vector<int32_t, DummyAllocator<int32_t, 8, 1, 3>> (size: 24, align: 1)
      Param
        Primitive: int32_t
      Param
[3]     DummyAllocator [MyAlloc] (size: 8, align: 1)
          Primitive: int32_t
)");
}

TEST(CodeGenTest, TransformContainerAllocatorParamInParent) {
  testTransform(R"(
[0] Class: std::map (size: 24)
      Param
        Primitive: int32_t
      Param
        Primitive: int32_t
      Param
[1]     Struct: MyAlloc<std::pair<const int, int>> (size: 1)
          Parent (offset: 0)
[2]         Struct: MyAllocBase<std::pair<const int, int>> (size: 1)
              Param
[3]             Class: std::pair (size: 8)
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
[4] Container: std::map<int32_t, int32_t, DummyAllocator<std::pair<int32_t const, int32_t>, 0, 1, 6>> (size: 24, align: 1)
      Param
        Primitive: int32_t
      Param
        Primitive: int32_t
      Param
[6]     DummyAllocator [MyAlloc<std::pair<const int, int>>] (size: 0, align: 1)
[5]       Container: std::pair<int32_t const, int32_t> (size: 8, align: 1)
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
  testTransform(config,
                R"(
[0] Class: MyClass (size: 24)
      Member: a (offset: 0)
        Primitive: int8_t
      Member: b (offset: 8)
        Primitive: int64_t
      Member: c (offset: 16)
        Primitive: int8_t
)",
                R"(
[0] Class: MyClass_0 [MyClass] (size: 24, align: 8)
      Member: a_0 [a] (offset: 0, align: 1)
        Primitive: int8_t
      Member: __oi_padding_1 (offset: 1)
[1]     Array: [int8_t[15]] (length: 15)
          Primitive: int8_t
      Member: c_2 [c] (offset: 16, align: 1)
        Primitive: int8_t
      Member: __oi_padding_3 (offset: 17)
[2]     Array: [int8_t[7]] (length: 7)
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
[0] Union: MyUnion_0 [MyUnion] (size: 8, align: 8)
      Member: __oi_padding_0 (offset: 0)
[1]     Array: [int8_t[8]] (length: 8)
          Primitive: int8_t
)");
}

TEST(CodeGenTest, ReplaceContainersAndDummies) {
  testTransform(R"(
[0] Class: std::vector (size: 24)
      Param
        Primitive: uint32_t
      Param
[1]     Class: allocator<int> (size: 1)
          Param
            Primitive: uint32_t
          Function: allocate
          Function: deallocate
)",
                R"(
[2] Container: std::vector<uint32_t, DummyAllocator<uint32_t, 0, 1, 3>> (size: 24, align: 1)
      Param
        Primitive: uint32_t
      Param
[3]     DummyAllocator [allocator<int>] (size: 0, align: 1)
          Primitive: uint32_t
)");
}

TEST(CodeGenTest, ContainerAlignment) {
  testTransform(R"(
[0] Class: MyClass (size: 24)
      Member: container (offset: 0)
[1]     Class: std::vector (size: 24)
          Param
            Primitive: int32_t
          Member: __impl__ (offset: 0)
            Primitive: StubbedPointer
          Member: __impl__ (offset: 8)
            Primitive: StubbedPointer
          Member: __impl__ (offset: 16)
            Primitive: StubbedPointer
)",
                R"(
[0] Class: MyClass_0 [MyClass] (size: 24, align: 8)
      Member: container_0 [container] (offset: 0, align: 8)
[2]     Container: std::vector<int32_t> (size: 24, align: 8)
          Param
            Primitive: int32_t
)");
}

TEST(CodeGenTest, InheritFromContainer) {
  testTransform(R"(
[0] Class: MyClass (size: 24)
      Parent (offset: 0)
[1]     Class: std::vector (size: 24)
          Param
            Primitive: int32_t
          Member: __impl__ (offset: 0)
            Primitive: StubbedPointer
          Member: __impl__ (offset: 8)
            Primitive: StubbedPointer
          Member: __impl__ (offset: 16)
            Primitive: StubbedPointer
)",
                R"(
[0] Class: MyClass_0 [MyClass] (size: 24, align: 8)
      Member: __oi_parent_0 [__oi_parent] (offset: 0, align: 8)
[2]     Container: std::vector<int32_t> (size: 24, align: 8)
          Param
            Primitive: int32_t
)");
}

TEST(CodeGenTest, InheritFromContainerCompat) {
  OICodeGen::Config config;
  testTransform(config,
                R"(
[0] Class: MyClass (size: 24)
      Parent (offset: 0)
[1]     Class: std::vector (size: 24)
          Param
            Primitive: int32_t
          Member: __impl__ (offset: 0)
            Primitive: StubbedPointer
          Member: __impl__ (offset: 8)
            Primitive: StubbedPointer
          Member: __impl__ (offset: 16)
            Primitive: StubbedPointer
)",
                R"(
[0] Class: MyClass_0 [MyClass] (size: 24, align: 8)
      Member: __oi_padding_0 (offset: 0)
[3]     Array: [int8_t[24]] (length: 24)
          Primitive: int8_t
)");
}
