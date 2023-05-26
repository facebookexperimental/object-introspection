#include <gtest/gtest.h>

#include <boost/algorithm/string.hpp>

#include "oi/type_graph/TopoSorter.h"
#include "oi/type_graph/Types.h"

using namespace type_graph;

Container getVector();  // TODO put in a header

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
  auto myenum = std::make_unique<Enum>("MyEnum", 4);
  test({*myenum}, "MyEnum");
}

TEST(TopoSorterTest, UnrelatedTypes) {
  auto mystruct = std::make_unique<Class>(Class::Kind::Struct, "MyStruct", 13);
  auto myenum = std::make_unique<Enum>("MyEnum", 4);
  auto myclass = std::make_unique<Class>(Class::Kind::Class, "MyClass", 69);

  // Try the same input in a few different orders and ensure they output order
  // matches the input order
  test({*mystruct, *myenum, *myclass}, R"(
MyStruct
MyEnum
MyClass
)");
  test({*myenum, *mystruct, *myclass}, R"(
MyEnum
MyStruct
MyClass
)");
  test({*myclass, *myenum, *mystruct}, R"(
MyClass
MyEnum
MyStruct
)");
}

TEST(TopoSorterTest, ClassMembers) {
  auto mystruct = std::make_unique<Class>(Class::Kind::Struct, "MyStruct", 13);
  auto myenum = std::make_unique<Enum>("MyEnum", 4);
  auto myclass = std::make_unique<Class>(Class::Kind::Class, "MyClass", 69);
  myclass->members.push_back(Member(mystruct.get(), "n", 0));
  myclass->members.push_back(Member(myenum.get(), "e", 4));

  test({*myclass}, R"(
MyStruct
MyEnum
MyClass
)");
}

TEST(TopoSorterTest, Parents) {
  auto myparent = std::make_unique<Class>(Class::Kind::Struct, "MyParent", 13);
  auto myclass = std::make_unique<Class>(Class::Kind::Class, "MyClass", 69);
  myclass->parents.push_back(Parent(myparent.get(), 0));

  test({*myclass}, R"(
MyParent
MyClass
)");
}

TEST(TopoSorterTest, TemplateParams) {
  auto myparam = std::make_unique<Class>(Class::Kind::Struct, "MyParam", 13);
  auto myclass = std::make_unique<Class>(Class::Kind::Class, "MyClass", 69);
  myclass->templateParams.push_back(TemplateParam(myparam.get()));

  test({*myclass}, R"(
MyParam
MyClass
)");
}

TEST(TopoSorterTest, Children) {
  auto mymember = std::make_unique<Class>(Class::Kind::Struct, "MyMember", 13);
  auto mychild = std::make_unique<Class>(Class::Kind::Struct, "MyChild", 13);
  mychild->members.push_back(Member(mymember.get(), "mymember", 0));

  auto myclass = std::make_unique<Class>(Class::Kind::Class, "MyClass", 69);
  mychild->parents.push_back(Parent(myclass.get(), 0));
  myclass->children.push_back(*mychild);

  std::vector<std::vector<ref<Type>>> inputs = {
      {*myclass},
      {*mychild},
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
  auto myparent = std::make_unique<Class>(Class::Kind::Class, "MyParent", 69);
  auto classA = std::make_unique<Class>(Class::Kind::Struct, "ClassA", 5);
  auto mychild = std::make_unique<Class>(Class::Kind::Struct, "MyChild", 13);

  mychild->parents.push_back(Parent(myparent.get(), 0));
  myparent->children.push_back(*mychild);

  mychild->members.push_back(Member(classA.get(), "a", 0));
  classA->members.push_back(Member(myparent.get(), "p", 0));

  std::vector<std::vector<ref<Type>>> inputs = {
      {*myparent},
      {*classA},
      {*mychild},
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
  auto myparam = std::make_unique<Class>(Class::Kind::Struct, "MyParam", 13);
  auto mycontainer = getVector();
  mycontainer.templateParams.push_back((myparam.get()));

  test({mycontainer}, "MyParam\nstd::vector");
}

TEST(TopoSorterTest, Arrays) {
  auto myclass = std::make_unique<Class>(Class::Kind::Class, "MyClass", 69);
  auto myarray = std::make_unique<Array>(myclass.get(), 10);

  test({*myarray}, "MyClass\n");
}

TEST(TopoSorterTest, Typedef) {
  auto classA = std::make_unique<Class>(Class::Kind::Class, "ClassA", 8);
  auto aliasA = std::make_unique<Typedef>("aliasA", classA.get());

  test({*aliasA}, R"(
ClassA
aliasA
)");
}

TEST(TopoSorterTest, Pointers) {
  // Pointers do not require pointee types to be defined first
  auto myclass = std::make_unique<Class>(Class::Kind::Class, "MyClass", 69);
  auto mypointer = std::make_unique<Pointer>(myclass.get());

  test({*mypointer}, "MyClass");
}

TEST(TopoSorterTest, PointerCycle) {
  auto classA = std::make_unique<Class>(Class::Kind::Class, "ClassA", 69);
  auto classB = std::make_unique<Class>(Class::Kind::Class, "ClassB", 69);
  auto ptrA = std::make_unique<Pointer>(classA.get());
  classA->members.push_back(Member(classB.get(), "b", 0));
  classB->members.push_back(Member(ptrA.get(), "a", 0));

  std::vector<std::vector<ref<Type>>> inputs = {
      {*classA},
      {*classB},
      {*ptrA},
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

TEST(TopoSorterTest, TwoDeep) {
  auto myunion = std::make_unique<Class>(Class::Kind::Union, "MyUnion", 7);
  auto mystruct = std::make_unique<Class>(Class::Kind::Struct, "MyStruct", 13);
  auto myclass = std::make_unique<Class>(Class::Kind::Class, "MyClass", 69);
  myclass->members.push_back(Member(mystruct.get(), "mystruct", 0));
  mystruct->members.push_back(Member(myunion.get(), "myunion", 0));

  test({*myclass}, R"(
MyUnion
MyStruct
MyClass
)");
}

TEST(TopoSorterTest, MultiplePaths) {
  auto myunion = std::make_unique<Class>(Class::Kind::Union, "MyUnion", 7);
  auto mystruct = std::make_unique<Class>(Class::Kind::Struct, "MyStruct", 13);
  auto myclass = std::make_unique<Class>(Class::Kind::Class, "MyClass", 69);
  myclass->members.push_back(Member(mystruct.get(), "mystruct", 0));
  myclass->members.push_back(Member(myunion.get(), "myunion1", 0));
  mystruct->members.push_back(Member(myunion.get(), "myunion2", 0));

  test({*myclass}, R"(
MyUnion
MyStruct
MyClass
)");
}
