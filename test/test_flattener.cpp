#include <gtest/gtest.h>

#include "oi/type_graph/Flattener.h"
#include "oi/type_graph/Types.h"
#include "test/type_graph_utils.h"

using namespace type_graph;

TEST(FlattenerTest, NoParents) {
  // Original and flattened:
  //   struct MyStruct { int n0; };
  //   class MyClass {
  //     int n;
  //     MyEnum e;
  //     MyStruct mystruct;
  //   };
  auto myint = Primitive{Primitive::Kind::Int32};
  auto myenum = Enum{"MyEnum", 4};
  auto mystruct = Class{1, Class::Kind::Struct, "MyStruct", 4};
  auto myclass = Class{0, Class::Kind::Class, "MyClass", 12};

  mystruct.members.push_back(Member{myint, "n0", 0});

  myclass.members.push_back(Member{myint, "n", 0});
  myclass.members.push_back(Member{myenum, "e", 4 * 8});
  myclass.members.push_back(Member{mystruct, "mystruct", 8 * 8});

  test(Flattener::createPass(), {myclass}, R"(
[0] Class: MyClass (size: 12)
      Member: n (offset: 0)
        Primitive: int32_t
      Member: e (offset: 4)
        Enum: MyEnum (size: 4)
      Member: mystruct (offset: 8)
[1]     Struct: MyStruct (size: 4)
          Member: n0 (offset: 0)
            Primitive: int32_t
)");
}

TEST(FlattenerTest, OnlyParents) {
  // Original:
  //   class C { int c; };
  //   class B { int b; };
  //   class A : B, C { };
  //
  // Flattened:
  //   class A {
  //     int b;
  //     int c;
  //   };
  auto myint = Primitive{Primitive::Kind::Int32};
  auto classA = Class{0, Class::Kind::Class, "ClassA", 8};
  auto classB = Class{1, Class::Kind::Class, "ClassB", 4};
  auto classC = Class{2, Class::Kind::Class, "ClassC", 4};

  classC.members.push_back(Member{myint, "c", 0});
  classB.members.push_back(Member{myint, "b", 0});

  classA.parents.push_back(Parent{classB, 0});
  classA.parents.push_back(Parent{classC, 4 * 8});

  test(Flattener::createPass(), {classA}, R"(
[0] Class: ClassA (size: 8)
      Member: b (offset: 0)
        Primitive: int32_t
      Member: c (offset: 4)
        Primitive: int32_t
)");
}

TEST(FlattenerTest, ParentsFirst) {
  // Original:
  //   class C { int c; };
  //   class B { int b; };
  //   class A : B, C { int a; };
  //
  // Flattened:
  //   class A {
  //     int b;
  //     int c;
  //     int a;
  //   };
  auto myint = Primitive{Primitive::Kind::Int32};
  auto classA = Class{0, Class::Kind::Class, "ClassA", 12};
  auto classB = Class{1, Class::Kind::Class, "ClassB", 4};
  auto classC = Class{2, Class::Kind::Class, "ClassC", 4};

  classC.members.push_back(Member{myint, "c", 0});
  classB.members.push_back(Member{myint, "b", 0});

  classA.parents.push_back(Parent{classB, 0});
  classA.parents.push_back(Parent{classC, 4 * 8});
  classA.members.push_back(Member{myint, "a", 8 * 8});

  test(Flattener::createPass(), {classA}, R"(
[0] Class: ClassA (size: 12)
      Member: b (offset: 0)
        Primitive: int32_t
      Member: c (offset: 4)
        Primitive: int32_t
      Member: a (offset: 8)
        Primitive: int32_t
)");
}

TEST(FlattenerTest, MembersFirst) {
  // Original:
  //   class C { int c; };
  //   class B { int b; };
  //   class A : B, C { int a; };
  //
  // Flattened:
  //   class A {
  //     int a;
  //     int b;
  //     int c;
  //   };
  auto myint = Primitive{Primitive::Kind::Int32};
  auto classA = Class{0, Class::Kind::Class, "ClassA", 12};
  auto classB = Class{1, Class::Kind::Class, "ClassB", 4};
  auto classC = Class{2, Class::Kind::Class, "ClassC", 4};

  classC.members.push_back(Member{myint, "c", 0});

  classB.members.push_back(Member{myint, "b", 0});

  classA.members.push_back(Member{myint, "a", 0});
  classA.parents.push_back(Parent{classB, 4 * 8});
  classA.parents.push_back(Parent{classC, 8 * 8});

  test(Flattener::createPass(), {classA}, R"(
[0] Class: ClassA (size: 12)
      Member: a (offset: 0)
        Primitive: int32_t
      Member: b (offset: 4)
        Primitive: int32_t
      Member: c (offset: 8)
        Primitive: int32_t
)");
}

