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

#pragma once

#include <filesystem>

#include "DrgnUtils.h"
#include "OICodeGen.h"
#include "OICompiler.h"

namespace ObjectIntrospection {

class OIGenerator {
 public:
  int generate(fs::path& primaryObject, SymbolService& symbols);

  void setOutputPath(fs::path _outputPath) {
    outputPath = std::move(_outputPath);
  }
  void setConfigFilePath(fs::path _configFilePath) {
    configFilePath = std::move(_configFilePath);
  }
  void setSourceFileDumpPath(fs::path _sourceFileDumpPath) {
    sourceFileDumpPath = std::move(_sourceFileDumpPath);
  }
  void setFailIfNothingGenerated(bool fail) {
    failIfNothingGenerated = fail;
  }

 private:
  std::filesystem::path outputPath;
  std::filesystem::path configFilePath;
  std::filesystem::path sourceFileDumpPath;
  bool failIfNothingGenerated;

  std::unordered_map<std::string, std::string> oilStrongToWeakSymbolsMap(
      drgnplusplus::program& prog);

  std::vector<std::tuple<drgn_qualified_type, std::string>>
  findOilTypesAndNames(drgnplusplus::program& prog);

  std::filesystem::path generateForType(
      const OICodeGen::Config& generatorConfig,
      const OICompiler::Config& compilerConfig, const drgn_qualified_type& type,
      const std::string& linkageName, SymbolService& symbols);
};

}  // namespace ObjectIntrospection
