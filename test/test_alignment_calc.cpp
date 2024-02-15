#include <gtest/gtest.h>

#include "oi/type_graph/AlignmentCalc.h"
#include "test/type_graph_utils.h"

using namespace type_graph;

TEST(AlignmentCalcTest, PrimitiveMembers) {
  test(AlignmentCalc::createPass(),
       R"(
[0] Class: MyClass (size: 16)
      Member: n (offset: 0)
        Primitive: int8_t
      Member: n (offset: 8)
        Primitive: int64_t
)",
       R"(
[0] Class: MyClass (size: 16, align: 8)
      Member: n (offset: 0, align: 1)
        Primitive: int8_t
      Member: n (offset: 8, align: 8)
        Primitive: int64_t
)");
}

TEST(AlignmentCalcTest, StructMembers) {
  test(AlignmentCalc::createPass(),
       R"(
[0] Class: MyClass (size: 12)
      Member: n (offset: 0)
        Primitive: int8_t
      Member: s (offset: 4)
[1]     Struct: MyStruct (size: 8)
          Member: n1 (offset: 0)
            Primitive: int32_t
          Member: n2 (offset: 4)
            Primitive: int32_t
)",
       R"(
[0] Class: MyClass (size: 12, align: 4)
      Member: n (offset: 0, align: 1)
        Primitive: int8_t
      Member: s (offset: 4, align: 4)
[1]     Struct: MyStruct (size: 8, align: 4)
          Member: n1 (offset: 0, align: 4)
            Primitive: int32_t
          Member: n2 (offset: 4, align: 4)
            Primitive: int32_t
)");
}

TEST(AlignmentCalcTest, StructInContainer) {
  test(AlignmentCalc::createPass(),
       R"(
[0] Container: std::vector (size: 8)
      Param
[1]     Class: MyClass (size: 16)
          Member: n (offset: 0)
            Primitive: int8_t
          Member: n (offset: 8)
            Primitive: int32_t
)",
       R"(
[0] Container: std::vector (size: 8)
      Param
[1]     Class: MyClass (size: 16, align: 4)
          Member: n (offset: 0, align: 1)
            Primitive: int8_t
          Member: n (offset: 8, align: 4)
            Primitive: int32_t
)");
}

TEST(AlignmentCalcTest, PackedMembers) {
  test(AlignmentCalc::createPass(),
       R"(
[0] Struct: MyStruct (size: 8)
      Member: n1 (offset: 0)
        Primitive: int8_t
      Member: n2 (offset: 1)
        Primitive: int32_t
      Member: n3 (offset: 5)
        Primitive: int8_t
      Member: n4 (offset: 6)
        Primitive: int8_t
      Member: n5 (offset: 7)
        Primitive: int8_t
)",
       R"(
[0] Struct: MyStruct (size: 8, align: 4, packed)
      Member: n1 (offset: 0, align: 1)
        Primitive: int8_t
      Member: n2 (offset: 1, align: 4)
        Primitive: int32_t
      Member: n3 (offset: 5, align: 1)
        Primitive: int8_t
      Member: n4 (offset: 6, align: 1)
        Primitive: int8_t
      Member: n5 (offset: 7, align: 1)
        Primitive: int8_t
)");
}

TEST(AlignmentCalcTest, PackedTailPadding) {
  test(AlignmentCalc::createPass(),
       R"(
[0] Struct: MyStruct (size: 5)
      Member: n1 (offset: 0)
        Primitive: int32_t
      Member: n2 (offset: 4)
        Primitive: int8_t
)",
       R"(
[0] Struct: MyStruct (size: 5, align: 4, packed)
      Member: n1 (offset: 0, align: 4)
        Primitive: int32_t
      Member: n2 (offset: 4, align: 1)
        Primitive: int8_t
)");
}

TEST(AlignmentCalcTest, RecurseClassParam) {
  test(AlignmentCalc::createPass(),
       R"(
[0] Class: MyClass (size: 0)
      Param
[1]     Class: ClassA (size: 16)
          Member: a (offset: 0)
            Primitive: int8_t
          Member: b (offset: 8)
            Primitive: int64_t
)",
       R"(
[0] Class: MyClass (size: 0, align: 1, packed)
      Param
[1]     Class: ClassA (size: 16, align: 8)
          Member: a (offset: 0, align: 1)
            Primitive: int8_t
          Member: b (offset: 8, align: 8)
            Primitive: int64_t
)");
}

