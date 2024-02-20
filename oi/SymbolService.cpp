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
#include "oi/SymbolService.h"

#include <glog/logging.h>

#include <algorithm>
#include <boost/scope_exit.hpp>
#include <cassert>
#include <cstring>
#include <fstream>

#include "oi/DrgnUtils.h"
#include "oi/OIParser.h"

extern "C" {
#include <elfutils/known-dwarf.h>
#include <elfutils/libdwfl.h>

#include "drgn.h"
#include "libdw/dwarf.h"
}

namespace fs = std::filesystem;

namespace oi::detail {

template <typename... Ts>
struct visitor : Ts... {
  using Ts::operator()...;
};

// Type deduction for the helper above
template <typename... Ts>
visitor(Ts...) -> visitor<Ts...>;

static bool LoadExecutableAddressRange(
    pid_t pid, std::vector<std::pair<uint64_t, uint64_t>>& exeAddrs) {
  std::ifstream f("/proc/" + std::to_string(pid) + "/maps");

  if (f.is_open()) {
    std::string line;
    uint64_t start = 0;
    uint64_t end = 0;
    uint64_t offset = 0;
    uint64_t inode = 0;
    uint dmajor = 0;
    uint dminor = 0;
    int nread = -1;
    constexpr int permissionsLen = 4;
    char perm[permissionsLen + 1];

    while (std::getline(f, line)) {
      if (sscanf(line.c_str(),
                 "%" PRIx64 "-%" PRIx64 " %s %" PRIx64 " %x:%x %" PRIu64 " %n",
                 &start,
                 &end,
                 perm,
                 &offset,
                 &dmajor,
                 &dminor,
                 &inode,
                 &nread) < 7 ||
          nread <= 0) {
        return false;
      }

      if (strlen(perm) != permissionsLen) {
        return false;
      }

      if (perm[2] == 'x') {
        exeAddrs.emplace_back(start, end);
      }
    }
  }
  return true;
}

#undef PREMISSIONS_LEN

static bool isExecutableAddr(
    uint64_t addr, const std::vector<std::pair<uint64_t, uint64_t>>& exeAddrs) {
  assert(std::is_sorted(begin(exeAddrs), end(exeAddrs)));

  // Find the smallest exeAddrs range where addr < range.end
  auto it = std::upper_bound(
      begin(exeAddrs),
      end(exeAddrs),
      std::make_pair(addr, addr),
      [](const auto& r1, const auto& r2) { return r1.second < r2.second; });

  return it != end(exeAddrs) && addr >= it->first;
}

SymbolService::SymbolService(pid_t pid) : target{pid} {
  // Update target processes memory map
  LoadExecutableAddressRange(pid, executableAddrs);
  if (!loadModules()) {
    throw std::runtime_error("Failed to load modules for process " +
                             std::to_string(pid));
  }
}

SymbolService::SymbolService(fs::path executablePath)
    : target{std::move(executablePath)} {
  if (!loadModules()) {
    throw std::runtime_error("Failed to load modules for executable " +
                             executablePath.string());
  }
}

SymbolService::~SymbolService() {
  if (dwfl != nullptr) {
    dwfl_end(dwfl);
  }

  if (prog != nullptr) {
    drgn_program_destroy(prog);
  }
}

struct ModParams {
  std::string_view symName;
  GElf_Sym sym;
  GElf_Addr value;
  std::vector<std::pair<uint64_t, uint64_t>>& exeAddrs;
  bool demangle;
};

/**
 * Callback for dwfl_getmodules(). For the provided module we iterate
 * through its symbol table and look for the given symbol. Values
 * are passed in and out via the 'arg' parameter.
 *
 * @param[in] arg[0] - The symbol to locate.
 * @param[out] arg[1] - Symbol information if found.
 * @param[out] arg[2] - Address of the symbol if found.
 *
 */

static int moduleCallback(Dwfl_Module* mod,
                          void** /* userData */,
                          const char* name,
                          Dwarf_Addr /* start */,
                          void* arg) {
  ModParams* m = (ModParams*)arg;

  int nsym = dwfl_module_getsymtab(mod);
  VLOG(1) << "mod name: " << name << " "
          << "nsyms " << nsym;

  // FIXME: There's surely a better way to distinguish debuginfo modules from
  //        actual code modules.
  char debugSuffix[] = ".debuginfo";
  size_t debugSuffixLen = sizeof(debugSuffix) - 1;
  size_t nameLen = strlen(name);

  if (debugSuffixLen <= nameLen) {
    if (strncmp(name + nameLen - debugSuffixLen, debugSuffix, debugSuffixLen) ==
        0) {
      VLOG(1) << "Skipping debuginfo module";
      m->value = 0;
      return DWARF_CB_OK;
    }
  }

  /* I think the first entry is always UNDEF */
  for (int i = 1; i < nsym; ++i) {
    Elf* elf = nullptr;
    GElf_Word shndxp = 0;

    const char* lookupResult = dwfl_module_getsym_info(
        mod, i, &m->sym, &m->value, &shndxp, &elf, nullptr);
    if (lookupResult == nullptr || lookupResult[0] == '\0') {
      continue;
    }

    std::string symName = lookupResult;

    if (m->demangle) {
      symName = boost::core::demangle(symName.c_str());
    }

    switch
      GELF_ST_TYPE(m->sym.st_info) {
        case STT_SECTION:
        case STT_FILE:
        case STT_TLS:
        case STT_NOTYPE:
          break;

        case STT_OBJECT:
          if (shndxp != SHN_UNDEF && symName == m->symName) {
            VLOG(1) << "Symbol lookup successful for " << symName
                    << " in module " << name;
            return DWARF_CB_ABORT;
          }
          break;

        default:
          /*
           * I don't understand why the only symbol that is presented
           * to us here has NOTYPE yet readelf shows me it is defined
           * as an STT_FUNC. Confused...
           */
          if (shndxp != SHN_UNDEF && symName == m->symName &&
              isExecutableAddr(m->value, m->exeAddrs)) {
            VLOG(1) << "Symbol lookup successful for " << symName
                    << " in module " << name;

            return DWARF_CB_ABORT;
          }
          break;
      }
  }

  // Set m->value to 0 if symbol is not found
  m->value = 0;
  return DWARF_CB_OK;
}

/* Load modules from a live process */
bool SymbolService::loadModulesFromPid(pid_t targetPid) {
  if (int err = dwfl_linux_proc_report(dwfl, targetPid)) {
    LOG(ERROR) << "dwfl_linux_proc_report: " << dwfl_errmsg(err);
    return false;
  }

  return true;
}

/* Load modules from an ELF binary */
bool SymbolService::loadModulesFromPath(const fs::path& targetPath) {
  auto* mod =
      dwfl_report_offline(dwfl, targetPath.c_str(), targetPath.c_str(), -1);
  if (mod == nullptr) {
    LOG(ERROR) << "dwfl_report_offline: " << dwfl_errmsg(dwfl_errno());
    return false;
  }

  Dwarf_Addr start = 0;
  Dwarf_Addr end = 0;
  if (dwfl_module_info(
          mod, nullptr, &start, &end, nullptr, nullptr, nullptr, nullptr) ==
      nullptr) {
    LOG(ERROR) << "dwfl_module_info: " << dwfl_errmsg(dwfl_errno());
    return false;
  }

  VLOG(1) << "Module info for " << targetPath << ": start= " << std::hex
          << start << ", end=" << end;

  // Add module's boundary to executableAddrs
  executableAddrs = {{start, end}};

  return true;
}

bool SymbolService::loadModules() {
  static char* debuginfo_path;
  static const Dwfl_Callbacks proc_callbacks{
      .find_elf = dwfl_linux_proc_find_elf,
      .find_debuginfo = dwfl_standard_find_debuginfo,
      .section_address = dwfl_offline_section_address,
      .debuginfo_path = &debuginfo_path,
  };

  dwfl = dwfl_begin(&proc_callbacks);
  if (dwfl == nullptr) {
    LOG(ERROR) << "dwfl_begin: " << dwfl_errmsg(dwfl_errno());
    return false;
  }

  dwfl_report_begin(dwfl);

  bool ok = std::visit(
      visitor{[this](pid_t targetPid) { return loadModulesFromPid(targetPid); },
              [this](const fs::path& targetPath) {
                return loadModulesFromPath(targetPath);
              }},
      target);

  if (!ok) {
    // The loadModules* function above already logged the error message
    return false;
  }

  if (dwfl_report_end(dwfl, nullptr, nullptr) != 0) {
    LOG(ERROR) << "dwfl_report_end: " << dwfl_errmsg(-1);
    return false;
  }

  return true;
}

std::optional<drgn_qualified_type> SymbolService::findTypeOfSymbol(
    drgn_program* prog, const std::string& symbolName) {
  drgn_symbol* sym;
  if (auto* err =
          drgn_program_find_symbol_by_name(prog, symbolName.c_str(), &sym);
      err != nullptr) {
    LOG(ERROR) << "Failed to lookup symbol '" << symbolName
               << "': " << err->code << " " << err->message;
    drgn_error_destroy(err);
    return std::nullopt;
  }

  uint64_t addr = drgn_symbol_address(sym);
  drgn_symbol_destroy(sym);

  if (auto t = findTypeOfAddr(prog, addr)) {
    return t;
  } else {
    LOG(ERROR) << "Failed to lookup symbol '" << symbolName;
    return std::nullopt;
  }
}

std::optional<drgn_qualified_type> SymbolService::findTypeOfAddr(
    drgn_program* prog, uintptr_t addr) {
  drgn_object obj;
  drgn_object_init(&obj, prog);

  const char* name;
  if (auto* err =
          drgn_program_find_function_by_address(prog, addr, &name, &obj);
      err != nullptr) {
    LOG(ERROR) << "Failed to lookup function '" << reinterpret_cast<void*>(addr)
               << "': " << err->code << " " << err->message;
    drgn_error_destroy(err);
    return std::nullopt;
  }

  auto type = drgn_object_qualified_type(&obj);
  drgn_object_deinit(&obj);
  return type;
}

/**
 * Resolve a symbol to its location in the target ELF binary.
 *
 * @param[in] symName - symbol to resolve
 * @return - A std::optional with the symbol's information
 */
std::optional<SymbolInfo> SymbolService::locateSymbol(
    const std::string& symName, bool demangle) {
  ModParams m = {.symName = symName,
                 .sym = {},
                 .value = 0,
                 .exeAddrs = executableAddrs,
                 .demangle = demangle};

  dwfl_getmodules(dwfl, moduleCallback, (void*)&m, 0);

  if (m.value == 0) {
    return std::nullopt;
  }
  return SymbolInfo{m.value, m.sym.st_size};
}

static std::string bytesToHexString(const unsigned char* bytes, int nbbytes) {
  static const char characters[] = "0123456789abcdef";

  std::string ret(nbbytes * 2, 0);

  for (int i = 0; i < nbbytes; ++i) {
    ret[2 * i] = characters[bytes[i] >> 4];
    ret[2 * i + 1] = characters[bytes[i] & 0x0F];
  }

  return ret;
}

/**
 * Callback for dwfl_getmodules(). For the provided module we lookup
 * its build ID and pass it back via the 'arg' parameter.
 * We expect the target program to always be the first module passed
 * to this callback. So we always return DWARF_CB_ABORT, as this is
 * the only build ID we are interested in.
 */
static int buildIDCallback(Dwfl_Module* mod,
                           void** /* userData */,
                           const char* name,
                           Dwarf_Addr /* start */,
                           void* arg) {
  auto* buildID = static_cast<std::optional<std::string>*>(arg);

  // We must call dwfl_module_getelf before using dwfl_module_build_id
  GElf_Addr bias = 0;
  Elf* elf = dwfl_module_getelf(mod, &bias);
  if (elf == nullptr) {
    LOG(ERROR) << "Failed to getelf for " << name << ": " << dwfl_errmsg(-1);
    return DWARF_CB_ABORT;
  }

  GElf_Addr vaddr = 0;
  const unsigned char* bytes = nullptr;

  int nbbytes = dwfl_module_build_id(mod, &bytes, &vaddr);
  if (nbbytes <= 0) {
    *buildID = std::nullopt;
    LOG(ERROR) << "Build ID not found for " << name;
  } else {
    *buildID = bytesToHexString(bytes, nbbytes);
    VLOG(1) << "Build ID lookup successful for " << name << ": "
            << buildID->value();
  }

  return DWARF_CB_ABORT;
}

std::optional<std::string> SymbolService::locateBuildID() {
  std::optional<std::string> buildID;
  dwfl_getmodules(dwfl, buildIDCallback, (void*)&buildID, 0);

  return buildID;
}

struct drgn_program* SymbolService::getDrgnProgram() {
  if (hardDisableDrgn) {
    LOG(ERROR) << "drgn is disabled, refusing to initialize";
    return nullptr;
  }

