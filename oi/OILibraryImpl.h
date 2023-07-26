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

#include "ObjectIntrospection.h"
#include "oi/OICodeGen.h"
#include "oi/OICompiler.h"
#include "oi/SymbolService.h"

namespace ObjectIntrospection {

class OILibraryImpl {
 public:
  OILibraryImpl(OILibrary*, void*);
  ~OILibraryImpl();

  bool mapSegment();
  bool unmapSegment();
  void initCompiler();
  int compileCode();
  bool processConfigFile();
  void enableLayoutAnalysis();

 private:
  class OILibrary* _self;

  void* _TemplateFunc;

  oi::detail::OICompiler::Config compilerConfig{};
  oi::detail::OICodeGen::Config generatorConfig{};
  std::shared_ptr<oi::detail::SymbolService> symbols{};

  struct c {
    void* textSegBase = nullptr;
    size_t textSegSize = 1u << 22;
  } segConfig;
};
}  // namespace ObjectIntrospection
