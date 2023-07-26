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
#include "oi/OILibraryImpl.h"

#include <glog/logging.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <boost/format.hpp>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "ObjectIntrospection.h"
#include "glog/vlog_is_on.h"
#include "oi/Features.h"
#include "oi/Headers.h"
#include "oi/OIParser.h"
#include "oi/OIUtils.h"
#include "oi/SymbolService.h"
#include "oi/TypeHierarchy.h"

extern "C" {
#include <drgn.h>
#include <sys/mman.h>
}

namespace ObjectIntrospection {

OILibraryImpl::OILibraryImpl(OILibrary* self, void* TemplateFunc)
    : _self(self), _TemplateFunc(TemplateFunc) {
  if (_self->opts.debugLevel != 0) {
    google::LogToStderr();
    google::SetStderrLogging(0);
    google::SetVLOGLevel("*", _self->opts.debugLevel);
    // Upstream glog defines `GLOG_INFO` as 0 https://fburl.com/ydjajhz0,
    // but internally it's defined as 1 https://fburl.com/code/9fwams75
    //
    // We don't want to link gflags in OIL, so setting it via the flags rather
    // than with gflags::SetCommandLineOption
    FLAGS_minloglevel = 0;
  }
}

OILibraryImpl::~OILibraryImpl() {
  unmapSegment();
}

bool OILibraryImpl::mapSegment() {
  void* textSeg =
      mmap(NULL, segConfig.textSegSize, PROT_EXEC | PROT_READ | PROT_WRITE,
           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (textSeg == MAP_FAILED) {
    PLOG(ERROR) << "error mapping text segment";
    return false;
  }
  segConfig.textSegBase = textSeg;

  return true;
}

bool OILibraryImpl::unmapSegment() {
  if (segConfig.textSegBase != nullptr &&
      munmap(segConfig.textSegBase, segConfig.textSegSize) != 0) {
    PLOG(ERROR) << "error unmapping text segment";
    return false;
  }

  return true;
}

void OILibraryImpl::initCompiler() {
  symbols = std::make_shared<SymbolService>(getpid());

  generatorConfig.useDataSegment = false;
}

bool OILibraryImpl::processConfigFile() {
  auto features = OIUtils::processConfigFile(
      _self->opts.configFilePath,
      {
          {Feature::ChaseRawPointers, _self->opts.chaseRawPointers},
          {Feature::PackStructs, true},
          {Feature::GenJitDebug, _self->opts.generateJitDebugInfo},
      },
      compilerConfig, generatorConfig);
  if (!features) {
    return false;
  }
  generatorConfig.features = *features;
  compilerConfig.features = *features;
  return true;
}

template <class T, class F>
class Cleanup {
  T resource;
  F cleanupFunc;

 public:
  Cleanup(T _resource, F _cleanupFunc)
      : resource{_resource}, cleanupFunc{_cleanupFunc} {};
  ~Cleanup() {
    cleanupFunc(resource);
  }
};

void close_file(std::FILE* fp) {
  std::fclose(fp);
}

int OILibraryImpl::compileCode() {
  OICompiler compiler{symbols, compilerConfig};

  int objectMemfd = memfd_create("oil_object_code", 0);
  if (!objectMemfd) {
    PLOG(ERROR) << "failed to create memfd for object code";
    return Response::OIL_COMPILATION_FAILURE;
  }

  using unique_file_t = std::unique_ptr<std::FILE, decltype(&close_file)>;
  unique_file_t objectStream(fdopen(objectMemfd, "w+"), &close_file);
  if (!objectStream) {
    PLOG(ERROR) << "failed to convert memfd to stream";
    // This only needs to be cleaned up in the error case, as the fclose
    // on the unique_file_t will clean up the underlying fd if it was
    // created successfully.
    close(objectMemfd);
    return Response::OIL_COMPILATION_FAILURE;
  }
  auto objectPath =
      fs::path((boost::format("/dev/fd/%1%") % objectMemfd).str());

  struct drgn_program* prog = symbols->getDrgnProgram();
  if (!prog) {
    return Response::OIL_COMPILATION_FAILURE;
  }
  struct drgn_symbol* sym;
  if (auto err = drgn_program_find_symbol_by_address(
          prog, (uintptr_t)_TemplateFunc, &sym)) {
    LOG(ERROR) << "Error when finding symbol by address " << err->code << " "
               << err->message;
    drgn_error_destroy(err);
    return Response::OIL_COMPILATION_FAILURE;
  }
  const char* name = drgn_symbol_name(sym);
  drgn_symbol_destroy(sym);

  // TODO: change this to the new drgn interface from symbol -> type
  auto rootType = symbols->getRootType(irequest{"entry", name, "arg0"});
  if (!rootType.has_value()) {
    LOG(ERROR) << "Failed to get type of probe argument";
    return Response::OIL_COMPILATION_FAILURE;
  }

  std::string code(headers::oi_OITraceCode_cpp);

  auto codegen = OICodeGen::buildFromConfig(generatorConfig, *symbols);
  if (!codegen) {
    return OIL_COMPILATION_FAILURE;
  }

  codegen->setRootType(rootType->type);
  if (!codegen->generate(code)) {
    return Response::OIL_COMPILATION_FAILURE;
  }

  std::string sourcePath = _self->opts.sourceFileDumpPath;
  if (_self->opts.sourceFileDumpPath.empty()) {
    // This is the path Clang acts as if it has compiled from e.g. for debug
    // information. It does not need to exist.
    sourcePath = "oil_jit.cpp";
  } else {
    std::ofstream outputFile(sourcePath);
    outputFile << code;
  }

  if (!compiler.compile(code, sourcePath, objectPath)) {
    return Response::OIL_COMPILATION_FAILURE;
  }

  auto relocRes = compiler.applyRelocs(
      reinterpret_cast<uint64_t>(segConfig.textSegBase), {objectPath}, {});
  if (!relocRes.has_value()) {
    return Response::OIL_RELOCATION_FAILURE;
  }

  const auto& [_, segments, jitSymbols] = relocRes.value();

  // Locate the probe's entry point
  _self->fp = nullptr;
  for (const auto& [symName, symAddr] : jitSymbols) {
    if (symName.starts_with("_Z7getSize")) {
      _self->fp = (size_t(*)(const void*))symAddr;
      break;
    }
  }
  if (!_self->fp) {
    return Response::OIL_RELOCATION_FAILURE;
  }

  // Copy relocated segments in their final destination
  for (const auto& [BaseAddr, RelocAddr, Size] : segments)
    memcpy((void*)RelocAddr, (void*)BaseAddr, Size);

  return Response::OIL_SUCCESS;
}

}  // namespace ObjectIntrospection
