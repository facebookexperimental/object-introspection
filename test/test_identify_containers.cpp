#include <gtest/gtest.h>

#include "oi/ContainerInfo.h"
#include "oi/type_graph/IdentifyContainers.h"
#include "oi/type_graph/Types.h"
#include "test/type_graph_utils.h"

using namespace type_graph;

namespace {
void test(std::string_view input, std::string_view expectedAfter) {
  ::test(IdentifyContainers::createPass(getContainerInfos()),
         input,
         expectedAfter);
}
};  // namespace

TEST(IdentifyContainers, Container) {
  test(R"(
[0] Class: std::vector (size: 24)
      Param
        Primitive: int32_t
      Member: a (offset: 0)
        Primitive: int32_t
)",
       R"(
[1] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Underlying
[0]     Class: std::vector (size: 24)
          Param
            Primitive: int32_t
          Member: a (offset: 0)
            Primitive: int32_t
)");
}

TEST(IdentifyContainers, ContainerInClass) {
  test(R"(
[0] Class: MyClass (size: 0)
      Param
[1]     Class: std::vector (size: 24)
          Param
            Primitive: int32_t
      Parent (offset: 0)
[2]     Class: std::vector (size: 24)
          Param
            Primitive: int32_t
      Member: a (offset: 0)
[3]     Class: std::vector (size: 24)
          Param
            Primitive: int32_t
)",
       R"(
[0] Class: MyClass (size: 0)
      Param
[4]     Container: std::vector (size: 24)
          Param
            Primitive: int32_t
          Underlying
[1]         Class: std::vector (size: 24)
              Param
                Primitive: int32_t
      Parent (offset: 0)
[5]     Container: std::vector (size: 24)
          Param
            Primitive: int32_t
          Underlying
[2]         Class: std::vector (size: 24)
              Param
                Primitive: int32_t
      Member: a (offset: 0)
[6]     Container: std::vector (size: 24)
          Param
            Primitive: int32_t
          Underlying
[3]         Class: std::vector (size: 24)
              Param
                Primitive: int32_t
)");
}

TEST(IdentifyContainers, ContainerInContainer) {
  test(R"(
[0] Class: std::vector (size: 24)
      Param
[1]     Class: std::vector (size: 24)
          Param
            Primitive: int32_t
)",
       R"(
[2] Container: std::vector (size: 24)
      Param
[3]     Container: std::vector (size: 24)
          Param
            Primitive: int32_t
          Underlying
[1]         Class: std::vector (size: 24)
              Param
                Primitive: int32_t
      Underlying
[0]     Class: std::vector (size: 24)
          Param
            [1]
)");
}

TEST(IdentifyContainers, ContainerInContainer2) {
  test(R"(
[0] Container: std::vector (size: 24)
      Param
[1]     Class: std::vector (size: 24)
          Param
            Primitive: int32_t
)",
       R"(
[0] Container: std::vector (size: 24)
      Param
[2]     Container: std::vector (size: 24)
          Param
            Primitive: int32_t
          Underlying
[1]         Class: std::vector (size: 24)
              Param
                Primitive: int32_t
)");
}

TEST(IdentifyContainers, ContainerInArray) {
  test(R"(
[0] Array: (length: 2)
[1]   Class: std::vector (size: 24)
        Param
          Primitive: int32_t
)",
       R"(
[0] Array: (length: 2)
[2]   Container: std::vector (size: 24)
        Param
          Primitive: int32_t
        Underlying
[1]       Class: std::vector (size: 24)
            Param
              Primitive: int32_t
)");
}

TEST(IdentifyContainers, ContainerInTypedef) {
  test(R"(
[0] Typedef: MyAlias
[1]   Class: std::vector (size: 24)
        Param
          Primitive: int32_t
)",
       R"(
[0] Typedef: MyAlias
[2]   Container: std::vector (size: 24)
        Param
          Primitive: int32_t
        Underlying
[1]       Class: std::vector (size: 24)
            Param
              Primitive: int32_t
)");
}

TEST(IdentifyContainers, ContainerInPointer) {
  test(R"(
[0] Pointer
[1]   Class: std::vector (size: 24)
        Param
          Primitive: int32_t
)",
       R"(
[0] Pointer
[2]   Container: std::vector (size: 24)
        Param
          Primitive: int32_t
        Underlying
[1]       Class: std::vector (size: 24)
            Param
              Primitive: int32_t
)");
}

TEST(IdentifyContainers, ContainerDuplicate) {
  test(R"(
[0] Class: std::vector (size: 24)
      Param
        Primitive: int32_t
      Member: a (offset: 0)
        Primitive: int32_t
    [0]
)",
       R"(
[1] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Underlying
[0]     Class: std::vector (size: 24)
          Param
            Primitive: int32_t
          Member: a (offset: 0)
            Primitive: int32_t
    [1]
)");
}

TEST(IdentifyContainers, CycleClass) {
  test(R"(
[0] Class: ClassA (size: 0)
      Member: x (offset: 0)
[1]     Class: ClassB (size: 0)
          Param
            [0]
)",
       R"(
[0] Class: ClassA (size: 0)
      Member: x (offset: 0)
[1]     Class: ClassB (size: 0)
          Param
            [0]
)");
}

TEST(IdentifyContainers, CycleContainer) {
  test(R"(
[0] Class: ClassA (size: 0)
      Member: x (offset: 0)
[1]     Class: std::vector (size: 0)
          Param
            [0]
)",
       R"(
[0] Class: ClassA (size: 0)
      Member: x (offset: 0)
[2]     Container: std::vector (size: 0)
          Param
            [0]
          Underlying
[1]         Class: std::vector (size: 0)
              Param
                [0]
)");
}