TEST(FlattenerTest, MixedMembersAndParents) {
  // Original:
  //   class C { int c; };
  //   class B { int b; };
  //   class A : B, C { int a1; int a2; };
  //
  // Flattened:
  //   class A {
  //     int b;
  //     int a1;
  //     int a2;
  //     int c;
  //   };
  auto myint = Primitive{Primitive::Kind::Int32};
  auto classA = Class{0, Class::Kind::Class, "ClassA", 16};
  auto classB = Class{1, Class::Kind::Class, "ClassB", 4};
  auto classC = Class{2, Class::Kind::Class, "ClassC", 4};

  classC.members.push_back(Member{myint, "c", 0});

  classB.members.push_back(Member{myint, "b", 0});

  classA.parents.push_back(Parent{classB, 0});
  classA.members.push_back(Member{myint, "a1", 4 * 8});
  classA.members.push_back(Member{myint, "a2", 8 * 8});
  classA.parents.push_back(Parent{classC, 12 * 8});

  test(Flattener::createPass(), {classA}, R"(
[0] Class: ClassA (size: 16)
      Member: b (offset: 0)
        Primitive: int32_t
      Member: a1 (offset: 4)
        Primitive: int32_t
      Member: a2 (offset: 8)
        Primitive: int32_t
      Member: c (offset: 12)
        Primitive: int32_t
)");
}

TEST(FlattenerTest, EmptyParent) {
  // Original:
  //   class C { int c; };
  //   class B { };
  //   class A : B, C { int a1; int a2; };
  //
  // Flattened:
  //   class A {
  //     int c;
  //     int a1;
  //     int a2;
  //   };
  auto myint = Primitive{Primitive::Kind::Int32};
  auto classA = Class{0, Class::Kind::Class, "ClassA", 12};
  auto classB = Class{1, Class::Kind::Class, "ClassB", 0};
  auto classC = Class{2, Class::Kind::Class, "ClassC", 4};

  classC.members.push_back(Member{myint, "c", 0});

  classA.members.push_back(Member{myint, "a1", 4 * 8});
  classA.members.push_back(Member{myint, "a2", 8 * 8});
  classA.parents.push_back(Parent{classB, 0});
  classA.parents.push_back(Parent{classC, 0});

  test(Flattener::createPass(), {classA}, R"(
[0] Class: ClassA (size: 12)
      Member: c (offset: 0)
        Primitive: int32_t
      Member: a1 (offset: 4)
        Primitive: int32_t
      Member: a2 (offset: 8)
        Primitive: int32_t
)");
}

TEST(FlattenerTest, TwoDeep) {
  // Original:
  //   class D { int d; };
  //   class C { int c; };
  //   class B : D { int b; };
  //   class A : B, C { int a; };
  //
  // Flattened:
  //   class A {
  //     int d;
  //     int b;
  //     int c;
  //     int a;
  //   };
  auto myint = Primitive{Primitive::Kind::Int32};
  auto classA = Class{0, Class::Kind::Class, "ClassA", 16};
  auto classB = Class{1, Class::Kind::Class, "ClassB", 8};
  auto classC = Class{2, Class::Kind::Class, "ClassC", 4};
  auto classD = Class{3, Class::Kind::Class, "ClassD", 4};

  classD.members.push_back(Member{myint, "d", 0});

  classC.members.push_back(Member{myint, "c", 0});

  classB.parents.push_back(Parent{classD, 0});
  classB.members.push_back(Member{myint, "b", 4 * 8});

  classA.parents.push_back(Parent{classB, 0});
  classA.parents.push_back(Parent{classC, 8 * 8});
  classA.members.push_back(Member{myint, "a", 12 * 8});

  test(Flattener::createPass(), {classA}, R"(
[0] Class: ClassA (size: 16)
      Member: d (offset: 0)
        Primitive: int32_t
      Member: b (offset: 4)
        Primitive: int32_t
      Member: c (offset: 8)
        Primitive: int32_t
      Member: a (offset: 12)
        Primitive: int32_t
)");
}

