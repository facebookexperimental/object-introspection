#include <gtest/gtest.h>

#include "oi/type_graph/NameGen.h"
#include "oi/type_graph/Types.h"
#include "test/type_graph_utils.h"

using namespace type_graph;

TEST(NameGenTest, ClassParams) {
  auto myparam1 = Class{0, Class::Kind::Struct, "MyParam", 13};
  auto myparam2 = Class{1, Class::Kind::Struct, "MyParam", 13};
  auto myclass = Class{2, Class::Kind::Struct, "MyClass<MyParam, MyParam>", 13};
  myclass.templateParams.push_back(myparam1);
  myclass.templateParams.push_back(myparam2);

  NameGen nameGen;
  nameGen.generateNames({myclass});

  EXPECT_EQ(myclass.name(), "MyClass_0");
  EXPECT_EQ(myparam1.name(), "MyParam_1");
  EXPECT_EQ(myparam2.name(), "MyParam_2");

  EXPECT_EQ(myclass.inputName(), "MyClass<MyParam, MyParam>");
}

TEST(NameGenTest, ClassContainerParam) {
  auto myint = Primitive{Primitive::Kind::Int32};
  auto myparam = getVector();
  myparam.templateParams.push_back(myint);

  auto myclass = Class{0, Class::Kind::Struct, "MyClass", 13};
  myclass.templateParams.push_back(myparam);

  NameGen nameGen;
  nameGen.generateNames({myclass});

  EXPECT_EQ(myclass.name(), "MyClass_0");
  EXPECT_EQ(myparam.name(), "std::vector<int32_t>");

  EXPECT_EQ(myclass.inputName(), "MyClass");
}

TEST(NameGenTest, ClassParents) {
  auto myparent1 = Class{0, Class::Kind::Struct, "MyParent", 13};
  auto myparent2 = Class{1, Class::Kind::Struct, "MyParent", 13};
  auto myclass = Class{2, Class::Kind::Struct, "MyClass", 13};
  myclass.parents.push_back(Parent{myparent1, 0});
  myclass.parents.push_back(Parent{myparent2, 0});

  NameGen nameGen;
  nameGen.generateNames({myclass});

  EXPECT_EQ(myclass.name(), "MyClass_0");
  EXPECT_EQ(myparent1.name(), "MyParent_1");
  EXPECT_EQ(myparent2.name(), "MyParent_2");

  EXPECT_EQ(myclass.inputName(), "MyClass");
  EXPECT_EQ(myparent1.inputName(), "MyParent");
  EXPECT_EQ(myparent2.inputName(), "MyParent");
}

TEST(NameGenTest, ClassMembers) {
  auto mymember1 = Class{0, Class::Kind::Struct, "MyMember", 13};
  auto mymember2 = Class{1, Class::Kind::Struct, "MyMember", 13};
  auto myclass = Class{2, Class::Kind::Struct, "MyClass", 13};

  // A class may end up with members sharing a name after flattening
  myclass.members.push_back(Member{mymember1, "mem", 0});
  myclass.members.push_back(Member{mymember2, "mem", 0});

  NameGen nameGen;
  nameGen.generateNames({myclass});

  EXPECT_EQ(myclass.name(), "MyClass_0");
  EXPECT_EQ(myclass.members[0].name, "mem_0");
  EXPECT_EQ(myclass.members[1].name, "mem_1");
  EXPECT_EQ(mymember1.name(), "MyMember_1");
  EXPECT_EQ(mymember2.name(), "MyMember_2");

  EXPECT_EQ(myclass.inputName(), "MyClass");
  EXPECT_EQ(myclass.members[0].inputName, "mem");
  EXPECT_EQ(myclass.members[1].inputName, "mem");
  EXPECT_EQ(mymember1.inputName(), "MyMember");
  EXPECT_EQ(mymember2.inputName(), "MyMember");
}

