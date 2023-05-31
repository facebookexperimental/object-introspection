#pragma once

#include <functional>
#include <string_view>
#include <vector>

namespace type_graph {
class Container;
class Pass;
class Type;
}  // namespace type_graph

void test(type_graph::Pass pass,
          std::vector<std::reference_wrapper<type_graph::Type>> rootTypes,
          std::string_view expectedBefore,
          std::string_view expectedAfter);

void test(type_graph::Pass pass,
          std::vector<std::reference_wrapper<type_graph::Type>> rootTypes,
          std::string_view expectedAfter);

type_graph::Container getVector();