TEST(FlattenerTest, DiamondInheritance) {
  // Original:
  //   class C { int c; };
  //   class B : C { int b; };
  //   class A : B, C { int a; };
  //
  // Flattened:
  //   class A {
  //     int c0;
  //     int b;
  //     int c1;
  //     int a;
  //   };
  auto myint = Primitive{Primitive::Kind::Int32};
  auto classA = Class{0, Class::Kind::Class, "ClassA", 16};
  auto classB = Class{1, Class::Kind::Class, "ClassB", 8};
  auto classC = Class{2, Class::Kind::Class, "ClassC", 4};

  classC.members.push_back(Member{myint, "c", 0});

  classB.parents.push_back(Parent{classC, 0});
  classB.members.push_back(Member{myint, "b", 4 * 8});

  classA.parents.push_back(Parent{classB, 0});
  classA.parents.push_back(Parent{classC, 8 * 8});
  classA.members.push_back(Member{myint, "a", 12 * 8});

  test(Flattener::createPass(), {classA}, R"(
[0] Class: ClassA (size: 16)
      Member: c (offset: 0)
        Primitive: int32_t
      Member: b (offset: 4)
        Primitive: int32_t
      Member: c (offset: 8)
        Primitive: int32_t
      Member: a (offset: 12)
        Primitive: int32_t
)");
}

TEST(FlattenerTest, Member) {
  // Original:
  //   class C { int c; };
  //   class B : C { int b; };
  //   class A { int a; B b; };
  //
  // Flattened:
  //   class B { int c; int b; };
  //   Class A { int a; B b; };
  auto myint = Primitive{Primitive::Kind::Int32};
  auto classA = Class{0, Class::Kind::Class, "ClassA", 12};
  auto classB = Class{1, Class::Kind::Class, "ClassB", 8};
  auto classC = Class{2, Class::Kind::Class, "ClassC", 4};

  classC.members.push_back(Member{myint, "c", 0});

  classB.parents.push_back(Parent{classC, 0});
  classB.members.push_back(Member{myint, "b", 4 * 8});

  classA.members.push_back(Member{myint, "a", 0});
  classA.members.push_back(Member{classB, "b", 4 * 8});

  test(Flattener::createPass(), {classA}, R"(
[0] Class: ClassA (size: 12)
      Member: a (offset: 0)
        Primitive: int32_t
      Member: b (offset: 4)
[1]     Class: ClassB (size: 8)
          Member: c (offset: 0)
            Primitive: int32_t
          Member: b (offset: 4)
            Primitive: int32_t
)");
}

TEST(FlattenerTest, MemberOfParent) {
  // Original:
  //   class C { int c; };
  //   class B { int b; C c; };
  //   class A : B { int a; };
  //
  // Flattened:
  //   class C { int c; };
  //   class A { int b; C c; int a; };
  auto myint = Primitive{Primitive::Kind::Int32};
  auto classA = Class{0, Class::Kind::Class, "ClassA", 12};
  auto classB = Class{1, Class::Kind::Class, "ClassB", 8};
  auto classC = Class{2, Class::Kind::Class, "ClassC", 4};

  classC.members.push_back(Member{myint, "c", 0});

  classB.members.push_back(Member{myint, "b", 0});
  classB.members.push_back(Member{classC, "c", 4 * 8});

  classA.parents.push_back(Parent{classB, 0});
  classA.members.push_back(Member{myint, "a", 8 * 8});

  test(Flattener::createPass(), {classA}, R"(
[0] Class: ClassA (size: 12)
      Member: b (offset: 0)
        Primitive: int32_t
      Member: c (offset: 4)
[1]     Class: ClassC (size: 4)
          Member: c (offset: 0)
            Primitive: int32_t
      Member: a (offset: 8)
        Primitive: int32_t
)");
}

