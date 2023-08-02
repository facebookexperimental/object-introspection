#pragma once

#include <functional>
#include <string_view>
#include <vector>

#include "oi/type_graph/Types.h"

namespace oi::detail::type_graph {
class Pass;
class TypeGraph;
}  // namespace oi::detail::type_graph

using namespace oi::detail;

void check(const type_graph::TypeGraph& typeGraph,
           std::string_view expected,
           std::string_view comment);

void test(type_graph::Pass pass,
          std::string_view input,
          std::string_view expectedAfter);

void testNoChange(type_graph::Pass pass, std::string_view input);

type_graph::Container getVector(type_graph::NodeId id = 0);
type_graph::Container getMap(type_graph::NodeId id = 0);
type_graph::Container getList(type_graph::NodeId id = 0);
type_graph::Container getPair(type_graph::NodeId id = 0);
