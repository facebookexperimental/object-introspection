#include <gtest/gtest.h>

#include <boost/algorithm/string.hpp>

#include "oi/type_graph/TopoSorter.h"
#include "oi/type_graph/Types.h"
#include "test/type_graph_utils.h"

using namespace type_graph;

template <typename T>
using ref = std::reference_wrapper<T>;

void test(const std::vector<ref<Type>> input, std::string expected) {
  TopoSorter topo;
  topo.sort(input);

  std::string output;
  for (const Type& type : topo.sortedTypes()) {
    output += type.name();
    output.push_back('\n');
  }

  boost::trim(expected);
  boost::trim(output);

  EXPECT_EQ(expected, output);
}

TEST(TopoSorterTest, SingleType) {
  auto myenum = Enum{"MyEnum", 4};
  test({myenum}, "MyEnum");
}

TEST(TopoSorterTest, UnrelatedTypes) {
  auto mystruct = Class{0, Class::Kind::Struct, "MyStruct", 13};
  auto myenum = Enum{"MyEnum", 4};
  auto myclass = Class{1, Class::Kind::Class, "MyClass", 69};

  // Try the same input in a few different orders and ensure they output order
  // matches the input order
  test({mystruct, myenum, myclass}, R"(
MyStruct
MyEnum
MyClass
)");
  test({myenum, mystruct, myclass}, R"(
MyEnum
MyStruct
MyClass
)");
  test({myclass, myenum, mystruct}, R"(
MyClass
MyEnum
MyStruct
)");
}

TEST(TopoSorterTest, ClassMembers) {
  auto mystruct = Class{0, Class::Kind::Struct, "MyStruct", 13};
  auto myenum = Enum{"MyEnum", 4};
  auto myclass = Class{1, Class::Kind::Class, "MyClass", 69};
  myclass.members.push_back(Member{mystruct, "n", 0});
  myclass.members.push_back(Member{myenum, "e", 4});

  test({myclass}, R"(
MyStruct
MyEnum
MyClass
)");
}

TEST(TopoSorterTest, Parents) {
  auto myparent = Class{0, Class::Kind::Struct, "MyParent", 13};
  auto myclass = Class{1, Class::Kind::Class, "MyClass", 69};
  myclass.parents.push_back(Parent{myparent, 0});

  test({myclass}, R"(
MyParent
MyClass
)");
}

TEST(TopoSorterTest, TemplateParams) {
  auto myparam = Class{0, Class::Kind::Struct, "MyParam", 13};
  auto myclass = Class{1, Class::Kind::Class, "MyClass", 69};
  myclass.templateParams.push_back(TemplateParam{myparam});

  test({myclass}, R"(
MyParam
MyClass
)");
}

TEST(TopoSorterTest, TemplateParamValue) {
  auto myclass = Class{1, Class::Kind::Class, "MyClass", 69};
  auto myint = Primitive{Primitive::Kind::Int32};
  myclass.templateParams.push_back(TemplateParam{myint, "123"});

  test({myclass}, R"(
int32_t
MyClass
)");
}

TEST(TopoSorterTest, Children) {
  auto mymember = Class{0, Class::Kind::Struct, "MyMember", 13};
  auto mychild = Class{1, Class::Kind::Struct, "MyChild", 13};
  mychild.members.push_back(Member{mymember, "mymember", 0});

  auto myclass = Class{2, Class::Kind::Class, "MyClass", 69};
  mychild.parents.push_back(Parent{myclass, 0});
  myclass.children.push_back(mychild);

  std::vector<std::vector<ref<Type>>> inputs = {
      {myclass},
      {mychild},
  };

  // Same as for pointer cycles, outputs must be in the same order no matter
  // which type we start the sort on.
  for (const auto& input : inputs) {
    test(input, R"(
MyClass
MyMember
MyChild
)");
  }
}

TEST(TopoSorterTest, ChildrenCycle) {
  // class MyParent {};
  // class ClassA {
  //   MyParent p;
  // };
  // class MyChild : MyParent {
  //   A a;
  // };
  auto myparent = Class{0, Class::Kind::Class, "MyParent", 69};
  auto classA = Class{1, Class::Kind::Struct, "ClassA", 5};
  auto mychild = Class{2, Class::Kind::Struct, "MyChild", 13};

  mychild.parents.push_back(Parent{myparent, 0});
  myparent.children.push_back(mychild);

  mychild.members.push_back(Member{classA, "a", 0});
  classA.members.push_back(Member{myparent, "p", 0});

  std::vector<std::vector<ref<Type>>> inputs = {
      {myparent},
      {classA},
      {mychild},
  };

  // Same as for pointer cycles, outputs must be in the same order no matter
  // which type we start the sort on.
  for (const auto& input : inputs) {
    test(input, R"(
MyParent
ClassA
MyChild
)");
  }
}

TEST(TopoSorterTest, Containers) {
  auto myparam1 = Class{1, Class::Kind::Struct, "MyParam1", 13};
  auto myparam2 = Class{2, Class::Kind::Struct, "MyParam2", 13};
  auto mycontainer = getMap();
  mycontainer.templateParams.push_back((myparam1));
  mycontainer.templateParams.push_back((myparam2));

  test({mycontainer}, R"(
MyParam1
MyParam2
std::map
)");
}

