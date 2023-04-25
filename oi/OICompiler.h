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

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>

#include "oi/SymbolService.h"
#include "oi/X86InstDefs.h"

namespace fs = std::filesystem;

class OIMemoryManager;

/**
 * `OICompiler` provides the tools to compile and relocate code.
 * It also provides utilities to manipulate X86 assembly instructions
 * and retrieve the type to introspect for a given `irequest`.
 */
class OICompiler {
 public:
  /* Configuration option for `OICompiler` */
  struct Config {
    /* Whether to generate DWARF debug info for the JIT code */
    bool generateJitDebugInfo = false;

    std::vector<fs::path> userHeaderPaths{};
    std::vector<fs::path> sysHeaderPaths{};
  };

  /**
   * The result of a call to `applyRelocs()`.
   * It contains the next BaseRelocAddress for further relocation,
   * information about the Objects' buffers location and symbols.
   */
  struct RelocResult {
    /**
     * Information about an Object file and its relocation.
     *
     * BaseAddr contains the address of the object file buffer.
     * RelocAddr contains the relocated address of the object file buffer.
     * Size is the size of the object file buffer in bytes.
     *
     * You typically need to copy the BaseAddr buffer to RelocAddr in order to
     * finalize the relocation.
     * Note that we don't manage separate segments for code and data. Thus, the
     * RelocInfo struct describes one Slab which contains the two segments.
     */
    struct RelocInfo {
      uintptr_t BaseAddr, RelocAddr;
      size_t Size;
    };

    using RelocInfos = std::vector<RelocInfo>;
    using SymTable = std::unordered_map<std::string, uintptr_t>;

    uintptr_t newBaseRelocAddr;
    RelocInfos relocInfos;
    SymTable symbols;
  };

  /**
   * Generator that takes a series of opcodes in
   * and output the corresponding disassembled instructions.
   */
  class Disassembler {
   public:
    /*
     * Please forgive me :(
     * We have to remain compatible with C++17 for OICompiler.cpp
     * So we use std::basic_string_view instead of std::span.
     */
    template <typename T>
    using Span = std::basic_string_view<T>;

    /**
     * Instruction holds the information returned by the disassembler.
     * The fields are valid until a new Instruction struct has been
     * output by the disassembler.
     * There is no ownership on the fields, so copy the values
     * in owning data-structure, if you want to extend the lifetime.
     */
    struct Instruction {
      uintptr_t offset;
      Span<uint8_t> opcodes;
      std::string_view disassembly;
    };

    /**
     * Create a disassembler from anything that resemble a std::span.
     */
    template <typename... Args>
    Disassembler(Args&&... args) : funcText(std::forward<Args>(args)...) {
    }

    /*
     * Disassemble the next instuction, if any, and return the
     * corresponding Instruction struct.
     */
    std::optional<Instruction> operator()();

   private:
    uintptr_t offset = 0;
    Span<uint8_t> funcText;
    std::array<char, 128> disassemblyBuffer;
  };

  OICompiler(std::shared_ptr<SymbolService>, Config);
  ~OICompiler();

  /**
   * Compile the given @param code and write the result in @param objectPath.
   *
   * @param code the C++ source code to compile
   * @param sourcePath path/name of the code to compile (not used)
   * @param objectPath path where to write the resulting Object file
   *
   * @return true if the compilation succeeded, false otherwise.
   */
  bool compile(const std::string&, const fs::path&, const fs::path&);

  /**
   * Load the @param objectFiles in memory and apply relocation at
   * @param BaseRelocAddress. Note that it doesn't copy the object files at the
   * @param BaseRelocAddress, but returns all the information to make the copy.
   * Note: synthetic variables are symbols we define, but are used within the
   * JIT code. See 'OITraceCode.cpp' and its global variables declared with
   * extern.
   *
   * @param BaseRelocAddress where will the relocated code be located
   * @param objectFiles paths to the object files to load and relocate
   * @param syntheticSymbols a symbol table for synthetic variables
   *
   * @return a `std::optional` containing @ref RelocResult if the relocation was
   * successful. Calling `applyRelocs()` again invalidates the Segments
   * information. So make sure you copy the Segments' content before doing
   * another call.
   */
  std::optional<RelocResult> applyRelocs(
      uintptr_t, const std::set<fs::path>&,
      const std::unordered_map<std::string, uintptr_t>&);

  /**
   * Locates all the offsets of the given @param insts opcodes
   * in the @param funcText. Typically used to find all `ret` instructions
   * within a function.
   *
   * @param funcText the binary instruction of a function
   * @param insts an array of instruction buffers to look for in @param funcText
   *
   * @return an optional with the offsets where the given instructions were
   * found
   */
  template <class FuncTextRange, class NeedlesRange>
  static std::optional<std::vector<uintptr_t>> locateOpcodes(
      const FuncTextRange& funcText, const NeedlesRange& needles);

  /**
   * @return a string representation of the opcode(s) of the instruction found
   * at @param offset within function's binary instructions @param funcText.
   */
  static std::optional<std::string> decodeInst(const std::vector<std::byte>&,
                                               uintptr_t);

 private:
  std::shared_ptr<SymbolService> symbols;
  Config config;

  /**
   * memMgr is only used by applyReloc, but its lifetime must be larger than
   * the duration of the function. The RelocResult returned references addrs
   * manager by the Memory Manager, so we need to let the caller copy the code
   * and data sections to their final location before release the Objects
   * memory.
   * This is why memMgr is a std::unique_ptr in the class instead of a local
   * variable in the applyReloc function.
   */
  std::unique_ptr<OIMemoryManager> memMgr;
};

template <class FuncTextRange, class NeedlesRange>
std::optional<std::vector<uintptr_t>> OICompiler::locateOpcodes(
    const FuncTextRange& funcText, const NeedlesRange& needles) {
  auto DG = Disassembler((uint8_t*)std::data(funcText), std::size(funcText));

  std::vector<uintptr_t> locs;
  while (auto inst = DG()) {
    auto it = std::find_if(
        std::begin(needles), std::end(needles), [&](const auto& needle) {
          // Inst->opcodes.starts_with(needle);
          return 0 ==
                 inst->opcodes.find(OICompiler::Disassembler::Span<uint8_t>(
                     std::data(needle), std::size(needle)));
        });

    if (it != std::end(needles)) {
      locs.push_back(inst->offset);
    }
  }

  return locs;
}