TEST(NameGenTest, ClassMemberInvalidChar) {
  auto myclass = Class{2, Class::Kind::Struct, "MyClass", 13};

  auto myint = Primitive{Primitive::Kind::Int32};
  myclass.members.push_back(Member{myint, "mem.Nope", 0});

  NameGen nameGen;
  nameGen.generateNames({myclass});

  EXPECT_EQ(myclass.name(), "MyClass_0");
  EXPECT_EQ(myclass.members[0].name, "mem$Nope_0");

  EXPECT_EQ(myclass.inputName(), "MyClass");
  EXPECT_EQ(myclass.members[0].inputName, "mem.Nope");
}

TEST(NameGenTest, ClassChildren) {
  auto mychild1 = Class{0, Class::Kind::Struct, "MyChild", 13};
  auto mychild2 = Class{1, Class::Kind::Struct, "MyChild", 13};
  auto myclass = Class{2, Class::Kind::Struct, "MyClass", 13};
  myclass.children.push_back(mychild1);
  myclass.children.push_back(mychild2);

  NameGen nameGen;
  nameGen.generateNames({myclass});

  EXPECT_EQ(myclass.name(), "MyClass_0");
  EXPECT_EQ(mychild1.name(), "MyChild_1");
  EXPECT_EQ(mychild2.name(), "MyChild_2");

  EXPECT_EQ(myclass.inputName(), "MyClass");
  EXPECT_EQ(mychild1.inputName(), "MyChild");
  EXPECT_EQ(mychild2.inputName(), "MyChild");
}

TEST(NameGenTest, ContainerParams) {
  auto myparam1 = Class{0, Class::Kind::Struct, "MyParam", 13};
  auto myparam2 = Class{1, Class::Kind::Struct, "MyParam", 13};
  auto mycontainer = getVector();
  mycontainer.templateParams.push_back(myparam1);
  mycontainer.templateParams.push_back(myparam2);

  NameGen nameGen;
  nameGen.generateNames({mycontainer});

  EXPECT_EQ(myparam1.name(), "MyParam_0");
  EXPECT_EQ(myparam2.name(), "MyParam_1");
  EXPECT_EQ(mycontainer.name(), "std::vector<MyParam_0, MyParam_1>");

  EXPECT_EQ(myparam1.inputName(), "MyParam");
  EXPECT_EQ(myparam2.inputName(), "MyParam");
  EXPECT_EQ(mycontainer.inputName(), "std::vector<MyParam, MyParam>");
}

TEST(NameGenTest, ContainerParamsDuplicates) {
  auto myparam = Class{0, Class::Kind::Struct, "MyParam", 13};
  auto mycontainer = getVector();
  mycontainer.templateParams.push_back(myparam);
  mycontainer.templateParams.push_back(myparam);

  NameGen nameGen;
  nameGen.generateNames({mycontainer});

  EXPECT_EQ(myparam.name(), "MyParam_0");
  EXPECT_EQ(mycontainer.name(), "std::vector<MyParam_0, MyParam_0>");

  EXPECT_EQ(myparam.inputName(), "MyParam");
  EXPECT_EQ(mycontainer.inputName(), "std::vector<MyParam, MyParam>");
}

TEST(NameGenTest, ContainerParamsDuplicatesDeep) {
  auto myparam = Class{0, Class::Kind::Struct, "MyParam", 13};

  auto mycontainer1 = getVector();
  mycontainer1.templateParams.push_back(myparam);

  auto mycontainer2 = getVector();
  mycontainer2.templateParams.push_back(myparam);
  mycontainer2.templateParams.push_back(mycontainer1);

  NameGen nameGen;
  nameGen.generateNames({mycontainer2});

  EXPECT_EQ(myparam.name(), "MyParam_0");
  EXPECT_EQ(mycontainer1.name(), "std::vector<MyParam_0>");
  EXPECT_EQ(mycontainer2.name(),
            "std::vector<MyParam_0, std::vector<MyParam_0>>");

  EXPECT_EQ(myparam.inputName(), "MyParam");
  EXPECT_EQ(mycontainer1.inputName(), "std::vector<MyParam>");
  EXPECT_EQ(mycontainer2.inputName(),
            "std::vector<MyParam, std::vector<MyParam>>");
}

