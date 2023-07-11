#pragma once

#include <functional>
#include <string_view>
#include <vector>

#include "oi/type_graph/Types.h"

namespace type_graph {
class Pass;
}  // namespace type_graph

void check(const std::vector<std::reference_wrapper<type_graph::Type>>& types,
           std::string_view expected,
           std::string_view comment);

void test(type_graph::Pass pass,
          std::string_view input,
          std::string_view expectedAfter);

void testNoChange(type_graph::Pass pass, std::string_view input);

void test(type_graph::Pass pass,
          std::vector<std::reference_wrapper<type_graph::Type>> rootTypes,
          std::string_view expectedBefore,
          std::string_view expectedAfter);

void test(type_graph::Pass pass,
          std::vector<std::reference_wrapper<type_graph::Type>> rootTypes,
          std::string_view expectedAfter);

type_graph::Container getVector(type_graph::NodeId id = 0);
type_graph::Container getMap(type_graph::NodeId id = 0);
type_graph::Container getList(type_graph::NodeId id = 0);
type_graph::Container getPair(type_graph::NodeId id = 0);
