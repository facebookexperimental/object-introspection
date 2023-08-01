#include <gtest/gtest.h>

#include "oi/type_graph/AlignmentCalc.h"
#include "test/type_graph_utils.h"

using namespace type_graph;

TEST(AlignmentCalcTest, PrimitiveMembers) {
  auto myclass = Class{0, Class::Kind::Class, "MyClass", 16};
  auto myint8 = Primitive{Primitive::Kind::Int8};
  auto myint64 = Primitive{Primitive::Kind::Int64};
  myclass.members.push_back(Member{myint8, "n", 0});
  myclass.members.push_back(Member{myint64, "n", 8 * 8});

  test(AlignmentCalc::createPass(), {myclass}, R"(
[0] Class: MyClass (size: 16, align: 8)
      Member: n (offset: 0, align: 1)
        Primitive: int8_t
      Member: n (offset: 8, align: 8)
        Primitive: int64_t
)");
}

TEST(AlignmentCalcTest, StructMembers) {
  auto mystruct = Class{1, Class::Kind::Struct, "MyStruct", 8};
  auto myint32 = Primitive{Primitive::Kind::Int32};
  mystruct.members.push_back(Member{myint32, "n1", 0});
  mystruct.members.push_back(Member{myint32, "n2", 4 * 8});

  auto myclass = Class{0, Class::Kind::Class, "MyClass", 12};
  auto myint8 = Primitive{Primitive::Kind::Int8};
  myclass.members.push_back(Member{myint8, "n", 0});
  myclass.members.push_back(Member{mystruct, "s", 4 * 8});

  test(AlignmentCalc::createPass(), {myclass}, R"(
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
  auto myclass = Class{1, Class::Kind::Class, "MyClass", 16};
  auto myint8 = Primitive{Primitive::Kind::Int8};
  auto myint64 = Primitive{Primitive::Kind::Int64};
  myclass.members.push_back(Member{myint8, "n", 0});
  myclass.members.push_back(Member{myint64, "n", 8 * 8});

  auto mycontainer = Container{0, ContainerInfo{}, 8};
  mycontainer.templateParams.push_back(myclass);

  test(AlignmentCalc::createPass(), {mycontainer}, R"(
[0] Container:  (size: 8)
      Param
[1]     Class: MyClass (size: 16, align: 8)
          Member: n (offset: 0, align: 1)
            Primitive: int8_t
          Member: n (offset: 8, align: 8)
            Primitive: int64_t
)");
}

TEST(AlignmentCalcTest, PackedMembers) {
  test(AlignmentCalc::createPass(), R"(
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
  test(AlignmentCalc::createPass(), R"(
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
  test(AlignmentCalc::createPass(), R"(
[0] Class: MyClass (size: 0)
      Param
[1]     Class: ClassA (size: 16)
          Member: a (offset: 0)
            Primitive: int8_t
          Member: b (offset: 8)
            Primitive: int64_t
)",
       R"(
[0] Class: MyClass (size: 0, align: 1)
      Param
[1]     Class: ClassA (size: 16, align: 8)
          Member: a (offset: 0, align: 1)
            Primitive: int8_t
          Member: b (offset: 8, align: 8)
            Primitive: int64_t
)");
}

TEST(AlignmentCalcTest, RecurseClassParent) {
  test(AlignmentCalc::createPass(), R"(
[0] Class: MyClass (size: 0)
      Parent (offset: 0)
[1]     Class: ClassA (size: 16)
          Member: a (offset: 0)
            Primitive: int8_t
          Member: b (offset: 8)
            Primitive: int64_t
)",
       R"(
[0] Class: MyClass (size: 0, align: 1)
      Parent (offset: 0)
[1]     Class: ClassA (size: 16, align: 8)
          Member: a (offset: 0, align: 1)
            Primitive: int8_t
          Member: b (offset: 8, align: 8)
            Primitive: int64_t
)");
}

TEST(AlignmentCalcTest, RecurseClassMember) {
  test(AlignmentCalc::createPass(), R"(
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
  test(AlignmentCalc::createPass(), R"(
[0] Class: MyClass (size: 0)
      Child
[1]     Class: ClassA (size: 16)
          Member: a (offset: 0)
            Primitive: int8_t
          Member: b (offset: 8)
            Primitive: int64_t
)",
       R"(
[0] Class: MyClass (size: 0, align: 1)
      Child
[1]     Class: ClassA (size: 16, align: 8)
          Member: a (offset: 0, align: 1)
            Primitive: int8_t
          Member: b (offset: 8, align: 8)
            Primitive: int64_t
)");
}

TEST(AlignmentCalcTest, Bitfields) {
  test(AlignmentCalc::createPass(), R"(
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
  test(AlignmentCalc::createPass(), R"(
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
  test(AlignmentCalc::createPass(), R"(
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
