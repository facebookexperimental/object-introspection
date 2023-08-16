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
#include <oi/oi.h>

#include <filesystem>
#include <map>
#include <span>
#include <unordered_set>
#include <utility>

#include "oi/CodeGen.h"
#include "oi/Features.h"
#include "oi/OICompiler.h"

namespace oi::detail {

class OILibraryImpl {
 private:
  class LocalTextSegment {
   public:
    LocalTextSegment() = default;
    LocalTextSegment(size_t size);
    ~LocalTextSegment();
    LocalTextSegment(const LocalTextSegment&) = delete;
    LocalTextSegment& operator=(const LocalTextSegment&) = delete;
    LocalTextSegment(LocalTextSegment&& that) {
      std::swap(this->data_, that.data_);
    }
    LocalTextSegment& operator=(LocalTextSegment&& that) {
      std::swap(this->data_, that.data_);
      return *this;
    }

    std::span<uint8_t> data() {
      return data_;
    }
    void release() {
      data_ = {};
    }

   private:
    std::span<uint8_t> data_;
  };
  class MemoryFile {
   public:
    MemoryFile(const char* name);
    ~MemoryFile();

    std::filesystem::path path();

   private:
    int fd_ = -1;
  };

 public:
  OILibraryImpl(void* atomicHole,
                std::unordered_set<oi::Feature> fs,
                GeneratorOptions opts);
  std::pair<void*, const exporters::inst::Inst&> init();

 private:
  void* atomicHole_;
  std::map<Feature, bool> requestedFeatures_;
  GeneratorOptions opts_;

  oi::detail::OICompiler::Config compilerConfig_{};
  oi::detail::OICodeGen::Config generatorConfig_{};

  LocalTextSegment textSeg;

  void processConfigFile();
  std::pair<void*, const exporters::inst::Inst&> compileCode();
};

}  // namespace oi::detail