  if (prog != nullptr) {
    return prog;
  }

  LOG(INFO) << "Initialising drgn. This might take a while";
  switch (target.index()) {
    case 0: {
      if (auto* err = drgn_program_from_pid(std::get<pid_t>(target), &prog)) {
        LOG(ERROR) << "Failed to initialize drgn: " << err->code << " "
                   << err->message;
        return nullptr;
      }
      auto executable = fs::read_symlink(
          "/proc/" + std::to_string(std::get<pid_t>(target)) + "/exe");
      const auto* executableCStr = executable.c_str();
      if (auto* err = drgn_program_load_debug_info(
              prog, &executableCStr, 1, false, false)) {
        LOG(ERROR) << "Error loading debug info: " << err->message;
        return nullptr;
      }
      break;
    }
    case 1: {
      if (auto* err = drgn_program_create(nullptr, &prog)) {
        LOG(ERROR) << "Failed to create empty drgn program: " << err->code
                   << " " << err->message;
        return nullptr;
      }

      const char* path = std::get<fs::path>(target).c_str();
      if (auto* err =
              drgn_program_load_debug_info(prog, &path, 1, false, false)) {
        LOG(ERROR) << "Failed to read debug info: " << err->code << " "
                   << err->message;
        drgn_program_destroy(prog);

        prog = nullptr;
        return prog;
      }

      LOG(INFO) << "Successfully read debug info";
      break;
    }
  }

