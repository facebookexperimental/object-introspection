#include <gtest/gtest.h>

#include "oi/type_graph/Flattener.h"
#include "oi/type_graph/Types.h"
#include "test/type_graph_utils.h"

using namespace type_graph;

TEST(FlattenerTest, NoParents) {
  // No change
  // Original and flattened:
  //   struct MyStruct { int n0; };
  //   class MyClass {
  //     int n;
  //     MyEnum e;
  //     MyStruct mystruct;
  //   };
  testNoChange(Flattener::createPass(), R"(
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
  test(Flattener::createPass(),
       R"(
[0] Class: ClassA (size: 8)
      Parent (offset: 0)
[1]     Class: ClassB (size: 4)
          Member: b (offset: 0)
            Primitive: int32_t
      Parent (offset: 4)
[2]     Class: ClassC (size: 4)
          Member: c (offset: 0)
            Primitive: int32_t
)",
       R"(
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

  test(Flattener::createPass(),
       R"(
[0] Class: ClassA (size: 12)
      Parent (offset: 0)
[1]     Class: ClassB (size: 4)
          Member: b (offset: 0)
            Primitive: int32_t
      Parent (offset: 4)
[2]     Class: ClassC (size: 4)
          Member: c (offset: 0)
            Primitive: int32_t
      Member: a (offset: 8)
        Primitive: int32_t
)",
       R"(
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

  test(Flattener::createPass(),
       R"(
[0] Class: ClassA (size: 12)
      Parent (offset: 4)
[1]     Class: ClassB (size: 4)
          Member: b (offset: 0)
            Primitive: int32_t
      Parent (offset: 8)
[2]     Class: ClassC (size: 4)
          Member: c (offset: 0)
            Primitive: int32_t
      Member: a (offset: 0)
        Primitive: int32_t
)",
       R"(
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

  test(Flattener::createPass(),
       R"(
[0] Class: ClassA (size: 16)
      Parent (offset: 0)
[1]     Class: ClassB (size: 4)
          Member: b (offset: 0)
            Primitive: int32_t
      Parent (offset: 12)
[2]     Class: ClassC (size: 4)
          Member: c (offset: 0)
            Primitive: int32_t
      Member: a1 (offset: 4)
        Primitive: int32_t
      Member: a2 (offset: 8)
        Primitive: int32_t
)",
       R"(
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

  test(Flattener::createPass(),
       R"(
[0] Class: ClassA (size: 12)
      Parent (offset: 0)
[1]     Class: ClassB (size: 0)
      Parent (offset: 0)
[2]     Class: ClassC (size: 4)
          Member: c (offset: 0)
            Primitive: int32_t
      Member: a1 (offset: 4)
        Primitive: int32_t
      Member: a2 (offset: 8)
        Primitive: int32_t
)",
       R"(
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

  test(Flattener::createPass(),
       R"(
[0] Class: ClassA (size: 16)
      Parent (offset: 0)
[1]     Class: ClassB (size: 8)
          Parent (offset: 0)
[2]         Class: ClassD (size: 4)
              Member: d (offset: 0)
                Primitive: int32_t
          Member: b (offset: 4)
            Primitive: int32_t
      Parent (offset: 8)
[3]     Class: ClassC (size: 4)
          Member: c (offset: 0)
            Primitive: int32_t
      Member: a (offset: 12)
        Primitive: int32_t
)",
       R"(
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

  test(Flattener::createPass(),
       R"(
[0] Class: ClassA (size: 16)
      Parent (offset: 0)
[1]     Class: ClassB (size: 8)
          Parent (offset: 0)
[2]         Class: ClassC (size: 4)
              Member: c (offset: 0)
                Primitive: int32_t
          Member: b (offset: 4)
            Primitive: int32_t
      Parent (offset: 8)
        [2]
      Member: a (offset: 12)
        Primitive: int32_t
)",
       R"(
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

  test(Flattener::createPass(),
       R"(
[0] Class: ClassA (size: 12)
      Member: a (offset: 0)
        Primitive: int32_t
      Member: b (offset: 4)
[1]     Class: ClassB (size: 8)
          Parent (offset: 0)
[2]         Class: ClassC (size: 4)
              Member: c (offset: 0)
                Primitive: int32_t
          Member: b (offset: 4)
            Primitive: int32_t
)",
       R"(
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

  test(Flattener::createPass(),
       R"(
[0] Class: ClassA (size: 12)
      Parent (offset: 0)
[1]     Class: ClassB (size: 8)
          Member: b (offset: 0)
            Primitive: int32_t
          Member: c (offset: 4)
[2]         Class: ClassC (size: 4)
              Member: c (offset: 0)
                Primitive: int32_t
      Member: a (offset: 8)
        Primitive: int32_t
)",
       R"(
[0] Class: ClassA (size: 12)
      Member: b (offset: 0)
        Primitive: int32_t
      Member: c (offset: 4)
[2]     Class: ClassC (size: 4)
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

  test(Flattener::createPass(),
       R"(
[0] Container: std::vector (size: 24)
      Param
[1]     Class: ClassA (size: 8)
          Parent (offset: 0)
[2]         Class: ClassB (size: 4)
              Member: b (offset: 0)
                Primitive: int32_t
          Member: a (offset: 4)
            Primitive: int32_t
      Param
        Primitive: int32_t
)",
       R"(
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

  test(Flattener::createPass(),
       R"(
[0] Array: (length: 5)
[1]   Class: ClassA (size: 8)
        Parent (offset: 0)
[2]       Class: ClassB (size: 4)
            Member: b (offset: 0)
              Primitive: int32_t
        Member: a (offset: 4)
          Primitive: int32_t
)",
       R"(
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

  test(Flattener::createPass(),
       R"(
[0] Typedef: aliasA
[1]   Class: ClassA (size: 8)
        Parent (offset: 0)
[2]       Class: ClassB (size: 4)
            Member: b (offset: 0)
              Primitive: int32_t
        Member: a (offset: 4)
          Primitive: int32_t
)",
       R"(
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

  test(Flattener::createPass(),
       R"(
[0] Class: ClassA (size: 8)
      Parent (offset: 0)
[1]     Typedef: aliasB
[2]       Class: ClassB (size: 4)
            Member: b (offset: 0)
              Primitive: int32_t
      Member: a (offset: 4)
        Primitive: int32_t
)",
       R"(
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
  //   class C { A* a; };
  auto myint = Primitive{Primitive::Kind::Int32};

  auto classB = Class{3, Class::Kind::Class, "ClassB", 4};
  classB.members.push_back(Member{myint, "b", 0});

  auto classA = Class{2, Class::Kind::Class, "ClassA", 8};
  classA.parents.push_back(Parent{classB, 0});
  classA.members.push_back(Member{myint, "a", 4 * 8});

  auto ptrA = Pointer{1, classA};
  auto classC = Class{0, Class::Kind::Class, "ClassC", 8};
  classC.members.push_back(Member{ptrA, "a", 0});

  test(Flattener::createPass(),
       R"(
[0] Class: ClassC (size: 8)
      Member: a (offset: 0)
[1]     Pointer
[2]       Class: ClassA (size: 8)
            Parent (offset: 0)
[3]           Class: ClassB (size: 4)
                Member: b (offset: 0)
                  Primitive: int32_t
            Member: a (offset: 4)
              Primitive: int32_t
)",
       R"(
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
  auto classA = Class{0, Class::Kind::Class, "ClassA", 69};
  auto classB = Class{1, Class::Kind::Class, "ClassB", 69};
  auto ptrA = Pointer{2, classA};
  classA.members.push_back(Member{classB, "b", 0});
  classB.members.push_back(Member{ptrA, "a", 0});

  test(Flattener::createPass(),
       R"(
[0] Class: ClassA (size: 69)
      Member: b (offset: 0)
[1]     Class: ClassB (size: 69)
          Member: a (offset: 0)
[2]         Pointer
              [0]
    [1]
)",
       R"(
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

  test(Flattener::createPass(),
       R"(
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

  test(Flattener::createPass(),
       R"(
[0] Class: ClassA (size: 0)
      Parent (offset: 0)
[1]     Class: ClassB (size: 0)
          Function: funcB
      Parent (offset: 0)
[2]     Class: ClassC (size: 0)
          Function: funcC
      Function: funcA
)",
       R"(
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

  test(Flattener::createPass(),
       R"(
[0] Class: ClassB (size: 4)
      Member: b (offset: 0)
        Primitive: int32_t
      Child
[1]     Class: ClassA (size: 8)
          Parent (offset: 0)
            [0]
          Parent (offset: 4)
[2]         Class: ClassC (size: 4)
              Member: c (offset: 0)
                Primitive: int32_t
              Child
                [1]
)",
       R"(
[0] Class: ClassB (size: 4)
      Member: b (offset: 0)
        Primitive: int32_t
      Child
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

  test(Flattener::createPass(),
       R"(
[0] Class: ClassD (size: 4)
      Member: d (offset: 0)
        Primitive: int32_t
      Child
[1]     Class: ClassB (size: 8)
          Parent (offset: 0)
            [0]
          Member: b (offset: 4)
            Primitive: int32_t
          Child
[2]         Class: ClassA (size: 16)
              Parent (offset: 0)
                [1]
              Parent (offset: 8)
[3]             Class: ClassC (size: 4)
                  Member: c (offset: 0)
                    Primitive: int32_t
                  Child
                    [2]
              Member: a (offset: 12)
                Primitive: int32_t
)",
       R"(
[0] Class: ClassD (size: 4)
      Member: d (offset: 0)
        Primitive: int32_t
      Child
[1]     Class: ClassB (size: 8)
          Member: d (offset: 0)
            Primitive: int32_t
          Member: b (offset: 4)
            Primitive: int32_t
          Child
[2]         Class: ClassA (size: 16)
              Member: d (offset: 0)
                Primitive: int32_t
              Member: b (offset: 4)
                Primitive: int32_t
              Member: c (offset: 8)
                Primitive: int32_t
              Member: a (offset: 12)
                Primitive: int32_t
      Child
        [2]
)");
}

TEST(FlattenerTest, ParentContainer) {
  test(Flattener::createPass(),
       R"(
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
  test(Flattener::createPass(),
       R"(
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
  test(Flattener::createPass(),
       R"(
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
[2]     Container: std::vector (size: 24)
          Param
            Primitive: int32_t
)");
}

TEST(FlattenerTest, ContainerWithParent) {
  // This is necessary to correctly calculate container alignment
  test(Flattener::createPass(),
       R"(
[0] Container: std::vector (size: 24)
      Underlying
[1]     Class: vector (size: 24)
          Parent (offset: 0)
[2]         Class: Parent (size: 4)
              Member: x (offset: 0)
                Primitive: int32_t
)",
       R"(
[0] Container: std::vector (size: 24)
      Underlying
[1]     Class: vector (size: 24)
          Member: x (offset: 0)
            Primitive: int32_t
)");
}

TEST(FlattenerTest, AllocatorParamInParent) {
  test(Flattener::createPass(),
       R"(
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
[3]         Container: std::pair (size: 8)
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
  testNoChange(Flattener::createPass(), R"(
[0] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Param
[1]     Struct: MyAlloc (size: 1)
          Function: allocate
          Function: deallocate
)");
}

TEST(FlattenerTest, AllocatorParamInParentContainer) {
  // This could be supported if need-be, we just don't do it yet
  test(Flattener::createPass(),
       R"(
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
  test(Flattener::createPass(),
       R"(
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
  test(Flattener::createPass(),
       R"(
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
[1]     Struct: MyAlloc (size: 1)
          Function: allocate
          Function: deallocate
          Function: allocate
          Function: deallocate
)");
}

TEST(FlattenerTest, ClassParam) {
  test(Flattener::createPass(),
       R"(
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

TEST(FlattenerTest, IncompleteParent) {
  test(Flattener::createPass(),
       R"(
[0] Class: MyClass (size: 4)
      Parent (offset: 0)
        Incomplete: [IncompleteParent]
)",
       R"(
[0] Class: MyClass (size: 4)
)");
}