TEST(FlattenerTest, ContainerParam) {
  // Original:
  //   class B { int b; };
  //   class A : B { int a; };
  //   std::vector<A, int>
  //
  // Flattened:
  //   class A { int b; int a; };
  //   std::vector<A, int>
  auto myint = Primitive{Primitive::Kind::Int32};
  auto classA = Class{1, Class::Kind::Class, "ClassA", 8};
  auto classB = Class{2, Class::Kind::Class, "ClassB", 4};
  auto container = getVector();

  classB.members.push_back(Member{myint, "b", 0});

  classA.parents.push_back(Parent{classB, 0});
  classA.members.push_back(Member{myint, "a", 4 * 8});

  container.templateParams.push_back(TemplateParam{classA});
  container.templateParams.push_back(TemplateParam{myint});

  test(Flattener::createPass(), {container}, R"(
[0] Container: std::vector (size: 24)
      Param
[1]     Class: ClassA (size: 8)
          Member: b (offset: 0)
            Primitive: int32_t
          Member: a (offset: 4)
            Primitive: int32_t
      Param
        Primitive: int32_t
)");
}

TEST(FlattenerTest, Array) {
  // Original:
  //   class B { int b; };
  //   class A : B { int a; };
  //   A[5]
  auto myint = Primitive{Primitive::Kind::Int32};

  auto classB = Class{2, Class::Kind::Class, "ClassB", 4};
  classB.members.push_back(Member{myint, "b", 0});

  auto classA = Class{1, Class::Kind::Class, "ClassA", 8};
  classA.parents.push_back(Parent{classB, 0});
  classA.members.push_back(Member{myint, "a", 4 * 8});

  auto arrayA = Array{0, classA, 5};

  test(Flattener::createPass(), {arrayA}, R"(
[0] Array: (length: 5)
[1]   Class: ClassA (size: 8)
        Member: b (offset: 0)
          Primitive: int32_t
        Member: a (offset: 4)
          Primitive: int32_t
)");
}

TEST(FlattenerTest, Typedef) {
  // Original:
  //   class B { int b; };
  //   class A : B { int a; };
  //   using aliasA = A;
  auto myint = Primitive{Primitive::Kind::Int32};
  auto classB = Class{2, Class::Kind::Class, "ClassB", 4};
  classB.members.push_back(Member{myint, "b", 0});

  auto classA = Class{1, Class::Kind::Class, "ClassA", 8};
  classA.parents.push_back(Parent{classB, 0});
  classA.members.push_back(Member{myint, "a", 4 * 8});

  auto aliasA = Typedef{0, "aliasA", classA};

  test(Flattener::createPass(), {aliasA}, R"(
[0] Typedef: aliasA
[1]   Class: ClassA (size: 8)
        Member: b (offset: 0)
          Primitive: int32_t
        Member: a (offset: 4)
          Primitive: int32_t
)");
}

TEST(FlattenerTest, TypedefParent) {
  // Original:
  //   class B { int b; };
  //   using aliasB = B;
  //   class A : aliasB { int a; };
  auto myint = Primitive{Primitive::Kind::Int32};
  auto classB = Class{1, Class::Kind::Class, "ClassB", 4};
  classB.members.push_back(Member{myint, "b", 0});

  auto aliasB = Typedef{2, "aliasB", classB};

  auto classA = Class{0, Class::Kind::Class, "ClassA", 8};
  classA.parents.push_back(Parent{aliasB, 0});
  classA.members.push_back(Member{myint, "a", 4 * 8});

  test(Flattener::createPass(), {classA}, R"(
[0] Class: ClassA (size: 8)
      Member: b (offset: 0)
        Primitive: int32_t
      Member: a (offset: 4)
        Primitive: int32_t
)");
}

TEST(FlattenerTest, Pointer) {
  // Original:
  //   class B { int b; };
  //   class A : B { int a; };
  //   class C { A a; };
  auto myint = Primitive{Primitive::Kind::Int32};

  auto classB = Class{3, Class::Kind::Class, "ClassB", 4};
  classB.members.push_back(Member{myint, "b", 0});

  auto classA = Class{2, Class::Kind::Class, "ClassA", 8};
  classA.parents.push_back(Parent{classB, 0});
  classA.members.push_back(Member{myint, "a", 4 * 8});

  auto ptrA = Pointer{1, classA};
  auto classC = Class{0, Class::Kind::Class, "ClassC", 8};
  classC.members.push_back(Member{ptrA, "a", 0});

  test(Flattener::createPass(), {classC}, R"(
[0] Class: ClassC (size: 8)
      Member: a (offset: 0)
[1]     Pointer
[2]       Class: ClassA (size: 8)
            Member: b (offset: 0)
              Primitive: int32_t
            Member: a (offset: 4)
              Primitive: int32_t
)");
}

