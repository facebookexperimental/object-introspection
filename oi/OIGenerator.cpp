/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "oi/OIGenerator.h"

#include <glog/logging.h>

#include <boost/core/demangle.hpp>
#include <fstream>
#include <iostream>
#include <string_view>
#include <unordered_map>
#include <variant>

#include "oi/CodeGen.h"
#include "oi/Config.h"
#include "oi/DrgnUtils.h"
#include "oi/Headers.h"

namespace oi::detail {

std::unordered_map<std::string, std::string>
OIGenerator::oilStrongToWeakSymbolsMap(drgnplusplus::program& prog) {
  static constexpr std::string_view strongSymbolPrefix =
      "oi::IntrospectionResult oi::introspect<";
  static constexpr std::string_view weakSymbolPrefix =
      "oi::IntrospectionResult oi::introspectImpl<";

  std::unordered_map<std::string, std::pair<std::string, std::string>>
      templateArgsToSymbolsMap;

  auto symbols = prog.find_all_symbols();
  for (drgn_symbol* sym : *symbols) {
    auto symName = drgnplusplus::symbol::name(sym);
    if (symName == nullptr || *symName == '\0')
      continue;
    auto demangled = boost::core::demangle(symName);

    if (demangled.starts_with(strongSymbolPrefix)) {
      auto& matchedSyms = templateArgsToSymbolsMap[demangled.substr(
          strongSymbolPrefix.length())];
      if (!matchedSyms.first.empty()) {
        LOG(WARNING) << "non-unique symbols found: `" << matchedSyms.first
                     << "` and `" << symName << '`';
      }
      matchedSyms.first = symName;
    } else if (demangled.starts_with(weakSymbolPrefix)) {
      auto& matchedSyms =
          templateArgsToSymbolsMap[demangled.substr(weakSymbolPrefix.length())];
      if (!matchedSyms.second.empty()) {
        LOG(WARNING) << "non-unique symbols found: `" << matchedSyms.second
                     << "` and `" << symName << "`";
      }
      matchedSyms.second = symName;
    }
  }

  std::unordered_map<std::string, std::string> strongToWeakSymbols;
  for (auto& [_, val] : templateArgsToSymbolsMap) {
    if (val.first.empty() || val.second.empty()) {
      continue;
    }
    strongToWeakSymbols[std::move(val.first)] = std::move(val.second);
  }

  return strongToWeakSymbols;
}

std::unordered_map<std::string, drgn_qualified_type>
OIGenerator::findOilTypesAndNames(drgnplusplus::program& prog) {
  auto strongToWeakSymbols = oilStrongToWeakSymbolsMap(prog);

  std::unordered_map<std::string, drgn_qualified_type> out;

  for (drgn_qualified_type& func : drgnplusplus::func_iterator(prog)) {
    std::string strongLinkageName;
    {
      const char* linkageNameCstr;
      if (auto err = drgnplusplus::error(
              drgn_type_linkage_name(func.type, &linkageNameCstr))) {
        // throw err;
        continue;
      }
      strongLinkageName = linkageNameCstr;
    }

    std::string weakLinkageName;
    if (auto search = strongToWeakSymbols.find(strongLinkageName);
        search != strongToWeakSymbols.end()) {
      weakLinkageName = search->second;
    } else {
      continue;  // not an oil strong symbol
    }

    // IntrospectionResult (*)(const T&)
    CHECK(drgn_type_has_parameters(func.type)) << "functions have parameters";
    CHECK(drgn_type_num_parameters(func.type) == 1)
        << "introspection func has one parameter";

    auto* params = drgn_type_parameters(func.type);
    drgn_qualified_type tType;
    if (auto err =
            drgnplusplus::error(drgn_parameter_type(&params[0], &tType))) {
      throw err;
    }

    if (drgn_type_has_name(tType.type)) {
      LOG(INFO) << "found OIL type: " << drgn_type_name(tType.type);
    } else {
      LOG(INFO) << "found OIL type: (no name)";
    }
    out.emplace(std::move(weakLinkageName), tType);
  }

  return out;
}

fs::path OIGenerator::generateForType(const OICodeGen::Config& generatorConfig,
                                      const OICompiler::Config& compilerConfig,
                                      const drgn_qualified_type& type,
                                      const std::string& linkageName,
                                      SymbolService& symbols) {
  CodeGen codegen{generatorConfig, symbols};

  std::string code;
  if (!codegen.codegenFromDrgn(type.type, linkageName, code)) {
    LOG(ERROR) << "codegen failed!";
    return {};
  }

  std::string sourcePath = sourceFileDumpPath;
  if (sourceFileDumpPath.empty()) {
    // This is the path Clang acts as if it has compiled from e.g. for debug
    // information. It does not need to exist.
    sourcePath = "oil_jit.cpp";
  } else {
    std::ofstream outputFile(sourcePath);
    outputFile << code;
  }

  OICompiler compiler{{}, compilerConfig};

  // TODO: Revert to outputPath and remove printing when typegraph is done.
  fs::path tmpObject = outputPath;
  tmpObject.replace_extension(
      "." + std::to_string(std::hash<std::string>{}(linkageName)) + ".o");

  if (!compiler.compile(code, sourcePath, tmpObject)) {
    return {};
  }
  return tmpObject;
}

int OIGenerator::generate(fs::path& primaryObject, SymbolService& symbols) {
  drgnplusplus::program prog;

  {
    std::array<const char*, 1> objectPaths = {{primaryObject.c_str()}};
    if (auto err = drgnplusplus::error(
            drgn_program_load_debug_info(prog.get(),
                                         std::data(objectPaths),
                                         std::size(objectPaths),
                                         false,
                                         false))) {
      LOG(ERROR) << "error loading debug info program: " << err;
      throw err;
    }
  }

  auto oilTypes = findOilTypesAndNames(prog);

  std::map<Feature, bool> featuresMap = {
      {Feature::TypeGraph, true},
      {Feature::TreeBuilderV2, true},
      {Feature::Library, true},
      {Feature::PackStructs, true},
      {Feature::PruneTypeGraph, true},
  };

  OICodeGen::Config generatorConfig{};
  OICompiler::Config compilerConfig{};
  compilerConfig.usePIC = pic;

  auto features = config::processConfigFiles(
      configFilePaths, featuresMap, compilerConfig, generatorConfig);
  if (!features) {
    LOG(ERROR) << "failed to process config file";
    return -1;
  }
  generatorConfig.features = *features;
  compilerConfig.features = *features;

  size_t failures = 0;
  for (const auto& [linkageName, type] : oilTypes) {
    if (auto obj = generateForType(
            generatorConfig, compilerConfig, type, linkageName, symbols);
        !obj.empty()) {
      std::cout << obj.string() << std::endl;
    } else {
      LOG(WARNING) << "failed to generate for symbol `" << linkageName
                   << "`. this is non-fatal but the call will not work.";
      failures++;
    }
  }

  size_t successes = oilTypes.size() - failures;
  LOG(INFO) << "object introspection generation complete. " << successes
            << " successes and " << failures << " failures.";

  if (failures > 0 || (failIfNothingGenerated && successes == 0)) {
    return -1;
  }
  return 0;
}

}  // namespace oi::detail
