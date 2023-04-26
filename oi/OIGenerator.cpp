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
#include <unordered_map>
#include <variant>

#include "oi/DrgnUtils.h"
#include "oi/OIUtils.h"

namespace ObjectIntrospection {

std::unordered_map<std::string, std::string>
OIGenerator::oilStrongToWeakSymbolsMap(drgnplusplus::program& prog) {
  static constexpr std::string_view strongSymbolPrefix =
      "int ObjectIntrospection::getObjectSize<";
  static constexpr std::string_view weakSymbolPrefix =
      "int ObjectIntrospection::getObjectSizeImpl<";

  std::unordered_map<std::string, std::pair<std::string, std::string>>
      templateArgsToSymbolsMap;

  auto symbols = prog.find_symbols_by_name(nullptr);
  for (drgn_symbol* sym : *symbols) {
    auto symName = drgnplusplus::symbol::name(sym);
    auto demangled = boost::core::demangle(symName);

    if (demangled.starts_with(strongSymbolPrefix)) {
      auto& matchedSyms = templateArgsToSymbolsMap[demangled.substr(
          strongSymbolPrefix.length())];
      if (!matchedSyms.first.empty()) {
        LOG(WARNING) << "non-unique symbols found: `" << matchedSyms.first
                     << "` and `" << symName << "`";
      }
      matchedSyms.first = symName;
    } else if (demangled.starts_with(weakSymbolPrefix)) {
      auto& matchedSyms =
          templateArgsToSymbolsMap[demangled.substr(weakSymbolPrefix.length())];
      if (!matchedSyms.second.empty()) {
        LOG(WARNING) << "non-unique symbols found: `" << matchedSyms.first
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

std::vector<std::tuple<drgn_qualified_type, std::string>>
OIGenerator::findOilTypesAndNames(drgnplusplus::program& prog) {
  auto strongToWeakSymbols = oilStrongToWeakSymbolsMap(prog);

  std::vector<std::tuple<drgn_qualified_type, std::string>> out;

  // TODO: Clean up this loop when switching to
  // drgn_program_find_function_by_address.
  for (auto& func : drgnplusplus::func_iterator(prog)) {
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

    auto templateParameters = drgn_type_template_parameters(func.type);
    drgn_type_template_parameter param = templateParameters[0];

    drgn_qualified_type paramType;
    if (auto err = drgnplusplus::error(
            drgn_template_parameter_type(&param, &paramType))) {
      LOG(ERROR) << "error getting drgn template parameter type: " << err;
      throw err;
    }

    if (drgn_type_has_name(paramType.type)) {
      LOG(INFO) << "found OIL type: " << drgn_type_name(paramType.type);
    } else {
      LOG(INFO) << "found OIL type: (no name)";
    }
    out.push_back({paramType, std::move(weakLinkageName)});
  }

  return out;
}

fs::path OIGenerator::generateForType(const OICodeGen::Config& generatorConfig,
                                      const OICompiler::Config& compilerConfig,
                                      const drgn_qualified_type& type,
                                      const std::string& linkageName,
                                      SymbolService& symbols) {
  auto codegen = OICodeGen::buildFromConfig(generatorConfig, symbols);
  if (!codegen) {
    LOG(ERROR) << "failed to initialise codegen";
    return {};
  }

  std::string code =
#include "OITraceCode.cpp"
      ;

  codegen->setRootType(type);
  codegen->setLinkageName(linkageName);

  if (!codegen->generate(code)) {
    LOG(ERROR) << "failed to generate code";
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
  tmpObject.replace_extension("." + linkageName + ".o");

  if (!compiler.compile(code, sourcePath, tmpObject)) {
    return {};
  }
  return tmpObject;
}

int OIGenerator::generate(fs::path& primaryObject, SymbolService& symbols) {
  drgnplusplus::program prog;

  {
    std::array<const char*, 1> objectPaths = {{primaryObject.c_str()}};
    if (auto err = drgnplusplus::error(drgn_program_load_debug_info(
            prog.get(), std::data(objectPaths), std::size(objectPaths), false,
            false))) {
      LOG(ERROR) << "error loading debug info program: " << err;
      throw err;
    }
  }

  std::vector<std::tuple<drgn_qualified_type, std::string>> oilTypes =
      findOilTypesAndNames(prog);

  std::map<Feature, bool> featuresMap = {
      {Feature::PackStructs, true},
  };

  OICodeGen::Config generatorConfig{};
  OICompiler::Config compilerConfig{};

  auto features = OIUtils::processConfigFile(configFilePath, featuresMap,
                                             compilerConfig, generatorConfig);
  if (!features) {
    LOG(ERROR) << "failed to process config file";
    return -1;
  }
  generatorConfig.features = *features;
  generatorConfig.useDataSegment = false;

  size_t failures = 0;
  for (const auto& [type, linkageName] : oilTypes) {
    if (auto obj = generateForType(generatorConfig, compilerConfig, type,
                                   linkageName, symbols);
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

}  // namespace ObjectIntrospection
