#pragma once

#include <functional>
#include <string_view>
#include <vector>

namespace type_graph {
class Container;
class Pass;
class Type;
}  // namespace type_graph

void check(const std::vector<std::reference_wrapper<type_graph::Type>>& types,
           std::string_view expected,
           std::string_view comment);

void test(type_graph::Pass pass,
          std::vector<std::reference_wrapper<type_graph::Type>> rootTypes,
          std::string_view expectedBefore,
          std::string_view expectedAfter);

void test(type_graph::Pass pass,
          std::vector<std::reference_wrapper<type_graph::Type>> rootTypes,
          std::string_view expectedAfter);

type_graph::Container getVector();
type_graph::Container getMap();
type_graph::Container getList();