  return prog;
}

/*
 * Although 'parseFormalParam' has an all-encompassing sounding name, its sole
 * task is to extract the location information for this parameter if any exist.
 */
static void parseFormalParam(Dwarf_Die& param,
                             struct drgn_elf_file* file,
                             struct drgn_program* prog,
                             Dwarf_Die& funcDie,
                             std::shared_ptr<FuncDesc>& fd) {
  /*
   * NOTE: It is vital that the function descriptors list of arguments
   * are in order and that an entry exists for each argument position
   * even if something goes wrong here when extracting the formal parameter.
   * We *must* pay careful attention to that especially when introducing
   * any new error handling.
   */
  auto farg = fd->addArgument();
  auto* err =
      drgn_object_locator_init(prog, file, &funcDie, &param, &farg->locator);
  if (err) {
    LOG(ERROR) << "Could not initialize drgn_object_locator for parameter: "
               << err->code << ", " << err->message;
    farg->valid = false;
    return;
  }

  const char* name = nullptr;
  Dwarf_Attribute attr;
  if (dwarf_attr_integrate(&param, DW_AT_name, &attr)) {
    if (!(name = dwarf_formstring(&attr))) {
      LOG(ERROR) << "DW_AT_name exists but no name extracted";
    }
  } else {
    VLOG(1) << "Parameter has no DW_AT_name attribute!";
  }

  if (name && !strcmp(name, "this")) {
    VLOG(1) << "'this' pointer found";
    fd->isMethod = true;
  }

  farg->typeName =
      SymbolService::getTypeName(farg->locator.qualified_type.type);
  VLOG(1) << "Type of argument '" << name << "': " << farg->typeName;

  farg->valid = true;
  VLOG(1) << "Adding function arg address: " << farg;
}

/*
static bool handleInlinedFunction(const irequest& request,
                                  std::shared_ptr<FuncDesc> funcDesc,
                                  struct drgn_qualified_type& funcType,
                                  Dwarf_Die& funcDie,
                                  struct drgn_module*& module) {
  VLOG(1) << "Function '" << funcDesc->symName << "' has been inlined";
  struct drgn_type_inlined_instances_iterator* iter = nullptr;
  auto* err = drgn_type_inlined_instances_iterator_init(funcType.type, &iter);
  if (err) {
    LOG(ERROR) << "Error creating inlined instances iterator: " << err->message;
    return false;
  }
  if (strcmp(drgn_type_parameters(funcType.type)[0].name, "this") == 0) {
    funcDesc->isMethod = true;
  }
  auto index = funcDesc->getArgumentIndex(request.arg, false);
  if (!index.has_value()) {
    return false;
  }
  auto* argumentName = drgn_type_parameters(funcType.type)[index.value()].name;
  struct drgn_type* inlinedInstance = nullptr;
  bool foundInstance = false;
  // The index at which the parameter was actually found in the inlined
  // instance. This may differ from the index of the parameter in the function
  // definition, as oftentimes as the result of compiler optimizations, some
  // parameters will be omitted altogether from inlined instances.
  size_t foundIndex = 0;
  while (!foundInstance) {
    err = drgn_type_inlined_instances_iterator_next(iter, &inlinedInstance);
    if (err) {
      LOG(ERROR) << "Error advancing inlined instances iterator: "
                 << err->message;
      return false;
    }
    if (!inlinedInstance) {
      LOG(ERROR) << "Could not find an inlined instance of this function "
                    "with the argument '"
                 << argumentName << "'";
      return false;
    }

    auto numParameters = drgn_type_num_parameters(inlinedInstance);
    auto* parameters = drgn_type_parameters(inlinedInstance);
    for (size_t i = 0; i < numParameters; i++) {
      if (strcmp(argumentName, parameters[i].name) == 0) {
        foundInstance = true;
        foundIndex = i;
        break;
      }
    }
  }

  if (foundIndex != index) {
    // We patch the parameters of `inlinedInstance` such that
    // each parameter is found at the index one would expect from
    // the function definition, matching the representation of the
    // abstract root.
    auto targetParameter = drgn_type_parameters(inlinedInstance)[foundIndex];
    inlinedInstance->_private.num_parameters =
        drgn_type_num_parameters(funcType.type);
    // Allocating with `calloc` since `drgn` manages the lifetimes of its
    // own structures, and it is written in C.
    inlinedInstance->_private.parameters = (struct drgn_type_parameter*)calloc(
        inlinedInstance->_private.num_parameters,
        sizeof(*inlinedInstance->_private.parameters));
    inlinedInstance->_private.parameters[index.value()] = targetParameter;
  }

  err = drgn_type_dwarf_die(inlinedInstance, &funcDie);
  if (err) {
    LOG(ERROR) << "Error obtaining DWARF DIE from type: " << err->message;
    return false;
  }
  funcType.type = inlinedInstance;
  module = inlinedInstance->_private.module;
  return true;
}
*/

static std::optional<std::shared_ptr<FuncDesc>> createFuncDesc(
    struct drgn_program* prog, const irequest& request) {
  VLOG(1) << "Creating function description for: " << request.func;