TEST(NameGenTest, ContainerParamsDuplicatesAcrossContainers) {
  auto myparam1 = Class{0, Class::Kind::Struct, "MyParam", 13};
  auto myparam2 = Class{1, Class::Kind::Struct, "MyParam", 13};
  auto myparam3 = Class{2, Class::Kind::Struct, "MyParam", 13};

  auto mycontainer1 = getVector();
  mycontainer1.templateParams.push_back(myparam1);
  mycontainer1.templateParams.push_back(myparam2);

  auto mycontainer2 = getVector();
  mycontainer2.templateParams.push_back(myparam2);
  mycontainer2.templateParams.push_back(myparam3);

  NameGen nameGen;
  nameGen.generateNames({mycontainer1, mycontainer2});

  EXPECT_EQ(myparam1.name(), "MyParam_0");
  EXPECT_EQ(myparam2.name(), "MyParam_1");
  EXPECT_EQ(myparam3.name(), "MyParam_2");
  EXPECT_EQ(mycontainer1.name(), "std::vector<MyParam_0, MyParam_1>");
  EXPECT_EQ(mycontainer2.name(), "std::vector<MyParam_1, MyParam_2>");

  EXPECT_EQ(myparam1.inputName(), "MyParam");
  EXPECT_EQ(myparam2.inputName(), "MyParam");
  EXPECT_EQ(myparam3.inputName(), "MyParam");
  EXPECT_EQ(mycontainer1.inputName(), "std::vector<MyParam, MyParam>");
  EXPECT_EQ(mycontainer2.inputName(), "std::vector<MyParam, MyParam>");
}

TEST(NameGenTest, ContainerParamsConst) {
  auto myparam1 = Class{0, Class::Kind::Struct, "MyConstParam", 13};
  auto myparam2 = Class{1, Class::Kind::Struct, "MyParam", 13};
  auto ptrParam = Class{2, Class::Kind::Struct, "PtrParam", 13};
  auto myparam3 = Pointer{3, ptrParam};

  auto mycontainer = getVector();
  mycontainer.templateParams.push_back(
      TemplateParam{myparam1, {Qualifier::Const}});
  mycontainer.templateParams.push_back(TemplateParam{myparam2});
  mycontainer.templateParams.push_back(
      TemplateParam{myparam3, {Qualifier::Const}});

  NameGen nameGen;
  nameGen.generateNames({mycontainer});

  EXPECT_EQ(myparam1.name(), "MyConstParam_0");
  EXPECT_EQ(myparam2.name(), "MyParam_1");
  EXPECT_EQ(myparam3.name(), "PtrParam_2*");
  EXPECT_EQ(mycontainer.name(),
            "std::vector<MyConstParam_0 const, MyParam_1, PtrParam_2* const>");

  EXPECT_EQ(myparam1.inputName(), "MyConstParam");
  EXPECT_EQ(myparam2.inputName(), "MyParam");
  EXPECT_EQ(myparam3.inputName(), "PtrParam*");
  EXPECT_EQ(mycontainer.inputName(),
            "std::vector<MyConstParam const, MyParam, PtrParam* const>");
}

TEST(NameGenTest, ContainerNoParams) {
  auto mycontainer = getVector();

  NameGen nameGen;
  nameGen.generateNames({mycontainer});

  EXPECT_EQ(mycontainer.name(), "std::vector");
  EXPECT_EQ(mycontainer.inputName(), "std::vector");
}

TEST(NameGenTest, ContainerParamsValue) {
  auto myint = Primitive{Primitive::Kind::Int32};
  auto myenum = Enum{"MyEnum", 4};

  auto mycontainer = getVector();
  mycontainer.templateParams.push_back(TemplateParam{myint, "123"});
  mycontainer.templateParams.push_back(
      TemplateParam{myenum, "MyEnum::OptionC"});

  NameGen nameGen;
  nameGen.generateNames({mycontainer});

  EXPECT_EQ(myint.name(), "int32_t");
  EXPECT_EQ(myenum.name(), "MyEnum_0");
  EXPECT_EQ(mycontainer.name(), "std::vector<123, MyEnum::OptionC>");

  EXPECT_EQ(myint.inputName(), "int32_t");
  EXPECT_EQ(myenum.inputName(), "MyEnum");
  EXPECT_EQ(mycontainer.inputName(), "std::vector<123, MyEnum::OptionC>");
}