TEST(FlattenerTest, PointerCycle) {
  // Original:
  //   class B { A a };
  //   class A { B b; };
  //
  // Flattened:
  //   No change
  auto classA = Class{0, Class::Kind::Class, "ClassA", 69};
  auto classB = Class{1, Class::Kind::Class, "ClassB", 69};
  auto ptrA = Pointer{2, classA};
  classA.members.push_back(Member{classB, "b", 0});
  classB.members.push_back(Member{ptrA, "a", 0});

  test(Flattener::createPass(), {classA, classB}, R"(
[0] Class: ClassA (size: 69)
      Member: b (offset: 0)
[1]     Class: ClassB (size: 69)
          Member: a (offset: 0)
[2]         Pointer
              [0]
    [1]
)");
}

TEST(FlattenerTest, Alignment) {
  // Original:
  //   class alignas(16) C { int c; };
  //   class B { alignas(8) int b; };
  //   class A : B, C { int a; };
  auto myint = Primitive{Primitive::Kind::Int32};
  auto classA = Class{0, Class::Kind::Class, "ClassA", 12};
  auto classB = Class{1, Class::Kind::Class, "ClassB", 4};
  auto classC = Class{2, Class::Kind::Class, "ClassC", 4};
  classC.setAlign(16);

  classC.members.push_back(Member{myint, "c", 0});

  Member memberB{myint, "b", 0};
  memberB.align = 8;
  classB.members.push_back(memberB);

  classA.parents.push_back(Parent{classB, 0});
  classA.parents.push_back(Parent{classC, 4 * 8});
  classA.members.push_back(Member{myint, "a", 8 * 8});

  test(Flattener::createPass(), {classA}, R"(
[0] Class: ClassA (size: 12)
      Parent (offset: 0)
[1]     Class: ClassB (size: 4)
          Member: b (offset: 0, align: 8)
            Primitive: int32_t
      Parent (offset: 4)
[2]     Class: ClassC (size: 4, align: 16)
          Member: c (offset: 0)
            Primitive: int32_t
      Member: a (offset: 8)
        Primitive: int32_t
)",
       R"(
[0] Class: ClassA (size: 12)
      Member: b (offset: 0, align: 8)
        Primitive: int32_t
      Member: c (offset: 4, align: 16)
        Primitive: int32_t
      Member: a (offset: 8)
        Primitive: int32_t
)");
}

TEST(FlattenerTest, Functions) {
  // Original:
  //   class C { void funcC(); };
  //   class B : C { void funcB(); };
  //   class A : B { void funcA(); };
  auto classA = Class{0, Class::Kind::Class, "ClassA", 0};
  auto classB = Class{1, Class::Kind::Class, "ClassB", 0};
  auto classC = Class{2, Class::Kind::Class, "ClassC", 0};

  classA.parents.push_back(Parent{classB, 0});
  classB.parents.push_back(Parent{classC, 0});

  classA.functions.push_back(Function{"funcA"});
  classB.functions.push_back(Function{"funcB"});
  classC.functions.push_back(Function{"funcC"});

  test(Flattener::createPass(), {classA}, R"(
[0] Class: ClassA (size: 0)
      Function: funcA
      Function: funcB
      Function: funcC
)");
}

TEST(FlattenerTest, Children) {
  // Original:
  //   class C { int c; };
  //   class B { int b; };
  //   class A : B, C { };
  auto myint = Primitive{Primitive::Kind::Int32};
  auto classA = Class{1, Class::Kind::Class, "ClassA", 8};
  auto classB = Class{0, Class::Kind::Class, "ClassB", 4};
  auto classC = Class{2, Class::Kind::Class, "ClassC", 4};

  classC.members.push_back(Member{myint, "c", 0});
  classB.members.push_back(Member{myint, "b", 0});

  classA.parents.push_back(Parent{classB, 0});
  classA.parents.push_back(Parent{classC, 4 * 8});

  classB.children.push_back(classA);
  classC.children.push_back(classA);

  test(Flattener::createPass(), {classB}, R"(
[0] Class: ClassB (size: 4)
      Member: b (offset: 0)
        Primitive: int32_t
      Child:
[1]     Class: ClassA (size: 8)
          Member: b (offset: 0)
            Primitive: int32_t
          Member: c (offset: 4)
            Primitive: int32_t
)");
}

