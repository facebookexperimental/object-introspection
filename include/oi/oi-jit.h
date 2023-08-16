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
#ifndef INCLUDED_OI_OI_JIT_H
#define INCLUDED_OI_OI_JIT_H 1

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

#include "exporters/inst.h"
#include "oi.h"

namespace oi::detail {
class OILibraryImpl;
}

namespace oi {

struct GeneratorOptions {
  std::filesystem::path configFilePath;
  std::filesystem::path sourceFileDumpPath;
  int debugLevel = 0;
};

class OILibrary {
 public:
  OILibrary(void* atomicHome,
            std::unordered_set<Feature>,
            GeneratorOptions opts);
  ~OILibrary();
  std::pair<void*, const exporters::inst::Inst&> init();

 private:
  std::unique_ptr<detail::OILibraryImpl> pimpl_;
};

/*
 * setupAndIntrospect
 *
 * Execute JIT compilation then introspect the given object. Throws on error.
 * Returns std::nullopt rather than duplicating initialisation if its ongoing.
 */
template <typename T, Feature... Fs>
std::optional<IntrospectionResult> setupAndIntrospect(
    const T& objectAddr, const GeneratorOptions& opts);

template <typename T, Feature... Fs>
class CodegenHandler {
 public:
  static bool init(const GeneratorOptions& opts);
  static IntrospectionResult introspect(const T& objectAddr);

 private:
  using func_type = void (*)(const T&, std::vector<uint8_t>&);

  static std::atomic<bool>& getIsCritical();
  static std::atomic<func_type>& getIntrospectionFunc();
  static std::atomic<const exporters::inst::Inst*>&
  getTreeBuilderInstructions();
};

}  // namespace oi

#include "oi-jit-inl.h"
#endif