TEST(AlignmentCalcTest, RecurseClassMember) {
  test(AlignmentCalc::createPass(),
       R"(
[0] Class: MyClass (size: 0)
      Member: xxx (offset: 0)
[1]     Class: ClassA (size: 16)
          Member: a (offset: 0)
            Primitive: int8_t
          Member: b (offset: 8)
            Primitive: int64_t
)",
       R"(
[0] Class: MyClass (size: 0, align: 8)
      Member: xxx (offset: 0, align: 8)
[1]     Class: ClassA (size: 16, align: 8)
          Member: a (offset: 0, align: 1)
            Primitive: int8_t
          Member: b (offset: 8, align: 8)
            Primitive: int64_t
)");
}

TEST(AlignmentCalcTest, RecurseClassChild) {
  test(AlignmentCalc::createPass(),
       R"(
[0] Class: MyClass (size: 0)
      Child
[1]     Class: ClassA (size: 16)
          Member: a (offset: 0)
            Primitive: int8_t
          Member: b (offset: 8)
            Primitive: int64_t
)",
       R"(
[0] Class: MyClass (size: 0, align: 1, packed)
      Child
[1]     Class: ClassA (size: 16, align: 8)
          Member: a (offset: 0, align: 1)
            Primitive: int8_t
          Member: b (offset: 8, align: 8)
            Primitive: int64_t
)");
}

TEST(AlignmentCalcTest, Bitfields) {
  test(AlignmentCalc::createPass(),
       R"(
[0] Class: MyClass (size: 8)
      Member: a (offset: 0, bitsize: 2)
        Primitive: int8_t
      Member: b (offset: 0.25, bitsize: 30)
        Primitive: int64_t
)",
       R"(
[0] Class: MyClass (size: 8, align: 8)
      Member: a (offset: 0, align: 1, bitsize: 2)
        Primitive: int8_t
      Member: b (offset: 0.25, align: 8, bitsize: 30)
        Primitive: int64_t
)");
}

TEST(AlignmentCalcTest, Array) {
  test(AlignmentCalc::createPass(),
       R"(
[0] Class: MyClass (size: 1)
      Member: a (offset: 0)
[1]     Array: (length: 1)
[2]       Class: AlignedClass (size: 1)
            Member: b (offset: 0, align: 16)
              Primitive: int8_t
)",
       R"(
[0] Class: MyClass (size: 1, align: 16, packed)
      Member: a (offset: 0, align: 16)
[1]     Array: (length: 1)
[2]       Class: AlignedClass (size: 1, align: 16, packed)
            Member: b (offset: 0, align: 16)
              Primitive: int8_t
)");
}

TEST(AlignmentCalcTest, Typedef) {
  test(AlignmentCalc::createPass(),
       R"(
[0] Class: MyClass (size: 1)
      Member: a (offset: 0)
[1]     Typedef: MyTypedef
[2]       Class: AlignedClass (size: 1)
            Member: b (offset: 0, align: 16)
              Primitive: int8_t
)",
       R"(
[0] Class: MyClass (size: 1, align: 16, packed)
      Member: a (offset: 0, align: 16)
[1]     Typedef: MyTypedef
[2]       Class: AlignedClass (size: 1, align: 16, packed)
            Member: b (offset: 0, align: 16)
              Primitive: int8_t
)");
}

TEST(AlignmentCalcTest, Container) {
  test(AlignmentCalc::createPass(),
       R"(
[0] Container: std::vector (size: 24)
      Underlying
[1]     Class: vector (size: 24)
          Member: n (offset: 0)
            Primitive: int8_t
          Member: s (offset: 4)
[2]         Struct: MyStruct (size: 8)
              Member: n1 (offset: 0)
                Primitive: int32_t
              Member: n2 (offset: 4)
                Primitive: int32_t
)",
       R"(
[0] Container: std::vector (size: 24, align: 4)
      Underlying
[1]     Class: vector (size: 24, align: 4)
          Member: n (offset: 0, align: 1)
            Primitive: int8_t
          Member: s (offset: 4, align: 4)
[2]         Struct: MyStruct (size: 8, align: 4)
              Member: n1 (offset: 0, align: 4)
                Primitive: int32_t
              Member: n2 (offset: 4, align: 4)
                Primitive: int32_t
)");
}