  auto ft = SymbolService::findTypeOfSymbol(prog, request.func);
  if (!ft) {
    return std::nullopt;
  }

  if (drgn_type_kind(ft->type) != DRGN_TYPE_FUNCTION) {
    LOG(ERROR) << "Type corresponding to symbol '" << request.func
               << "' is not a function";
    return std::nullopt;
  }

  auto fd = std::make_shared<FuncDesc>(request.func);

  drgn_elf_file* file = ft->type->_private.file;
  Dwarf_Die funcDie;
  if (auto* err = drgn_type_dwarf_die(ft->type, &funcDie); err != nullptr) {
    LOG(ERROR) << "Error obtaining DWARF DIE from type: " << err->message;
    return std::nullopt;
  }

  if (dwarf_func_inline(&funcDie) == 1) {
    // if (!handleInlinedFunction(request, fd, *ft, funcDie, module)) {
    //   return std::nullopt;
    // }
    LOG(ERROR) << "inlined functions are not supported";
    return std::nullopt;
  }

  ptrdiff_t offset = 0;
  uintptr_t base = 0;
  uintptr_t start = 0;
  uintptr_t end = 0;

  while ((offset = dwarf_ranges(&funcDie, offset, &base, &start, &end)) > 0) {
    fd->ranges.emplace_back(start, end);
  }