TEST(FlattenerTest, ChildrenTwoDeep) {
  // Original:
  //   class D { int d; };
  //   class C { int c; };
  //   class B : D { int b; };
  //   class A : B, C { int a; };
  auto myint = Primitive{Primitive::Kind::Int32};
  auto classA = Class{2, Class::Kind::Class, "ClassA", 16};
  auto classB = Class{1, Class::Kind::Class, "ClassB", 8};
  auto classC = Class{3, Class::Kind::Class, "ClassC", 4};
  auto classD = Class{0, Class::Kind::Class, "ClassD", 4};

  classD.members.push_back(Member{myint, "d", 0});

  classC.members.push_back(Member{myint, "c", 0});

  classB.parents.push_back(Parent{classD, 0});
  classB.members.push_back(Member{myint, "b", 4 * 8});

  classA.parents.push_back(Parent{classB, 0});
  classA.parents.push_back(Parent{classC, 8 * 8});
  classA.members.push_back(Member{myint, "a", 12 * 8});

  classD.children.push_back(classB);
  classB.children.push_back(classA);
  classC.children.push_back(classA);

  test(Flattener::createPass(), {classD}, R"(
[0] Class: ClassD (size: 4)
      Member: d (offset: 0)
        Primitive: int32_t
      Child:
[1]     Class: ClassB (size: 8)
          Member: d (offset: 0)
            Primitive: int32_t
          Member: b (offset: 4)
            Primitive: int32_t
          Child:
[2]         Class: ClassA (size: 16)
              Member: d (offset: 0)
                Primitive: int32_t
              Member: b (offset: 4)
                Primitive: int32_t
              Member: c (offset: 8)
                Primitive: int32_t
              Member: a (offset: 12)
                Primitive: int32_t
)");
}

TEST(FlattenerTest, ParentContainer) {
  auto myint = Primitive{Primitive::Kind::Int32};

  auto vector = getVector();
  vector.templateParams.push_back(TemplateParam{myint});

  auto classA = Class{0, Class::Kind::Class, "ClassA", 32};
  classA.parents.push_back(Parent{vector, 0});
  classA.members.push_back(Member{myint, "a", 24 * 8});

  test(Flattener::createPass(), {classA}, R"(
[0] Class: ClassA (size: 32)
      Parent (offset: 0)
[1]     Container: std::vector (size: 24)
          Param
            Primitive: int32_t
      Member: a (offset: 24)
        Primitive: int32_t
)",
       R"(
[0] Class: ClassA (size: 32)
      Member: __oi_parent (offset: 0)
[1]     Container: std::vector (size: 24)
          Param
            Primitive: int32_t
      Member: a (offset: 24)
        Primitive: int32_t
)");
}

TEST(FlattenerTest, ParentTwoContainers) {
  auto myint = Primitive{Primitive::Kind::Int32};

  auto vector = getVector();
  vector.templateParams.push_back(TemplateParam{myint});

  auto classA = Class{0, Class::Kind::Class, "ClassA", 48};
  classA.parents.push_back(Parent{vector, 0});
  classA.parents.push_back(Parent{vector, 24 * 8});

  test(Flattener::createPass(), {classA}, R"(
[0] Class: ClassA (size: 48)
      Parent (offset: 0)
[1]     Container: std::vector (size: 24)
          Param
            Primitive: int32_t
      Parent (offset: 24)
        [1]
)",
       R"(
[0] Class: ClassA (size: 48)
      Member: __oi_parent (offset: 0)
[1]     Container: std::vector (size: 24)
          Param
            Primitive: int32_t
      Member: __oi_parent (offset: 24)
        [1]
)");
}