TEST(NameGenTest, Enum) {
  auto myenum0 = Enum{"MyEnum", 4};
  auto myenum1 = Enum{"MyEnum", 4};

  NameGen nameGen;
  nameGen.generateNames({myenum0, myenum1});

  EXPECT_EQ(myenum0.name(), "MyEnum_0");
  EXPECT_EQ(myenum1.name(), "MyEnum_1");

  EXPECT_EQ(myenum0.inputName(), "MyEnum");
  EXPECT_EQ(myenum1.inputName(), "MyEnum");
}

TEST(NameGenTest, Array) {
  auto myparam1 = Class{0, Class::Kind::Struct, "MyParam", 13};
  auto myparam2 = Class{1, Class::Kind::Struct, "MyParam", 13};

  auto mycontainer = getVector();
  mycontainer.templateParams.push_back(myparam1);
  mycontainer.templateParams.push_back(myparam2);

  auto myarray = Array{2, mycontainer, 5};

  NameGen nameGen;
  nameGen.generateNames({myarray});

  EXPECT_EQ(myparam1.name(), "MyParam_0");
  EXPECT_EQ(myparam2.name(), "MyParam_1");
  EXPECT_EQ(mycontainer.name(), "std::vector<MyParam_0, MyParam_1>");
  EXPECT_EQ(myarray.name(), "OIArray<std::vector<MyParam_0, MyParam_1>, 5>");

  EXPECT_EQ(myparam1.inputName(), "MyParam");
  EXPECT_EQ(myparam2.inputName(), "MyParam");
  EXPECT_EQ(mycontainer.inputName(), "std::vector<MyParam, MyParam>");
  EXPECT_EQ(myarray.inputName(), "std::vector<MyParam, MyParam>[5]");
}

TEST(NameGenTest, Typedef) {
  auto myparam1 = Class{0, Class::Kind::Struct, "MyParam", 13};
  auto myparam2 = Class{1, Class::Kind::Struct, "MyParam", 13};

  auto mycontainer = getVector();
  mycontainer.templateParams.push_back(myparam1);
  mycontainer.templateParams.push_back(myparam2);

  auto mytypedef = Typedef{2, "MyTypedef", mycontainer};

  NameGen nameGen;
  nameGen.generateNames({mytypedef});

  EXPECT_EQ(myparam1.name(), "MyParam_1");
  EXPECT_EQ(myparam2.name(), "MyParam_2");
  EXPECT_EQ(mycontainer.name(), "std::vector<MyParam_1, MyParam_2>");
  EXPECT_EQ(mytypedef.name(), "MyTypedef_0");

  EXPECT_EQ(myparam1.inputName(), "MyParam");
  EXPECT_EQ(myparam2.inputName(), "MyParam");
  EXPECT_EQ(mycontainer.inputName(), "std::vector<MyParam, MyParam>");
  EXPECT_EQ(mytypedef.inputName(), "MyTypedef");
}

TEST(NameGenTest, TypedefAliasTemplate) {
  auto myint = Primitive{Primitive::Kind::Int32};
  auto mytypedef = Typedef{0, "MyTypedef<ParamA, ParamB>", myint};

  NameGen nameGen;
  nameGen.generateNames({mytypedef});

  EXPECT_EQ(mytypedef.name(), "MyTypedef_0");
  EXPECT_EQ(mytypedef.inputName(), "MyTypedef<ParamA, ParamB>");
}