  if (offset < 0) {
    LOG(ERROR) << "Error while finding ranges of function: "
               << dwarf_errmsg(dwarf_errno());
    return std::nullopt;
  }

  auto retType = drgn_type_type(ft->type);
  auto retTypeName = SymbolService::getTypeName(retType.type);
  VLOG(1) << "Retval has type: " << retTypeName;

  if (!retTypeName.empty() && retTypeName != "void") {
    /*
     * I really can't figure out at the minute how to deduce from the DWARF
     * which register is used for the return value. I don't think we can just
     * assume it's 'rax' as according to the AMD64 ABI V1.0 Section 12.1.3 we
     * can use 'rax', 'rdi, and I think it may be more complex than that. More
     * investigation required.
     * Moreover, we must fabricate a pointer type to the return type for the
     * locator code to properly interpret the register's content. This WILL
     * break for return-by-value instead of return-by-reference. But this kind
     * of assumption is in-line we what we need to improve about return-value
     * locating, so this will be good-enough for now.
     *
     * For now, fabricate a 'Retval' object for rax.
     */
    fd->retval = std::make_shared<FuncDesc::Retval>();
    fd->retval->typeName = std::move(retTypeName);
    fd->retval->valid = true;
  }

  // Add params
  bool isVariadic = false;
  fd->arguments.reserve(drgn_type_num_parameters(ft->type));
  Dwarf_Die child;
  int r = dwarf_child(&funcDie, &child);