TEST(FlattenerTest, ParentClassAndContainer) {
  auto myint = Primitive{Primitive::Kind::Int32};

  auto vector = getVector();
  vector.templateParams.push_back(TemplateParam{myint});

  auto classB = Class{1, Class::Kind::Class, "ClassB", 4};
  classB.members.push_back(Member{myint, "b", 0});

  auto classA = Class{0, Class::Kind::Class, "ClassA", 32};
  classA.parents.push_back(Parent{classB, 0});
  classA.parents.push_back(Parent{vector, 8 * 8});

  test(Flattener::createPass(), {classA}, R"(
[0] Class: ClassA (size: 32)
      Parent (offset: 0)
[1]     Class: ClassB (size: 4)
          Member: b (offset: 0)
            Primitive: int32_t
      Parent (offset: 8)
[2]     Container: std::vector (size: 24)
          Param
            Primitive: int32_t
)",
       R"(
[0] Class: ClassA (size: 32)
      Member: b (offset: 0)
        Primitive: int32_t
      Member: __oi_parent (offset: 8)
[1]     Container: std::vector (size: 24)
          Param
            Primitive: int32_t
)");
}

TEST(FlattenerTest, AllocatorParamInParent) {
  ContainerInfo mapInfo{"std::map", STD_MAP_TYPE, "utility"};
  mapInfo.stubTemplateParams = {2, 3};

  Primitive myint{Primitive::Kind::Int32};

  auto pair = getPair(3);
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

  test(Flattener::createPass(), {map}, R"(
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
[0] Container: std::map (size: 24)
      Param
        Primitive: int32_t
      Param
        Primitive: int32_t
      Param
[1]     Struct: MyAlloc<std::pair<const int, int>> (size: 1)
          Param
[2]         Container: std::pair (size: 8)
              Param
                Primitive: int32_t
                Qualifiers: const
              Param
                Primitive: int32_t
          Function: allocate
          Function: deallocate
          Function: allocate
          Function: deallocate
)");
}

TEST(FlattenerTest, AllocatorUnfixableNoParent) {
  Primitive myint{Primitive::Kind::Int32};

  Class myalloc{1, Class::Kind::Struct, "MyAlloc", 1};
  myalloc.functions.push_back(Function{"allocate"});
  myalloc.functions.push_back(Function{"deallocate"});

  auto vector = getVector();
  vector.templateParams.push_back(TemplateParam{myint});
  vector.templateParams.push_back(TemplateParam{myalloc});

  test(Flattener::createPass(), {vector}, R"(
[0] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Param
[1]     Struct: MyAlloc (size: 1)
          Function: allocate
          Function: deallocate
)",
       R"(
[0] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Param
[1]     Struct: MyAlloc (size: 1)
          Function: allocate
          Function: deallocate
)");
}

TEST(FlattenerTest, AllocatorUnfixableParentNotClass) {
  // This could be supported if need-be, we just don't do it yet
  Primitive myint{Primitive::Kind::Int32};

  auto pair = getPair(3);
  pair.templateParams.push_back(TemplateParam{myint, {Qualifier::Const}});
  pair.templateParams.push_back(TemplateParam{myint});

  ContainerInfo stdAllocatorInfo{"std::allocator", DUMMY_TYPE, "memory"};
  Container stdAllocator{2, stdAllocatorInfo, 1};
  stdAllocator.templateParams.push_back(TemplateParam{pair});

  Class myalloc{1, Class::Kind::Struct, "MyAlloc", 1};
  myalloc.parents.push_back(Parent{stdAllocator, 0});
  myalloc.functions.push_back(Function{"allocate"});
  myalloc.functions.push_back(Function{"deallocate"});

  auto vector = getVector();
  vector.templateParams.push_back(TemplateParam{myint});
  vector.templateParams.push_back(TemplateParam{myalloc});

  test(Flattener::createPass(), {vector}, R"(
[0] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Param
[1]     Struct: MyAlloc (size: 1)
          Parent (offset: 0)
[2]         Container: std::allocator (size: 1)
              Param
[3]             Container: std::pair (size: 8)
                  Param
                    Primitive: int32_t
                    Qualifiers: const
                  Param
                    Primitive: int32_t
          Function: allocate
          Function: deallocate
)",
       R"(
[0] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Param
[1]     Struct: MyAlloc (size: 1)
          Member: __oi_parent (offset: 0)
[2]         Container: std::allocator (size: 1)
              Param
[3]             Container: std::pair (size: 8)
                  Param
                    Primitive: int32_t
                    Qualifiers: const
                  Param
                    Primitive: int32_t
          Function: allocate
          Function: deallocate
)");
}