TEST(NameGenTest, Pointer) {
  auto myparam1 = Class{0, Class::Kind::Struct, "MyParam", 13};
  auto myparam2 = Class{1, Class::Kind::Struct, "MyParam", 13};

  auto mycontainer = getVector();
  mycontainer.templateParams.push_back(myparam1);
  mycontainer.templateParams.push_back(myparam2);

  auto mypointer = Pointer{2, mycontainer};

  NameGen nameGen;
  nameGen.generateNames({mypointer});

  EXPECT_EQ(myparam1.name(), "MyParam_0");
  EXPECT_EQ(myparam2.name(), "MyParam_1");
  EXPECT_EQ(mycontainer.name(), "std::vector<MyParam_0, MyParam_1>");
  EXPECT_EQ(mypointer.name(), "std::vector<MyParam_0, MyParam_1>*");

  EXPECT_EQ(myparam1.inputName(), "MyParam");
  EXPECT_EQ(myparam2.inputName(), "MyParam");
  EXPECT_EQ(mycontainer.inputName(), "std::vector<MyParam, MyParam>");
  EXPECT_EQ(mypointer.inputName(), "std::vector<MyParam, MyParam>*");
}

TEST(NameGenTest, Dummy) {
  auto dummy = Dummy{0, 12, 34, "InputName"};

  NameGen nameGen;
  nameGen.generateNames({dummy});

  EXPECT_EQ(dummy.name(), "DummySizedOperator<12, 34, 0>");
  EXPECT_EQ(dummy.inputName(), "InputName");
}

TEST(NameGenTest, DummyAllocator) {
  auto myparam1 = Class{0, Class::Kind::Struct, "MyParam", 13};
  auto myparam2 = Class{1, Class::Kind::Struct, "MyParam", 13};

  auto mycontainer = getVector();
  mycontainer.templateParams.push_back(myparam1);
  mycontainer.templateParams.push_back(myparam2);

  auto myalloc = DummyAllocator{2, mycontainer, 12, 34, "BigAllocator"};

  NameGen nameGen;
  nameGen.generateNames({myalloc});

  EXPECT_EQ(myparam1.name(), "MyParam_0");
  EXPECT_EQ(myparam2.name(), "MyParam_1");
  EXPECT_EQ(mycontainer.name(), "std::vector<MyParam_0, MyParam_1>");
  EXPECT_EQ(myalloc.name(),
            "DummyAllocator<std::vector<MyParam_0, MyParam_1>, 12, 34, 2>");

  EXPECT_EQ(myparam1.inputName(), "MyParam");
  EXPECT_EQ(myparam2.inputName(), "MyParam");
  EXPECT_EQ(mycontainer.inputName(), "std::vector<MyParam, MyParam>");
  EXPECT_EQ(myalloc.inputName(), "BigAllocator");
}

TEST(NameGenTest, CaptureKeys) {
  auto myparam1 = Class{0, Class::Kind::Struct, "MyParam", 13};
  auto myparam2 = Class{1, Class::Kind::Struct, "MyParam", 13};

  auto mycontainer = getVector();
  mycontainer.templateParams.push_back(myparam1);
  mycontainer.templateParams.push_back(myparam2);

  auto captureKeys = CaptureKeys{mycontainer, mycontainer.containerInfo_};

  NameGen nameGen;
  nameGen.generateNames({captureKeys});

  EXPECT_EQ(myparam1.name(), "MyParam_0");
  EXPECT_EQ(myparam2.name(), "MyParam_1");
  EXPECT_EQ(mycontainer.name(), "std::vector<MyParam_0, MyParam_1>");
  EXPECT_EQ(captureKeys.name(),
            "OICaptureKeys<std::vector<MyParam_0, MyParam_1>>");

  EXPECT_EQ(myparam1.inputName(), "MyParam");
  EXPECT_EQ(myparam2.inputName(), "MyParam");
  EXPECT_EQ(mycontainer.inputName(), "std::vector<MyParam, MyParam>");
  EXPECT_EQ(captureKeys.inputName(), "std::vector<MyParam, MyParam>");
}