  while (r == 0) {
    switch (dwarf_tag(&child)) {
      case DW_TAG_formal_parameter:
        if (isVariadic) {
          LOG(WARNING) << "Formal parameter after unspecified "
                          "parameters tag!";
        }

        parseFormalParam(child, file, prog, funcDie, fd);
        break;

      case DW_TAG_unspecified_parameters:
        if (isVariadic) {
          VLOG(1) << "Multiple variadic parameters!";
        }
        VLOG(1) << "Unspecified parameters tag";
        isVariadic = true;
        break;

      default:
        break;
    }
    r = dwarf_siblingof(&child, &child);
  }

  if (r == -1) {
    LOG(ERROR) << "Couldn't parse DIE children";
  }

  return fd;
}

/*
 * Locate the function descriptor from the function descriptor cache or create
 * one if it doesn't exist. Just take the
 * up front hit of looking everything up now.
 */
std::shared_ptr<FuncDesc> SymbolService::findFuncDesc(const irequest& request) {
  if (auto it = funcDescs.find(request.func); it != end(funcDescs)) {
    VLOG(1) << "Found funcDesc for " << request.func;
    return it->second;
  }

  struct drgn_program* drgnProg = getDrgnProgram();
  if (drgnProg == nullptr) {
    return nullptr;
  }

  auto fd = createFuncDesc(drgnProg, request);
  if (!fd.has_value()) {
    LOG(ERROR) << "Failed to create FuncDesc for " << request.func;
    return nullptr;
  }

  VLOG(1) << "findFuncDesc returning " << std::hex << fd.value()->symName;
  funcDescs.emplace(request.func, fd.value());
  return fd.value();
}

std::shared_ptr<GlobalDesc> SymbolService::findGlobalDesc(
    const std::string& global) {
  if (auto it = globalDescs.find(global); it != end(globalDescs)) {
    VLOG(1) << "Found globalDesc for " << global;
    return it->second;
  }

  auto sym = locateSymbol(global);
  if (!sym.has_value()) {
    LOG(ERROR) << "Failed to get address for global " << global;
    return nullptr;
  }

  VLOG(1) << "locateGlobal: address of " << global << " " << std::hex
          << sym->addr;

  struct drgn_program* drgnProg = getDrgnProgram();
  if (drgnProg == nullptr) {
    return nullptr;
  }

  auto gd = std::make_shared<GlobalDesc>(global, sym->addr);

  struct drgn_object globalObj {};
  drgn_object_init(&globalObj, drgnProg);
  BOOST_SCOPE_EXIT_ALL(&) {
    drgn_object_deinit(&globalObj);
  };

  if (auto* err = drgn_program_find_object(drgnProg,
                                           global.c_str(),
                                           nullptr,
                                           DRGN_FIND_OBJECT_ANY,
                                           &globalObj)) {
    LOG(ERROR) << "Failed to lookup global variable '" << global
               << "': " << err->code << " " << err->message;

    return nullptr;
  }

  auto globalType = drgn_object_qualified_type(&globalObj);
  gd->typeName = getTypeName(globalType.type);

  VLOG(1) << "findGlobalDesc returning " << std::hex << gd;
  globalDescs.emplace(global, gd);
  return gd;
}

std::string SymbolService::getTypeName(struct drgn_type* type) {
  if (drgn_type_kind(type) == DRGN_TYPE_POINTER) {
    type = drgn_type_type(type).type;
  }
  return drgn_utils::typeToName(type);
}

std::optional<RootInfo> SymbolService::getRootType(const irequest& req) {
  if (req.type == "global") {
    /*
     * This is super simple as all we have to do is locate assign the
     * type of the provided global variable.
     */
    VLOG(1) << "Processing global: " << req.func;

    auto globalDesc = findGlobalDesc(req.func);
    if (!globalDesc) {
      return std::nullopt;
    }

    auto* drgnProg = getDrgnProgram();
    if (drgnProg == nullptr) {
      return std::nullopt;
    }

    drgn_object global{};
    drgn_object_init(&global, drgnProg);
    if (auto* err = drgn_program_find_object(
            drgnProg, req.func.c_str(), nullptr, DRGN_FIND_OBJECT_ANY, &global);
        err != nullptr) {
      LOG(ERROR) << "Failed to lookup global variable '" << req.func
                 << "': " << err->code << " " << err->message;
      drgn_error_destroy(err);
      return std::nullopt;
    }

    return RootInfo{req.func, drgn_object_qualified_type(&global)};
  }

  VLOG(1) << "Passing : " << req.func;
  auto fd = findFuncDesc(req);
  if (!fd) {
    VLOG(1) << "Failed to lookup function " << req.func;
    return std::nullopt;
  }

  // TODO: We are dealing with demangled names for drgn. drgn seems to store
  // function names without parameters however. So strip parameters from
  // demangled function name before passing to drgn.
  // auto tmp = boost::core::demangle(req->func.c_str());
  // auto demangledName = tmp.substr(0, tmp.find("("));

  auto* drgnProg = getDrgnProgram();
  if (drgnProg == nullptr) {
    return std::nullopt;
  }

  auto ft = findTypeOfSymbol(drgnProg, req.func);
  if (!ft) {
    return std::nullopt;
  }

  if (req.isReturnRetVal()) {
    VLOG(1) << "Processing return retval";
    return RootInfo{std::string("return"), drgn_type_type(ft->type)};
  }

  if (!drgn_type_has_parameters(ft->type)) {
    LOG(ERROR) << "Error: Object is not function?";
    return std::nullopt;
  }

  auto* params = drgn_type_parameters(ft->type);
  auto paramsCount = drgn_type_num_parameters(ft->type);
  if (paramsCount == 0) {
    LOG(ERROR) << "Function " << req.func << " has no parameters";
    return std::nullopt;
  }

  auto argIdxOpt = fd->getArgumentIndex(req.arg);
  if (!argIdxOpt.has_value()) {
    return std::nullopt;
  }

  uint8_t argIdx = argIdxOpt.value();

  /*
   * The function descriptor has a fully populated argument vector so
   * check that we have a valid argument decriptor for the requested arg.
   * Most likely reason for it being invalid is if the DWARF formal parameter
   * contains no location information.
   */
  if (!fd->arguments[argIdx]->valid) {
    LOG(ERROR) << "Argument " << argIdx << " for " << fd->symName
               << " is invalid";
    return std::nullopt;
  }

  drgn_qualified_type paramType{};
  if (auto* err = drgn_parameter_type(&params[argIdx], &paramType);
      err != nullptr) {
    LOG(ERROR) << "Failed to get params: " << err->code << " " << err->message;
    drgn_error_destroy(err);
    return std::nullopt;
  }

  std::string paramName;
  if (params[argIdx].name) {
    VLOG(1) << "ARG NAME: " << params[argIdx].name;
    paramName = params[argIdx].name;
  }

  return RootInfo{paramName, paramType};
}

}  // namespace oi::detail
