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
  auto myint = std::make_unique<Primitive>(Primitive::Kind::Int32);
  auto myenum = std::make_unique<Enum>("MyEnum", 4);
  auto mystruct = std::make_unique<Class>(Class::Kind::Struct, "MyStruct", 4);
  auto myclass = std::make_unique<Class>(Class::Kind::Class, "MyClass", 12);

  mystruct->members.push_back(Member(myint.get(), "n0", 0));

  myclass->members.push_back(Member(myint.get(), "n", 0));
  myclass->members.push_back(Member(myenum.get(), "e", 4));
  myclass->members.push_back(Member(mystruct.get(), "mystruct", 8));

  test(Flattener::createPass(), {*myclass}, R"(
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
  auto myint = std::make_unique<Primitive>(Primitive::Kind::Int32);
  auto classA = std::make_unique<Class>(Class::Kind::Class, "ClassA", 8);
  auto classB = std::make_unique<Class>(Class::Kind::Class, "ClassB", 4);
  auto classC = std::make_unique<Class>(Class::Kind::Class, "ClassC", 4);

  classC->members.push_back(Member(myint.get(), "c", 0));
  classB->members.push_back(Member(myint.get(), "b", 0));

  classA->parents.push_back(Parent(classB.get(), 0));
  classA->parents.push_back(Parent(classC.get(), 4));

  test(Flattener::createPass(), {*classA}, R"(
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
  auto myint = std::make_unique<Primitive>(Primitive::Kind::Int32);
  auto classA = std::make_unique<Class>(Class::Kind::Class, "ClassA", 12);
  auto classB = std::make_unique<Class>(Class::Kind::Class, "ClassB", 4);
  auto classC = std::make_unique<Class>(Class::Kind::Class, "ClassC", 4);

  classC->members.push_back(Member(myint.get(), "c", 0));
  classB->members.push_back(Member(myint.get(), "b", 0));

  classA->parents.push_back(Parent(classB.get(), 0));
  classA->parents.push_back(Parent(classC.get(), 4));
  classA->members.push_back(Member(myint.get(), "a", 8));

  test(Flattener::createPass(), {*classA}, R"(
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
  auto myint = std::make_unique<Primitive>(Primitive::Kind::Int32);
  auto classA = std::make_unique<Class>(Class::Kind::Class, "ClassA", 12);
  auto classB = std::make_unique<Class>(Class::Kind::Class, "ClassB", 4);
  auto classC = std::make_unique<Class>(Class::Kind::Class, "ClassC", 4);

  classC->members.push_back(Member(myint.get(), "c", 0));

  classB->members.push_back(Member(myint.get(), "b", 0));

  classA->members.push_back(Member(myint.get(), "a", 0));
  classA->parents.push_back(Parent(classB.get(), 4));
  classA->parents.push_back(Parent(classC.get(), 8));

  test(Flattener::createPass(), {*classA}, R"(
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
  auto myint = std::make_unique<Primitive>(Primitive::Kind::Int32);
  auto classA = std::make_unique<Class>(Class::Kind::Class, "ClassA", 16);
  auto classB = std::make_unique<Class>(Class::Kind::Class, "ClassB", 4);
  auto classC = std::make_unique<Class>(Class::Kind::Class, "ClassC", 4);

  classC->members.push_back(Member(myint.get(), "c", 0));

  classB->members.push_back(Member(myint.get(), "b", 0));

  classA->parents.push_back(Parent(classB.get(), 0));
  classA->members.push_back(Member(myint.get(), "a1", 4));
  classA->members.push_back(Member(myint.get(), "a2", 8));
  classA->parents.push_back(Parent(classC.get(), 12));

  test(Flattener::createPass(), {*classA}, R"(
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
  auto myint = std::make_unique<Primitive>(Primitive::Kind::Int32);
  auto classA = std::make_unique<Class>(Class::Kind::Class, "ClassA", 12);
  auto classB = std::make_unique<Class>(Class::Kind::Class, "ClassB", 0);
  auto classC = std::make_unique<Class>(Class::Kind::Class, "ClassC", 4);

  classC->members.push_back(Member(myint.get(), "c", 0));

  classA->members.push_back(Member(myint.get(), "a1", 4));
  classA->members.push_back(Member(myint.get(), "a2", 8));
  classA->parents.push_back(Parent(classB.get(), 4));
  classA->parents.push_back(Parent(classC.get(), 0));

  test(Flattener::createPass(), {*classA}, R"(
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
  auto myint = std::make_unique<Primitive>(Primitive::Kind::Int32);
  auto classA = std::make_unique<Class>(Class::Kind::Class, "ClassA", 16);
  auto classB = std::make_unique<Class>(Class::Kind::Class, "ClassB", 8);
  auto classC = std::make_unique<Class>(Class::Kind::Class, "ClassC", 4);
  auto classD = std::make_unique<Class>(Class::Kind::Class, "ClassD", 4);

  classD->members.push_back(Member(myint.get(), "d", 0));

  classC->members.push_back(Member(myint.get(), "c", 0));

  classB->parents.push_back(Parent(classD.get(), 0));
  classB->members.push_back(Member(myint.get(), "b", 4));

  classA->parents.push_back(Parent(classB.get(), 0));
  classA->parents.push_back(Parent(classC.get(), 8));
  classA->members.push_back(Member(myint.get(), "a", 12));

  test(Flattener::createPass(), {*classA}, R"(
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
  auto myint = std::make_unique<Primitive>(Primitive::Kind::Int32);
  auto classA = std::make_unique<Class>(Class::Kind::Class, "ClassA", 16);
  auto classB = std::make_unique<Class>(Class::Kind::Class, "ClassB", 8);
  auto classC = std::make_unique<Class>(Class::Kind::Class, "ClassC", 4);

  classC->members.push_back(Member(myint.get(), "c", 0));

  classB->parents.push_back(Parent(classC.get(), 0));
  classB->members.push_back(Member(myint.get(), "b", 4));

  classA->parents.push_back(Parent(classB.get(), 0));
  classA->parents.push_back(Parent(classC.get(), 8));
  classA->members.push_back(Member(myint.get(), "a", 12));

  test(Flattener::createPass(), {*classA}, R"(
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
  auto myint = std::make_unique<Primitive>(Primitive::Kind::Int32);
  auto classA = std::make_unique<Class>(Class::Kind::Class, "ClassA", 12);
  auto classB = std::make_unique<Class>(Class::Kind::Class, "ClassB", 8);
  auto classC = std::make_unique<Class>(Class::Kind::Class, "ClassC", 4);

  classC->members.push_back(Member(myint.get(), "c", 0));

  classB->parents.push_back(Parent(classC.get(), 0));
  classB->members.push_back(Member(myint.get(), "b", 4));

  classA->members.push_back(Member(myint.get(), "a", 0));
  classA->members.push_back(Member(classB.get(), "b", 4));

  test(Flattener::createPass(), {*classA}, R"(
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
  auto myint = std::make_unique<Primitive>(Primitive::Kind::Int32);
  auto classA = std::make_unique<Class>(Class::Kind::Class, "ClassA", 12);
  auto classB = std::make_unique<Class>(Class::Kind::Class, "ClassB", 8);
  auto classC = std::make_unique<Class>(Class::Kind::Class, "ClassC", 4);

  classC->members.push_back(Member(myint.get(), "c", 0));

  classB->members.push_back(Member(myint.get(), "b", 0));
  classB->members.push_back(Member(classC.get(), "c", 4));

  classA->parents.push_back(Parent(classB.get(), 0));
  classA->members.push_back(Member(myint.get(), "a", 8));

  test(Flattener::createPass(), {*classA}, R"(
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
  auto myint = std::make_unique<Primitive>(Primitive::Kind::Int32);
  auto classA = std::make_unique<Class>(Class::Kind::Class, "ClassA", 8);
  auto classB = std::make_unique<Class>(Class::Kind::Class, "ClassB", 4);
  auto container = getVector();

  classB->members.push_back(Member(myint.get(), "b", 0));

  classA->parents.push_back(Parent(classB.get(), 0));
  classA->members.push_back(Member(myint.get(), "a", 4));

  container.templateParams.push_back(TemplateParam(classA.get()));
  container.templateParams.push_back(TemplateParam(myint.get()));

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
  auto myint = std::make_unique<Primitive>(Primitive::Kind::Int32);

  auto classB = std::make_unique<Class>(Class::Kind::Class, "ClassB", 4);
  classB->members.push_back(Member(myint.get(), "b", 0));

  auto classA = std::make_unique<Class>(Class::Kind::Class, "ClassA", 8);
  classA->parents.push_back(Parent(classB.get(), 0));
  classA->members.push_back(Member(myint.get(), "a", 4));

  auto arrayA = std::make_unique<Array>(classA.get(), 5);

  test(Flattener::createPass(), {*arrayA}, R"(
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
  auto myint = std::make_unique<Primitive>(Primitive::Kind::Int32);
  auto classB = std::make_unique<Class>(Class::Kind::Class, "ClassB", 4);
  classB->members.push_back(Member(myint.get(), "b", 0));

  auto classA = std::make_unique<Class>(Class::Kind::Class, "ClassA", 8);
  classA->parents.push_back(Parent(classB.get(), 0));
  classA->members.push_back(Member(myint.get(), "a", 4));

  auto aliasA = std::make_unique<Typedef>("aliasA", classA.get());

  test(Flattener::createPass(), {*aliasA}, R"(
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
  auto myint = std::make_unique<Primitive>(Primitive::Kind::Int32);
  auto classB = std::make_unique<Class>(Class::Kind::Class, "ClassB", 4);
  classB->members.push_back(Member(myint.get(), "b", 0));

  auto aliasB = std::make_unique<Typedef>("aliasB", classB.get());

  auto classA = std::make_unique<Class>(Class::Kind::Class, "ClassA", 8);
  classA->parents.push_back(Parent(aliasB.get(), 0));
  classA->members.push_back(Member(myint.get(), "a", 4));

  test(Flattener::createPass(), {*classA}, R"(
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
  //   class C { A *a; };
  auto myint = std::make_unique<Primitive>(Primitive::Kind::Int32);

  auto classB = std::make_unique<Class>(Class::Kind::Class, "ClassB", 4);
  classB->members.push_back(Member(myint.get(), "b", 0));

  auto classA = std::make_unique<Class>(Class::Kind::Class, "ClassA", 8);
  classA->parents.push_back(Parent(classB.get(), 0));
  classA->members.push_back(Member(myint.get(), "a", 4));

  auto ptrA = std::make_unique<Pointer>(classA.get());
  auto classC = std::make_unique<Class>(Class::Kind::Class, "ClassC", 8);
  classC->members.push_back(Member(ptrA.get(), "a", 0));

  test(Flattener::createPass(), {*classC}, R"(
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
  //   class B { A* a };
  //   class A { B b; };
  //
  // Flattened:
  //   No change
  auto classA = std::make_unique<Class>(Class::Kind::Class, "ClassA", 69);
  auto classB = std::make_unique<Class>(Class::Kind::Class, "ClassB", 69);
  auto ptrA = std::make_unique<Pointer>(classA.get());
  classA->members.push_back(Member(classB.get(), "b", 0));
  classB->members.push_back(Member(ptrA.get(), "a", 0));

  test(Flattener::createPass(), {*classA, *classB}, R"(
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
  auto myint = std::make_unique<Primitive>(Primitive::Kind::Int32);
  auto classA = std::make_unique<Class>(Class::Kind::Class, "ClassA", 12);
  auto classB = std::make_unique<Class>(Class::Kind::Class, "ClassB", 4);
  auto classC = std::make_unique<Class>(Class::Kind::Class, "ClassC", 4);
  classC->setAlign(16);

  classC->members.push_back(Member(myint.get(), "c", 0));
  classB->members.push_back(Member(myint.get(), "b", 0, 8));

  classA->parents.push_back(Parent(classB.get(), 0));
  classA->parents.push_back(Parent(classC.get(), 4));
  classA->members.push_back(Member(myint.get(), "a", 8));

  test(Flattener::createPass(), {*classA}, R"(
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
  auto classA = std::make_unique<Class>(Class::Kind::Class, "ClassA", 0);
  auto classB = std::make_unique<Class>(Class::Kind::Class, "ClassB", 0);
  auto classC = std::make_unique<Class>(Class::Kind::Class, "ClassC", 0);

  classA->parents.push_back(Parent(classB.get(), 0));
  classB->parents.push_back(Parent(classC.get(), 0));

  classA->functions.push_back(Function{"funcA"});
  classB->functions.push_back(Function{"funcB"});
  classC->functions.push_back(Function{"funcC"});

  test(Flattener::createPass(), {*classA}, R"(
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
  auto myint = std::make_unique<Primitive>(Primitive::Kind::Int32);
  auto classA = std::make_unique<Class>(Class::Kind::Class, "ClassA", 8);
  auto classB = std::make_unique<Class>(Class::Kind::Class, "ClassB", 4);
  auto classC = std::make_unique<Class>(Class::Kind::Class, "ClassC", 4);

  classC->members.push_back(Member(myint.get(), "c", 0));
  classB->members.push_back(Member(myint.get(), "b", 0));

  classA->parents.push_back(Parent(classB.get(), 0));
  classA->parents.push_back(Parent(classC.get(), 4));

  classB->children.push_back(*classA);
  classC->children.push_back(*classA);

  test(Flattener::createPass(), {*classB}, R"(
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
  auto myint = std::make_unique<Primitive>(Primitive::Kind::Int32);
  auto classA = std::make_unique<Class>(Class::Kind::Class, "ClassA", 16);
  auto classB = std::make_unique<Class>(Class::Kind::Class, "ClassB", 8);
  auto classC = std::make_unique<Class>(Class::Kind::Class, "ClassC", 4);
  auto classD = std::make_unique<Class>(Class::Kind::Class, "ClassD", 4);

  classD->members.push_back(Member(myint.get(), "d", 0));

  classC->members.push_back(Member(myint.get(), "c", 0));

  classB->parents.push_back(Parent(classD.get(), 0));
  classB->members.push_back(Member(myint.get(), "b", 4));

  classA->parents.push_back(Parent(classB.get(), 0));
  classA->parents.push_back(Parent(classC.get(), 8));
  classA->members.push_back(Member(myint.get(), "a", 12));

  classD->children.push_back(*classB);
  classB->children.push_back(*classA);
  classC->children.push_back(*classA);

  test(Flattener::createPass(), {*classD}, R"(
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
  vector.templateParams.push_back(TemplateParam{&myint});

  auto classA = Class{Class::Kind::Class, "ClassA", 32};
  classA.parents.push_back(Parent{&vector, 0});
  classA.members.push_back(Member{&myint, "a", 24});

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
      Member: __parent (offset: 0)
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
  vector.templateParams.push_back(TemplateParam{&myint});

  auto classA = Class{Class::Kind::Class, "ClassA", 48};
  classA.parents.push_back(Parent{&vector, 0});
  classA.parents.push_back(Parent{&vector, 24});

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
      Member: __parent (offset: 0)
[1]     Container: std::vector (size: 24)
          Param
            Primitive: int32_t
      Member: __parent (offset: 24)
        [1]
)");
}

TEST(FlattenerTest, ParentClassAndContainer) {
  auto myint = Primitive{Primitive::Kind::Int32};

  auto vector = getVector();
  vector.templateParams.push_back(TemplateParam{&myint});

  auto classB = Class{Class::Kind::Class, "ClassB", 4};
  classB.members.push_back(Member{&myint, "b", 0});

  auto classA = Class{Class::Kind::Class, "ClassA", 32};
  classA.parents.push_back(Parent{&classB, 0});
  classA.parents.push_back(Parent{&vector, 8});

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
      Member: __parent (offset: 8)
[1]     Container: std::vector (size: 24)
          Param
            Primitive: int32_t
)");
}

TEST(FlattenerTest, AllocatorParamInParent) {
  ContainerInfo pairInfo{"std::pair", SEQ_TYPE, "utility"};

  ContainerInfo mapInfo{"std::map", STD_MAP_TYPE, "utility"};
  mapInfo.stubTemplateParams = {2, 3};

  Primitive myint{Primitive::Kind::Int32};

  Container pair{pairInfo, 8};
  pair.templateParams.push_back(TemplateParam{&myint, {Qualifier::Const}});
  pair.templateParams.push_back(TemplateParam{&myint});

  Class myallocBase{Class::Kind::Struct,
                    "MyAllocBase<std::pair<const int, int>>", 1};
  myallocBase.templateParams.push_back(TemplateParam{&pair});
  myallocBase.functions.push_back(Function{"allocate"});
  myallocBase.functions.push_back(Function{"deallocate"});

  Class myalloc{Class::Kind::Struct, "MyAlloc<std::pair<const int, int>>", 1};
  myalloc.parents.push_back(Parent{&myallocBase, 0});
  myalloc.functions.push_back(Function{"allocate"});
  myalloc.functions.push_back(Function{"deallocate"});

  Container map{mapInfo, 24};
  map.templateParams.push_back(TemplateParam{&myint});
  map.templateParams.push_back(TemplateParam{&myint});
  map.templateParams.push_back(TemplateParam{&myalloc});

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
