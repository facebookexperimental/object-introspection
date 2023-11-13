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
#include "OILibraryImpl.h"

#include <glog/logging.h>
#include <sys/mman.h>

#include <boost/core/demangle.hpp>
#include <boost/format.hpp>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <stdexcept>

#include "oi/Config.h"
#include "oi/DrgnUtils.h"
#include "oi/Headers.h"

namespace oi::detail {
namespace {
// Map between the high level feature requests in the OIL API and the underlying
// codegen features.
std::map<Feature, bool> convertFeatures(std::unordered_set<oi::Feature> fs);

// Extract the root type from an atomic function pointer
drgn_qualified_type getTypeFromAtomicHole(drgn_program* prog, void* hole);
}  // namespace

OILibraryImpl::LocalTextSegment::LocalTextSegment(size_t size) {
  void* base = mmap(NULL,
                    size,
                    PROT_EXEC | PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS,
                    -1,
                    0);
  if (base == MAP_FAILED)
    throw std::runtime_error(std::string("segment map failed: ") +
                             std::strerror(errno));

  data_ = {static_cast<uint8_t*>(base), size};
}

OILibraryImpl::LocalTextSegment::~LocalTextSegment() {
  if (data_.empty())
    return;

  PLOG_IF(ERROR, munmap(data_.data(), data_.size()) != 0)
      << "segment unmap failed";
}

OILibraryImpl::MemoryFile::MemoryFile(const char* name) {
  fd_ = memfd_create(name, 0);
  if (fd_ == -1)
    throw std::runtime_error(std::string("memfd creation failed: ") +
                             std::strerror(errno));
}

OILibraryImpl::MemoryFile::~MemoryFile() {
  if (fd_ == -1)
    return;

  PLOG_IF(ERROR, close(fd_) == -1) << "memfd close failed";
}

std::filesystem::path OILibraryImpl::MemoryFile::path() {
  return {(boost::format("/dev/fd/%1%") % fd_).str()};
}

OILibraryImpl::OILibraryImpl(void* atomicHole,
                             std::unordered_set<oi::Feature> fs,
                             GeneratorOptions opts)
    : atomicHole_(atomicHole),
      requestedFeatures_(convertFeatures(std::move(fs))),
      opts_(std::move(opts)) {
}

std::pair<void*, const exporters::inst::Inst&> OILibraryImpl::init() {
  processConfigFile();

  constexpr size_t TextSegSize = 1u << 22;
  textSeg = {TextSegSize};

  return compileCode();
}

void OILibraryImpl::processConfigFile() {
  auto features = config::processConfigFiles(opts_.configFilePaths,
                                             requestedFeatures_,
                                             compilerConfig_,
                                             generatorConfig_);
  if (!features)
    throw std::runtime_error("failed to process configuration");

  generatorConfig_.features = *features;
  compilerConfig_.features = *features;
}

std::pair<void*, const exporters::inst::Inst&> OILibraryImpl::compileCode() {
  google::SetVLOGLevel("*", opts_.debugLevel);

  auto symbols = std::make_shared<SymbolService>(getpid());

  auto* prog = symbols->getDrgnProgram();
  CHECK(prog != nullptr) << "does this check need to exist?";

  auto rootType = getTypeFromAtomicHole(symbols->getDrgnProgram(), atomicHole_);

  CodeGen codegen{generatorConfig_, *symbols};

  std::string code;
  if (!codegen.codegenFromDrgn(rootType.type, code))
    throw std::runtime_error("oil jit codegen failed!");

  std::string sourcePath = opts_.sourceFileDumpPath;
  if (sourcePath.empty()) {
    sourcePath = "oil_jit.cpp";  // fake path for JIT debug info
  } else {
    std::ofstream outputFile(sourcePath);
    outputFile << code;
  }

  auto object = MemoryFile("oil_object_code");
  OICompiler compiler{symbols, compilerConfig_};
  if (!compiler.compile(code, sourcePath, object.path()))
    throw std::runtime_error("oil jit compilation failed!");

  auto relocRes = compiler.applyRelocs(
      reinterpret_cast<uint64_t>(textSeg.data().data()), {object.path()}, {});
  if (!relocRes)
    throw std::runtime_error("oil jit relocation failed!");

  const auto& [_, segments, jitSymbols] = *relocRes;

  std::string nameHash =
      (boost::format("%1$016x") %
       std::hash<std::string>{}(SymbolService::getTypeName(rootType.type)))
          .str();
  std::string functionSymbolPrefix = "_Z27introspect_" + nameHash;
  std::string typeSymbolName = "treeBuilderInstructions" + nameHash;
  void* fp = nullptr;
  const exporters::inst::Inst* ty = nullptr;
  for (const auto& [symName, symAddr] : jitSymbols) {
    if (fp == nullptr && symName.starts_with(functionSymbolPrefix)) {
      fp = reinterpret_cast<void*>(symAddr);
      if (ty != nullptr)
        break;
    } else if (ty == nullptr && symName == typeSymbolName) {
      ty = reinterpret_cast<const exporters::inst::Inst*>(symAddr);
      if (fp != nullptr)
        break;
    }
  }

  CHECK(fp != nullptr && ty != nullptr)
      << "failed to find always present symbols!";

  for (const auto& [baseAddr, relocAddr, size] : segments)
    std::memcpy(reinterpret_cast<void*>(relocAddr),
                reinterpret_cast<void*>(baseAddr),
                size);

  textSeg.release();  // don't munmap() the region containing the code
  return {fp, *ty};
}

namespace {
std::map<Feature, bool> convertFeatures(std::unordered_set<oi::Feature> fs) {
  std::map<Feature, bool> out{
      {Feature::TypeGraph, true},
      {Feature::TreeBuilderV2, true},
      {Feature::Library, true},
      {Feature::PackStructs, true},
      {Feature::PruneTypeGraph, true},
  };

  for (const auto f : fs) {
    switch (f) {
      case oi::Feature::ChaseRawPointers:
        out[Feature::ChaseRawPointers] = true;
        break;
      case oi::Feature::CaptureThriftIsset:
        out[Feature::CaptureThriftIsset] = true;
        break;
      case oi::Feature::GenJitDebug:
        out[Feature::GenJitDebug] = true;
        break;
    }
  }

  return out;
}

drgn_qualified_type getTypeFromAtomicHole(drgn_program* prog, void* hole) {
  // get the getter type:
  // std::atomic<std::vector<uint8_t> (*)(const T&)>& getIntrospectionFunc();
  auto atomicGetterType =
      SymbolService::findTypeOfAddr(prog, reinterpret_cast<uintptr_t>(hole));
  if (!atomicGetterType)
    throw std::runtime_error("failed to lookup function");

  // get the return type:
  // std::atomic<std::vector<uint8_t> (*)(const T&)>&
  CHECK(drgn_type_has_type(atomicGetterType->type))
      << "functions have a return type";
  auto retType = drgn_type_type(atomicGetterType->type);

  // get the atomic type:
  // std::atomic<std::vector<uint8_t> (*)(const T&)>
  CHECK(drgn_type_has_type(retType.type)) << "pointers have a value type";
  auto atomicType = drgn_type_type(retType.type);

  // get the function pointer type:
  // std::vector<uint8_t> (*)(const T&)
  CHECK(drgn_type_has_template_parameters(atomicType.type))
      << "atomic should have template parameters";
  CHECK(drgn_type_num_template_parameters(atomicType.type) == 1)
      << "atomic should have 1 template parameter";
  auto* templateParam = drgn_type_template_parameters(atomicType.type);
  struct drgn_qualified_type funcPointerType;
  if (auto err = drgn_template_parameter_type(templateParam, &funcPointerType))
    throw drgnplusplus::error(err);

  // get the function type:
  // std::vector<uint8_t>(const T&)
  CHECK(drgn_type_has_type(funcPointerType.type))
      << "function pointers have a value type";
  auto funcType = drgn_type_type(funcPointerType.type);

  // get the argument type:
  // const T&
  CHECK(drgn_type_has_parameters(funcType.type)) << "functions have parameters";
  CHECK(drgn_type_num_parameters(funcType.type) == 2)
      << "function should have 2 parameters";
  drgn_qualified_type argType;
  if (auto err =
          drgn_parameter_type(drgn_type_parameters(funcType.type), &argType))
    throw drgnplusplus::error(err);

  // get the type
  CHECK(drgn_type_has_type(argType.type))
      << "reference types have a value type";
  return drgn_type_type(argType.type);
}

}  // namespace
}  // namespace oi::detail