TEST(NameGenTest, Cycle) {
  auto classA = Class{0, Class::Kind::Class, "ClassA", 69};
  auto classB = Class{1, Class::Kind::Class, "ClassB", 69};
  auto ptrA = Pointer{2, classA};
  classA.members.push_back(Member{classB, "b", 0});
  classB.members.push_back(Member{ptrA, "a", 0});

  NameGen nameGen;
  nameGen.generateNames({classA});

  EXPECT_EQ(classA.name(), "ClassA_0");
  EXPECT_EQ(classB.name(), "ClassB_1");

  EXPECT_EQ(classA.inputName(), "ClassA");
  EXPECT_EQ(classB.inputName(), "ClassB");
}

TEST(NameGenTest, ContainerCycle) {
  auto container = getVector();
  auto myclass = Class{0, Class::Kind::Class, "MyClass", 69};
  myclass.members.push_back(Member{container, "c", 0});
  container.templateParams.push_back(TemplateParam{myclass});

  NameGen nameGen;
  nameGen.generateNames({myclass});

  EXPECT_EQ(myclass.name(), "MyClass_0");
  EXPECT_EQ(container.name(), "std::vector<MyClass_0>");

  EXPECT_EQ(myclass.inputName(), "MyClass");
  EXPECT_EQ(container.inputName(), "std::vector<MyClass>");
}

TEST(NameGenTest, AnonymousTypes) {
  auto myint = Primitive{Primitive::Kind::Int32};

  auto myclass = Class{0, Class::Kind::Class, "", 69};
  auto myenum = Enum{"", 4};
  auto mytypedef = Typedef{1, "", myint};

  NameGen nameGen;
  nameGen.generateNames({myclass, myenum, mytypedef});

  EXPECT_EQ(myclass.name(), "__oi_anon_0");
  EXPECT_EQ(myenum.name(), "__oi_anon_1");
  EXPECT_EQ(mytypedef.name(), "__oi_anon_2");

  EXPECT_EQ(myclass.inputName(), "__oi_anon_0");
  EXPECT_EQ(myenum.inputName(), "__oi_anon_1");
  EXPECT_EQ(mytypedef.inputName(), "");
}

TEST(NameGenTest, AnonymousMembers) {
  auto myint = Primitive{Primitive::Kind::Int32};

  auto myclass = Class{0, Class::Kind::Class, "C", 69};
  auto myunion1 = Class{1, Class::Kind::Union, "", 4};
  auto myunion2 = Class{2, Class::Kind::Union, "", 4};

  myclass.members.push_back(Member{myunion1, "", 0});
  myclass.members.push_back(Member{myunion2, "", 4});

  NameGen nameGen;
  nameGen.generateNames({myclass, myunion1, myunion2});

  EXPECT_EQ(myclass.name(), "C_0");
  EXPECT_EQ(myunion1.name(), "__oi_anon_1");
  EXPECT_EQ(myunion2.name(), "__oi_anon_2");
  EXPECT_EQ(myclass.members[0].name, "__oi_anon_0");
  EXPECT_EQ(myclass.members[1].name, "__oi_anon_1");

  EXPECT_EQ(myclass.inputName(), "C");
  EXPECT_EQ(myunion1.inputName(), "__oi_anon_1");
  EXPECT_EQ(myunion2.inputName(), "__oi_anon_2");
  EXPECT_EQ(myclass.members[0].inputName, "__oi_anon_0");
  EXPECT_EQ(myclass.members[1].inputName, "__oi_anon_1");
}

TEST(NameGenTest, IncompleteTypes) {
  auto myincompletevector = Incomplete{0, "std::vector<int>"};

  auto myint = Primitive{Primitive::Kind::Int32};
  auto myincompleteint = Incomplete{1, myint};

  NameGen nameGen;
  nameGen.generateNames({myincompletevector, myincompleteint});

  EXPECT_EQ(myincompletevector.name(), "Incomplete<struct std__vector_int_>");
  EXPECT_EQ(myincompleteint.name(), "Incomplete<struct int32_t>");
}
