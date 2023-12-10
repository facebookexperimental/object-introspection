#pragma once

#include <gmock/gmock.h>
#include <lldb/API/SBType.h>
#include <lldb/API/SBDebugger.h>

#include "oi/type_graph/LLDBParser.h"

namespace oi::detail {
class SymbolService;
}
namespace oi::detail::type_graph {
class TypeGraph;
}

using namespace oi::detail;

class LLDBParserTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite();
  static void TearDownTestSuite();

  static type_graph::LLDBParser getLLDBParser(
      type_graph::TypeGraph& typeGraph, type_graph::LLDBParserOptions options);
  lldb::SBType getLLDBRoot(std::string_view function);

  virtual std::string run(std::string_view function,
                          type_graph::LLDBParserOptions options);
  void test(std::string_view function,
            std::string_view expected,
            type_graph::LLDBParserOptions options);
  void test(std::string_view function, std::string_view expected);
  void testGlob(std::string_view function,
                std::string_view expected,
                type_graph::LLDBParserOptions options = {});
  void testMultiCompiler(std::string_view function,
                         std::string_view expectedClang,
                         std::string_view expectedGcc,
                         type_graph::LLDBParserOptions options = {});
  void testMultiCompilerGlob(std::string_view function,
                             std::string_view expectedClang,
                             std::string_view expectedGcc,
                             type_graph::LLDBParserOptions options = {});

  static lldb::SBDebugger debugger_;
  static lldb::SBTarget target_;
};
