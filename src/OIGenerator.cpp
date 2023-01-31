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

#include "OIGenerator.h"

#include <glog/logging.h>

#include <fstream>
#include <variant>

#include "DrgnUtils.h"
#include "OIUtils.h"

namespace ObjectIntrospection {

std::vector<std::tuple<drgn_qualified_type, std::string>>
OIGenerator::findOilTypesAndNames(drgnplusplus::program& prog) {
  std::vector<std::tuple<drgn_qualified_type, std::string>> out;

  for (auto& func : drgnplusplus::func_iterator(prog)) {
    std::string fqdn;
    {
      char* fqdnChars;
      size_t fqdnLen;
      if (drgnplusplus::error err(
              drgn_type_fully_qualified_name(func.type, &fqdnChars, &fqdnLen));
          err) {
        LOG(ERROR) << "error getting drgn type fully qualified name: " << err;
        throw err;
      }
      fqdn = std::string(fqdnChars, fqdnLen);
    }

    if (!fqdn.starts_with("ObjectIntrospection::getObjectSize<")) {
      continue;
    }
    if (drgn_type_num_parameters(func.type) != 2) {
      continue;
    }
    if (drgn_type_num_template_parameters(func.type) != 1) {
      continue;
    }

    auto templateParameters = drgn_type_template_parameters(func.type);
    drgn_type_template_parameter param = templateParameters[0];

    drgn_qualified_type paramType;
    if (auto err = drgnplusplus::error(
            drgn_template_parameter_type(&param, &paramType))) {
      LOG(ERROR) << "error getting drgn template parameter type: " << err;
      throw err;
    }

    LOG(INFO) << "found OIL type: " << drgn_type_name(paramType.type);

    std::string linkageName;
    {
      char* linkageNameCstr;
      if (auto err = drgnplusplus::error(
              drgn_type_linkage_name(func.type, &linkageNameCstr))) {
        throw err;
      }
      linkageName = linkageNameCstr;
    }

    const std::string_view linkagePrefix =
        "_ZN19ObjectIntrospection13getObjectSize";
    if (!linkageName.starts_with(linkagePrefix)) {
      LOG(INFO) << "expected symbol to match but missing linkagePrefix: `"
                << linkageName << "`";
      continue;
    }
    LOG(INFO) << "found linkage name: " << linkageName;

    std::string weakLinkageName =
        "_ZN19ObjectIntrospection17getObjectSizeImpl" +
        linkageName.substr(linkagePrefix.length());
    out.push_back({paramType, std::move(weakLinkageName)});
  }

  return out;
}

bool OIGenerator::generateForType(const OICodeGen::Config& generatorConfig,
                                  const OICompiler::Config& compilerConfig,
                                  const drgn_qualified_type& type,
                                  const std::string& linkageName,
                                  SymbolService& symbols) {
  auto codegen = OICodeGen::buildFromConfig(generatorConfig, symbols);
  if (!codegen) {
    LOG(ERROR) << "failed to initialise codegen";
    return false;
  }

  std::string code =
#include "OITraceCode.cpp"
      ;

  codegen->setRootType(type);
  codegen->setLinkageName(linkageName);

  if (!codegen->generate(code)) {
    LOG(ERROR) << "failed to generate code";
    return false;
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
  return compiler.compile(code, sourcePath, outputPath);
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

  if (size_t count = oilTypes.size(); count > 1) {
    LOG(WARNING) << "oilgen can currently only generate for one type per "
                    "compilation unit and we found "
                 << count;
  }

  OICodeGen::Config generatorConfig{};
  OICompiler::Config compilerConfig{};
  if (!OIUtils::processConfigFile(configFilePath, compilerConfig,
                                  generatorConfig)) {
    LOG(ERROR) << "failed to process config file";
    return -1;
  }
  generatorConfig.useDataSegment = false;

  size_t failures = 0;
  for (const auto& [type, linkageName] : oilTypes) {
    if (!generateForType(generatorConfig, compilerConfig, type, linkageName,
                         symbols)) {
      LOG(WARNING) << "failed to generate for symbol `" << linkageName
                   << "`. this is non-fatal but the call will not work.";
      failures++;
    }
  }

  size_t successes = oilTypes.size() - failures;
  LOG(INFO) << "object introspection generation complete. " << successes
            << " successes and " << failures << " failures.";
  return (failures > 0) ? -1 : 0;
}

}  // namespace ObjectIntrospection
