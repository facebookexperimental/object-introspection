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
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "oi/Descs.h"
#include "oi/TypeHierarchy.h"

struct Dwfl;
struct drgn_program;
struct irequest;

namespace oi::detail {

struct SymbolInfo {
  uint64_t addr;
  uint64_t size;
};

class SymbolService {
 public:
  SymbolService(pid_t);
  SymbolService(std::filesystem::path);
  SymbolService(const SymbolService&) = delete;
  SymbolService& operator=(const SymbolService&) = delete;
  ~SymbolService();

  struct drgn_program* getDrgnProgram();

  std::optional<std::string> locateBuildID();
  std::optional<SymbolInfo> locateSymbol(const std::string&,
                                         bool demangle = false);

  std::shared_ptr<FuncDesc> findFuncDesc(const irequest&);
  std::shared_ptr<GlobalDesc> findGlobalDesc(const std::string&);
  static std::string getTypeName(struct drgn_type*);
  std::optional<RootInfo> getDrgnRootType(const irequest&);

  static std::optional<drgn_qualified_type> findTypeOfSymbol(
      drgn_program*, const std::string& symbolName);
  static std::optional<drgn_qualified_type> findTypeOfAddr(drgn_program*,
                                                           uintptr_t addr);

  std::unordered_map<std::string, std::shared_ptr<FuncDesc>> funcDescs;
  std::unordered_map<std::string, std::shared_ptr<GlobalDesc>> globalDescs;

  void setHardDisableDrgn(bool val) {
    hardDisableDrgn = val;
  }

 private:
  std::variant<pid_t, std::filesystem::path> target;
  struct Dwfl* dwfl{nullptr};
  struct drgn_program* prog{nullptr};

  bool loadModules();
  bool loadModulesFromPid(pid_t);
  bool loadModulesFromPath(const std::filesystem::path&);

  std::vector<std::pair<uint64_t, uint64_t>> executableAddrs{};
  bool hardDisableDrgn = false;

 protected:
  SymbolService() = default;  // For unit tests
};

}  // namespace oi::detail
