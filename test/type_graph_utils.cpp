#include "test/type_graph_utils.h"

#include <gtest/gtest.h>

#include "oi/ContainerInfo.h"
#include "oi/type_graph/PassManager.h"
#include "oi/type_graph/Printer.h"
#include "oi/type_graph/TypeGraph.h"

using type_graph::Container;
using type_graph::Pass;
using type_graph::Type;
using type_graph::TypeGraph;

template <typename T>
using ref = std::reference_wrapper<T>;

void check(const std::vector<ref<Type>>& types,
           std::string_view expected,
           std::string_view comment) {
  std::stringstream out;
  type_graph::Printer printer(out);

  for (const auto& type : types) {
    printer.print(type);
  }

  expected.remove_prefix(1);  // Remove initial '\n'
  ASSERT_EQ(expected, out.str()) << "Test failure " << comment;
}

void test(type_graph::Pass pass,
          std::vector<ref<Type>> rootTypes,
          std::string_view expectedBefore,
          std::string_view expectedAfter) {
  check(rootTypes, expectedBefore, "before running pass");

  TypeGraph typeGraph;
  for (const auto& type : rootTypes) {
    typeGraph.addRoot(type);
  }

  pass.run(typeGraph);

  check(rootTypes, expectedAfter, "after running pass");
}

void test(type_graph::Pass pass,
          std::vector<ref<Type>> rootTypes,
          std::string_view expectedAfter) {
  TypeGraph typeGraph;
  for (const auto& type : rootTypes) {
    typeGraph.addRoot(type);
  }

  pass.run(typeGraph);

  check(rootTypes, expectedAfter, "after");
}

Container getVector() {
  static ContainerInfo info{"std::vector", SEQ_TYPE, "vector"};
  info.stubTemplateParams = {1};
  return Container{info, 24};
}
