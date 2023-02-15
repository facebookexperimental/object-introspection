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

#include <fcntl.h>
#include <glog/logging.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <boost/format.hpp>
#include <fstream>

#include "OIParser.h"
#include "OIUtils.h"

extern "C" {
#include <libelf.h>
}

namespace ObjectIntrospection {

/**
 * We need a way to identify the BLOB/Object file, but we don't have access
 * to the function's name or the introspected type's name without
 * initialising drgn.
 * Exclusively to OIL, we won't use the type's name, but the templated
 * function address. We assume the address of the templated function
 * changes at every compilation, so we don't re-use object files that
 * are for an older version of the binary.
 */
const std::string function_identifier(uintptr_t functionAddress) {
  return (boost::format("%x") % (uint32_t)(functionAddress % UINT32_MAX)).str();
}

OILibraryImpl::OILibraryImpl(OILibrary *self, void *TemplateFunc)
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
  void *textSeg =
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

  compilerConfig.generateJitDebugInfo = _self->opts.generateJitDebugInfo;

  generatorConfig.useDataSegment = false;
  generatorConfig.chaseRawPointers = _self->opts.chaseRawPointers;
  generatorConfig.packStructs = true;
  generatorConfig.genPaddingStats = false;
}

bool OILibraryImpl::processConfigFile() {
  return OIUtils::processConfigFile(_self->opts.configFilePath, compilerConfig,
                                    generatorConfig);
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

void close_file(std::FILE *fp) {
  std::fclose(fp);
}

static inline void logElfError(const char *message) {
  const char *elf_error_message = elf_errmsg(0);
  if (elf_error_message)
    LOG(ERROR) << message << ": " << elf_error_message;
  else
    LOG(ERROR) << message;
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

  if (_self->opts.forceJIT) {
    struct drgn_program *prog = symbols->getDrgnProgram();
    if (!prog) {
      return Response::OIL_COMPILATION_FAILURE;
    }
    struct drgn_symbol *sym;
    if (auto err = drgn_program_find_symbol_by_address(
            prog, (uintptr_t)_TemplateFunc, &sym)) {
      LOG(ERROR) << "Error when finding symbol by address " << err->code << " "
                 << err->message;
      drgn_error_destroy(err);
      return Response::OIL_COMPILATION_FAILURE;
    }
    const char *name = drgn_symbol_name(sym);
    drgn_symbol_destroy(sym);

    auto rootType = symbols->getRootType(irequest{"entry", name, "arg0"});
    if (!rootType.has_value()) {
      LOG(ERROR) << "Failed to get type of probe argument";
      return Response::OIL_COMPILATION_FAILURE;
    }

    std::string code =
#include "OITraceCode.cpp"
        ;

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
  } else {
    auto executable =
        open(fs::read_symlink("/proc/self/exe").c_str(), O_RDONLY);
    if (executable == -1) {
      PLOG(ERROR) << "Failed to open executable file";
      return Response::OIL_COMPILATION_FAILURE;
    }
    auto __executable_cleanup = Cleanup(executable, close);
    elf_version(EV_CURRENT);
    auto elf = elf_begin(executable, ELF_C_READ, NULL);
    auto __elf_cleanup = Cleanup(elf, elf_end);
    GElf_Ehdr ehdr;
    if (!gelf_getehdr(elf, &ehdr)) {
      logElfError("Failed to get ELF object file header");
      return Response::OIL_COMPILATION_FAILURE;
    }
    size_t string_table_index;
    if (elf_getshdrstrndx(elf, &string_table_index) != 0) {
      logElfError("Failed to get index of the section header string table");
      return Response::OIL_COMPILATION_FAILURE;
    }

    Elf_Scn *section = NULL;
    bool done = false;
    const auto identifier = function_identifier((uintptr_t)_TemplateFunc);
    const auto section_name = OI_SECTION_PREFIX.data() + identifier;
    while ((section = elf_nextscn(elf, section))) {
      GElf_Shdr section_header;
      GElf_Shdr *header = gelf_getshdr(section, &section_header);
      if (!header)
        continue;
      const char *name = elf_strptr(elf, string_table_index, header->sh_name);
      if (name && strcmp(name, section_name.c_str()) == 0) {
        Elf_Data *section_data;
        if (!(section_data = elf_getdata(section, NULL))) {
          LOG(ERROR) << "Failed to get data from section '" << name
                     << "': " << elf_errmsg(0);
          return Response::OIL_COMPILATION_FAILURE;
        }
        if (section_data->d_size == 0) {
          LOG(ERROR) << "Section '" << name << "' is empty";
          return Response::OIL_COMPILATION_FAILURE;
        }
        if (fwrite(section_data->d_buf, 1, section_data->d_size,
                   objectStream.get()) < section_data->d_size) {
          PLOG(ERROR)
              << "Failed to write object file contents to temporary file";
          return Response::OIL_COMPILATION_FAILURE;
        }
        done = true;
        break;
      }
    }
    if (!done) {
      LOG(ERROR) << "Did not find section '" << section_name
                 << "' in the executable";
      return Response::OIL_COMPILATION_FAILURE;
    }
    fflush(objectStream.get());
  }

  auto relocRes = compiler.applyRelocs(
      reinterpret_cast<uint64_t>(segConfig.textSegBase), {objectPath}, {});
  if (!relocRes.has_value()) {
    return Response::OIL_RELOCATION_FAILURE;
  }

  const auto &[_, segments, jitSymbols] = relocRes.value();

  // Locate the probe's entry point
  _self->fp = nullptr;
  for (const auto &[symName, symAddr] : jitSymbols) {
    if (symName.starts_with("_Z7getSize")) {
      _self->fp = (size_t(*)(const void *))symAddr;
      break;
    }
  }
  if (!_self->fp)
    return Response::OIL_RELOCATION_FAILURE;

  // Copy relocated segments in their final destination
  for (const auto &[BaseAddr, RelocAddr, Size] : segments)
    memcpy((void *)RelocAddr, (void *)BaseAddr, Size);

  return Response::OIL_SUCCESS;
}

}  // namespace ObjectIntrospection