TEST(FlattenerTest, AllocatorUnfixableParentNoParams) {
  Primitive myint{Primitive::Kind::Int32};

  Class myallocBase{2, Class::Kind::Struct, "MyAllocBase", 1};
  myallocBase.functions.push_back(Function{"allocate"});
  myallocBase.functions.push_back(Function{"deallocate"});

  Class myalloc{1, Class::Kind::Struct, "MyAlloc", 1};
  myalloc.parents.push_back(Parent{myallocBase, 0});
  myalloc.functions.push_back(Function{"allocate"});
  myalloc.functions.push_back(Function{"deallocate"});

  auto vector = getVector();
  vector.templateParams.push_back(TemplateParam{myint});
  vector.templateParams.push_back(TemplateParam{myalloc});

  test(Flattener::createPass(), {vector}, R"(
[0] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Param
[1]     Struct: MyAlloc (size: 1)
          Parent (offset: 0)
[2]         Struct: MyAllocBase (size: 1)
              Function: allocate
              Function: deallocate
          Function: allocate
          Function: deallocate
)",
       R"(
[0] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Param
[1]     Struct: MyAlloc (size: 1)
          Function: allocate
          Function: deallocate
          Function: allocate
          Function: deallocate
)");
}

TEST(FlattenerTest, AllocatorUnfixableParentParamIsValue) {
  ContainerInfo mapInfo{"std::map", STD_MAP_TYPE, "utility"};
  mapInfo.stubTemplateParams = {2, 3};

  Primitive myint{Primitive::Kind::Int32};

  Class myallocBase{2, Class::Kind::Struct, "MyAllocBase", 1};
  myallocBase.templateParams.push_back(TemplateParam{"123"});
  myallocBase.functions.push_back(Function{"allocate"});
  myallocBase.functions.push_back(Function{"deallocate"});

  Class myalloc{1, Class::Kind::Struct, "MyAlloc", 1};
  myalloc.parents.push_back(Parent{myallocBase, 0});
  myalloc.functions.push_back(Function{"allocate"});
  myalloc.functions.push_back(Function{"deallocate"});

  Container map{0, mapInfo, 24};
  map.templateParams.push_back(TemplateParam{myint});
  map.templateParams.push_back(TemplateParam{myint});
  map.templateParams.push_back(TemplateParam{myalloc});

  test(Flattener::createPass(), {map}, R"(
[0] Container: std::map (size: 24)
      Param
        Primitive: int32_t
      Param
        Primitive: int32_t
      Param
[1]     Struct: MyAlloc (size: 1)
          Parent (offset: 0)
[2]         Struct: MyAllocBase (size: 1)
              Param
                Value: 123
              Function: allocate
              Function: deallocate
          Function: allocate
          Function: deallocate
)",
       R"(
[0] Container: std::map (size: 24)
      Param
        Primitive: int32_t
      Param
        Primitive: int32_t
      Param
[1]     Struct: MyAlloc (size: 1)
          Function: allocate
          Function: deallocate
          Function: allocate
          Function: deallocate
)");
}

TEST(FlattenerTest, ClassParam) {
  auto myint = Primitive{Primitive::Kind::Int32};
  auto mychild = Class{1, Class::Kind::Class, "MyChild", 4};
  auto myparent = Class{2, Class::Kind::Class, "MyParent", 4};
  myparent.members.push_back(Member{myint, "a", 0});
  mychild.parents.push_back(Parent{myparent, 0});

  auto myclass = Class{0, Class::Kind::Class, "MyClass", 4};
  myclass.templateParams.push_back(TemplateParam{mychild});

  test(Flattener::createPass(), {myclass}, R"(
[0] Class: MyClass (size: 4)
      Param
[1]     Class: MyChild (size: 4)
          Parent (offset: 0)
[2]         Class: MyParent (size: 4)
              Member: a (offset: 0)
                Primitive: int32_t
)",
       R"(
[0] Class: MyClass (size: 4)
      Param
[1]     Class: MyChild (size: 4)
          Member: a (offset: 0)
            Primitive: int32_t
)");
}