TEST(TopoSorterTest, ContainersVector) {
  // std::vector allows a forward declared type
  auto myparam = Class{1, Class::Kind::Struct, "MyParam", 13};
  auto myalloc = Class{1, Class::Kind::Struct, "MyAlloc", 0};
  auto mycontainer = getVector();
  mycontainer.templateParams.push_back((myparam));
  mycontainer.templateParams.push_back((myalloc));

  test({mycontainer}, R"(
MyAlloc
std::vector
MyParam
)");
}

TEST(TopoSorterTest, ContainersList) {
  // std::list allows a forward declared type
  auto myparam = Class{1, Class::Kind::Struct, "MyParam", 13};
  auto myalloc = Class{1, Class::Kind::Struct, "MyAlloc", 0};
  auto mycontainer = getList();
  mycontainer.templateParams.push_back((myparam));
  mycontainer.templateParams.push_back((myalloc));

  test({mycontainer}, R"(
MyAlloc
std::list
MyParam
)");
}

TEST(TopoSorterTest, Arrays) {
  auto myclass = Class{0, Class::Kind::Class, "MyClass", 69};
  auto myarray = Array{1, myclass, 10};

  test({myarray}, R"(
MyClass
OIArray<MyClass, 10>
)");
}

TEST(TopoSorterTest, Typedef) {
  auto classA = Class{0, Class::Kind::Class, "ClassA", 8};
  auto aliasA = Typedef{1, "aliasA", classA};

  test({aliasA}, R"(
ClassA
aliasA
)");
}

TEST(TopoSorterTest, Pointers) {
  // Pointers do not require pointee types to be defined first
  auto classA = Class{0, Class::Kind::Class, "ClassA", 69};
  auto mypointer = Pointer{1, classA};

  auto myclass = Class{2, Class::Kind::Class, "MyClass", 69};
  myclass.members.push_back(Member{mypointer, "ptr", 0});

  test({myclass}, R"(
MyClass
ClassA
)");
}

TEST(TopoSorterTest, PointerCycle) {
  auto classA = Class{0, Class::Kind::Class, "ClassA", 69};
  auto classB = Class{1, Class::Kind::Class, "ClassB", 69};
  auto ptrA = Pointer{2, classA};
  classA.members.push_back(Member{classB, "b", 0});
  classB.members.push_back(Member{ptrA, "a", 0});

  std::vector<std::vector<ref<Type>>> inputs = {
      {classA},
      {classB},
      {ptrA},
  };

  // No matter which node we start the topological sort with, we must always get
  // the same sorted order for ClassA and ClassB.
  for (const auto& input : inputs) {
    test(input, R"(
ClassB
ClassA
)");
  }
}

TEST(TopoSorterTest, PointerToTypedef) {
  auto classA = Class{0, Class::Kind::Class, "ClassA", 8};
  auto aliasA = Typedef{1, "aliasA", classA};

  auto mypointer = Pointer{1, aliasA};

  auto myclass = Class{2, Class::Kind::Class, "MyClass", 69};
  myclass.members.push_back(Member{mypointer, "ptrToTypedef", 0});

  test({myclass}, R"(
ClassA
aliasA
MyClass
)");
}

TEST(TopoSorterTest, TwoDeep) {
  auto myunion = Class{0, Class::Kind::Union, "MyUnion", 7};
  auto mystruct = Class{1, Class::Kind::Struct, "MyStruct", 13};
  auto myclass = Class{2, Class::Kind::Class, "MyClass", 69};
  myclass.members.push_back(Member{mystruct, "mystruct", 0});
  mystruct.members.push_back(Member{myunion, "myunion", 0});

  test({myclass}, R"(
MyUnion
MyStruct
MyClass
)");
}

TEST(TopoSorterTest, MultiplePaths) {
  auto myunion = Class{0, Class::Kind::Union, "MyUnion", 7};
  auto mystruct = Class{1, Class::Kind::Struct, "MyStruct", 13};
  auto myclass = Class{2, Class::Kind::Class, "MyClass", 69};
  myclass.members.push_back(Member{mystruct, "mystruct", 0});
  myclass.members.push_back(Member{myunion, "myunion1", 0});
  mystruct.members.push_back(Member{myunion, "myunion2", 0});

  test({myclass}, R"(
MyUnion
MyStruct
MyClass
)");
}

TEST(TopoSorterTest, CaptureKeys) {
  auto myparam1 = Class{1, Class::Kind::Struct, "MyParam1", 13};
  auto myparam2 = Class{2, Class::Kind::Struct, "MyParam2", 13};
  auto mycontainer = getMap();
  mycontainer.templateParams.push_back((myparam1));
  mycontainer.templateParams.push_back((myparam2));

  auto captureKeys = CaptureKeys{mycontainer, mycontainer.containerInfo_};

  test({captureKeys}, R"(
MyParam1
MyParam2
std::map
OICaptureKeys<std::map>
)");
}
