#pragma once

#include <functional>
#include <unordered_map>

#include "oi/type_graph/Types.h"

namespace oi::detail::type_graph {
class TypeGraph;
}  // namespace oi::detail::type_graph

using namespace oi::detail::type_graph;

/*
 * TypeGraphParser
 *
 * Parses a textual type graph, as emitted by Printer.
 */
class TypeGraphParser {
 public:
  TypeGraphParser(TypeGraph& typeGraph) : typeGraph_(typeGraph) {
  }

  void parse(std::string_view input);

 private:
  TypeGraph& typeGraph_;
  std::unordered_map<NodeId, std::reference_wrapper<Type>> nodesById_;

  Type& parseType(std::string_view& input, size_t rootIndent);
  template <typename T>
  void parseParams(T& c, std::string_view& input, size_t rootIndent);
  void parseParents(Class& c, std::string_view& input, size_t rootIndent);
  void parseMembers(Class& c, std::string_view& input, size_t rootIndent);
  void parseFunctions(Class& c, std::string_view& input, size_t rootIndent);
  void parseChildren(Class& c, std::string_view& input, size_t rootIndent);
  void parseUnderlying(Container& c,
                       std::string_view& input,
                       size_t rootIndent);
};

class TypeGraphParserError : public std::runtime_error {
 public:
  TypeGraphParserError(const std::string& msg) : std::runtime_error{msg} {
  }
};
