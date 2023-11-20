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
#include "oi/OIDebugger.h"

#include <folly/Varint.h>

#include <algorithm>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/format.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/scope_exit.hpp>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <span>

extern "C" {
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/uio.h>
#include <sys/user.h>
#include <sys/wait.h>
}

#include <glog/logging.h>

#include "oi/CodeGen.h"
#include "oi/Config.h"
#include "oi/ContainerInfo.h"
#include "oi/Headers.h"
#include "oi/Metrics.h"
#include "oi/OILexer.h"
#include "oi/PaddingHunter.h"
#include "oi/Portability.h"
#include "oi/Syscall.h"
#include "oi/type_graph/DrgnParser.h"
#include "oi/type_graph/TypeGraph.h"

#if OI_PORTABILITY_META_INTERNAL()
#include "object-introspection/internal/GobsService.h"
#endif

using namespace std;

namespace oi::detail {

constexpr int oidMagicId = 0x01DE8;

bool OIDebugger::isGlobalDataProbeEnabled(void) const {
  return std::any_of(cbegin(pdata), cend(pdata), [](const auto& r) {
    return r.type == "global";
  });
}

bool OIDebugger::parseScript(std::istream& script) {
  metrics::Tracing _("parse_script");

  OIScanner scanner(&script);
  OIParser parser(scanner, pdata);

#if 0
  if (VLOG_IS_ON(1)) {
    scanner.set_debug(1);
    parser.set_debug_level(1);
  }
#endif

  if (parser.parse() != 0) {
    return false;
  }

  if (pdata.numReqs() == 0) {
    LOG(ERROR) << "No valid introspection data specified";
    return false;
  }

  return true;
}

bool OIDebugger::patchFunctions(void) {
  assert(pdata.numReqs() != 0);
  metrics::Tracing _("patch_functions");

  for (const auto& preq : pdata) {
    VLOG(1) << "Type " << preq.type << " Func " << preq.func
            << " Args: " << boost::join(preq.args, ",");

    if (preq.type == "global") {
      /*
       * processGlobal() - this function should do everything apart from
       * continue the target thread.
       */
      processGlobal(preq.func);
    } else {
      if (!functionPatch(preq)) {
        LOG(ERROR) << "Failed to patch function";
        return false;
      }
    }
  }

  return true;
}

/*
 * Single step an instruction in the target process 'pid' and leave the target
 * thread stopped. Returns the current rip.
 */
uint64_t OIDebugger::singlestepInst(pid_t pid, struct user_regs_struct& regs) {
  int status = 0;

  metrics::Tracing _("single_step_inst");

  if (ptrace(PTRACE_SINGLESTEP, pid, 0, 0) == -1) {
    LOG(ERROR) << "Error in singlestep!";
    return 0xdeadbeef;
  }

  /* Error check... */
  waitpid(pid, &status, 0);

  if (!WIFSTOPPED(status)) {
    LOG(ERROR) << "process not stopped!";
  }

  errno = 0;
  if (ptrace(PTRACE_GETREGS, pid, NULL, &regs) < 0) {
    LOG(ERROR) << "Couldn't read registers: " << strerror(errno);
  }
  VLOG(1) << "rip after singlestep: " << std::hex << regs.rip;

  return regs.rip;
}

void OIDebugger::dumpRegs(const char* text,
                          pid_t pid,
                          struct user_regs_struct* regs) {
  VLOG(1) << "(" << text << ")"
          << " dumpRegs: pid: " << std::dec << pid << std::hex << " rip "
          << regs->rip << " rbp: " << regs->rbp << " rsp " << regs->rsp
          << " rdi " << regs->rdi << " rsi " << regs->rsi << " rdx "
          << regs->rdx << " rbx " << regs->rbx << " rcx " << regs->rcx << " r8 "
          << regs->r8 << " r9 " << regs->r9 << " r10 " << regs->r10 << " r11 "
          << regs->r11 << " r12 " << regs->r12 << " r13 " << regs->r13
          << " r14 " << regs->r14 << " r15 " << regs->r15 << " rax "
          << regs->rax << " orig_rax " << regs->orig_rax << " eflags "
          << regs->eflags;
}

/*
 * singlestepFunc is a debugging aid for when times get desperate! It will
 * single step the tgid specified in pid until it reached the address
 * specified in 'real_end'. This means you'll get a register dump for
 * every instruction which can be extremely useful for getting to those
 * hard to reach places.
 *
 * Note that while this will single step a function nicely, it will also
 * caue the target thread to spin forever and therefore fail the process
 * because of the way we use breakpoint traps . It needs improving.
 */
bool OIDebugger::singleStepFunc(pid_t pid, uint64_t real_end) {
  uint64_t addr = 0x0;
  struct user_regs_struct regs {};
  uint64_t prev = 0;

  do {
    prev = addr;
    VLOG(1) << "singlestepFunc addr: " << std::hex << addr << " real_end "
            << real_end;
    addr = singlestepInst(pid, regs);
    dumpRegs("singlestep", pid, &regs);
  } while (addr != 0xdeadbeef && addr != real_end && prev != addr);

  return true;
}

bool OIDebugger::setupLogFile(void) {
  // 1. Open an anonymous memfd in the target with `memfd_create`.
  // 2. Duplicate that fd to the debugger using `pidfd_getfd`.
  // 3. Store the resulting fds in OIDebugger.
  bool ret = true;

  auto traceeFd = remoteSyscall<MemfdCreate>("jit.log", 0);
  if (!traceeFd.has_value()) {
    LOG(ERROR) << "Failed to create memory log file";
    return false;
  }
  logFds.traceeFd = *traceeFd;

  auto traceePidFd = syscall(SYS_pidfd_open, traceePid, 0);
  if (traceePidFd == -1) {
    PLOG(ERROR) << "Failed to open child pidfd";
    return false;
  }
  auto debuggerFd = syscall(SYS_pidfd_getfd, traceePidFd, *traceeFd, 0);
  if (close(static_cast<int>(traceePidFd)) != 0) {
    PLOG(ERROR) << "Failed to close pidfd";
    ret = false;
  }
  if (debuggerFd == -1) {
    PLOG(ERROR) << "Failed to duplicate child memfd to debugger";
    return false;
  }

  logFds.debuggerFd = static_cast<int>(debuggerFd);
  return ret;
}

bool OIDebugger::cleanupLogFile(void) {
  bool ret = true;
  if (logFds.traceeFd == -1)
    return ret;

  if (!remoteSyscall<SysClose>(logFds.traceeFd).has_value()) {
    LOG(ERROR) << "Remote close failed";
    ret = false;
  }

  if (logFds.debuggerFd == -1)
    return ret;

  FILE* logs = fdopen(logFds.debuggerFd, "r");
  if (logs == NULL) {
    PLOG(ERROR) << "Failed to fdopen jitlog";
    return false;
  }
  if (fseek(logs, 0, SEEK_SET) != 0) {
    PLOG(ERROR) << "Failed to fseek jitlog";
    return false;
  }

  char* line = nullptr;
  size_t read = 0;
  VLOG(1) << "Outputting JIT logs:";
  errno = 0;
  while ((read = getline(&line, &read, logs)) != (size_t)-1) {
    VLOG(1) << "JITLOG: " << line;
  }
  if (errno) {
    PLOG(ERROR) << "getline";
    return false;
  }
  VLOG(1) << "Finished outputting JIT logs.";

  free(line);
  if (fclose(logs) == -1) {
    PLOG(ERROR) << "fclose";
    return false;
  }

  return ret;
}

/* Set up traced process results and text segments */
bool OIDebugger::segmentInit(void) {
  if (generatorConfig.features[Feature::JitLogging]) {
    if (!setupLogFile()) {
      LOG(ERROR) << "setUpLogFile failed!!!";
      return false;
    }
  }

  /*
   * TODO: change this. If setup_results_segment() fails we have to remove
   * the text segment.
   */
  if (!segConfig.existingConfig || segConfig.dataSegSize != dataSegSize) {
    if (!segConfig.existingConfig) {
      if (!setupSegment(SegType::text) || !setupSegment(SegType::data)) {
        LOG(ERROR) << "setUpSegment failed!!!";
        return false;
      }
    } else {
      if (!unmapSegment(SegType::data)) {
        LOG(ERROR) << "Failed to unmmap data segment";
        return false;
      }
      if (!setupSegment(SegType::data)) {
        LOG(ERROR) << "Failed to setup data segment";
        return false;
      }
    }

    segConfig.existingConfig = true;
    segmentConfigFile.seekg(0);
    segmentConfigFile.write((char*)&segConfig, sizeof(segConfig));

    VLOG(1) << "segConfig size " << sizeof(segConfig);

    if (segmentConfigFile.fail()) {
      LOG(ERROR) << "init: error in writing configFile" << segConfigFilePath
                 << strerror(errno);
    }
    VLOG(1) << "About to flush segment config file";
    segmentConfigFile.flush();
  }

  // Using nanoseconds since epoch as the cookie value
  using namespace std::chrono;
  auto now = high_resolution_clock::now().time_since_epoch();
  segConfig.cookie = duration_cast<duration<uintptr_t, std::nano>>(now).count();

  return true;
}

/*
 * Temporary config file with settings for this debugging "session". The
 * notion of "session" is a bit fuzzy at the minute.
 */
void OIDebugger::createSegmentConfigFile(void) {
  /*
   * For now we'll just use the pid of the traced process to name the temp
   * file so we can find it
   */
  assert(traceePid != 0);

  segConfigFilePath = fs::temp_directory_path() /
                      ("oid-segconfig-" + std::to_string(traceePid));

  if (!fs::exists(segConfigFilePath) || fs::file_size(segConfigFilePath) == 0) {
    segmentConfigFile.open(segConfigFilePath,
                           ios::in | ios::out | ios::binary | ios::trunc);

    if (!segmentConfigFile.is_open()) {
      LOG(ERROR) << "Failed to open " << segConfigFilePath << " : "
                 << strerror(errno);
    }

    VLOG(1) << "createSegmentConfigFile: " << segConfigFilePath << " created";
  } else {
    VLOG(1) << "createSegmentConfigFile: config file " << segConfigFilePath
            << " already exists";

    /* Read config */
    segmentConfigFile =
        std::fstream(segConfigFilePath, ios::in | ios::out | ios::binary);
    segmentConfigFile.read((char*)&segConfig, sizeof(c));

    if (segmentConfigFile.fail()) {
      LOG(ERROR) << "createSegmentConfigFile: failed to read from "
                    "existing config file"
                 << strerror(errno);
    }

    VLOG(1) << "Existing Config File read: " << std::hex
            << " textSegBase: " << segConfig.textSegBase
            << " textSegSize: " << segConfig.textSegSize
            << " jitCodeStart: " << segConfig.jitCodeStart
            << " existingConfig: " << segConfig.existingConfig
            << " dataSegBase: " << segConfig.dataSegBase
            << " dataSegSize: " << segConfig.dataSegSize
            << " replayInstBase: " << segConfig.replayInstBase
            << " cookie: " << segConfig.cookie;

    assert(segConfig.existingConfig);
  }
}

void OIDebugger::deleteSegmentConfig(bool deleteSegConfigFile) {
  if (!segConfig.existingConfig) {
    return;
  }

  if (deleteSegConfigFile) {
    std::error_code ec;
    if (!fs::remove(segConfigFilePath, ec)) {
      VLOG(1) << "Failed to remove segment config file "
              << segConfigFilePath.c_str() << " value: " << ec.value()
              << " message: " << ec.message();
    }
  }

  segConfig.textSegBase = 0;
  segConfig.textSegSize = 0;
  segConfig.jitCodeStart = 0;
  segConfig.replayInstBase = 0;
  segConfig.existingConfig = false;
  segConfig.dataSegBase = 0;
  segConfig.dataSegSize = 0;
  segConfig.cookie = 0;
}

/*
 * C++ enums are terrible, we know. There are a number of bodges that can make
 * mapping enums values to strings more C++'esque * (read that as 'more
 * grotesque') but this is the simple approach.
 */
std::string OIDebugger::taskStateToString(OIDebugger::StatusType status) {
  /* Must reflect the order of OIDebugger::StatusType enum */
  static const std::array enumMapping{"SLEEP",
                                      "TRACED",
                                      "RUNNING",
                                      "ZOMBIE",
                                      "DEAD",
                                      "DISK SLEEP",
                                      "STOPPED",
                                      "OTHER",
                                      "BAD"};

  return enumMapping[static_cast<int>(status)];
}

OIDebugger::StatusType OIDebugger::getTaskState(pid_t pid) {
  /*
   * We often need to know (primarily for debugging) what the contents
   * a tasks /proc/<tid>/status "State" field is set to so we know if it
   * is stopped, sleeping etc. Obviously, this is racy but is better than
   * nothing.
   */
  auto path = fs::path("/proc") / std::to_string(pid) / "stat";
  std::ifstream stat{path};
  if (!stat) {
    return StatusType::bad;
  }

  // Ignore pid and comm
  stat.ignore(std::numeric_limits<std::streamsize>::max(), ' ');
  stat.ignore(std::numeric_limits<std::streamsize>::max(), ' ');

  char state = '\0';
  stat >> state;

  /* XXX Need to add other states */
  OIDebugger::StatusType ret = StatusType::other;
  if (state == 'S') {
    ret = StatusType::sleep;
  } else if (state == 't') {
    ret = StatusType::traced;
  } else if (state == 'R') {
    ret = StatusType::running;
  } else if (state == 'X') {
    ret = StatusType::dead;
  } else if (state == 'Z') {
    ret = StatusType::zombie;
  } else if (state == 'D') {
    ret = StatusType::diskSleep;
  } else if (state == 'T') {
    ret = StatusType::stopped;
  }

  return ret;
}

/* For debug - do not remove */
void OIDebugger::dumpAlltaskStates(void) {
  VLOG(1) << "Task State Dump";
  for (auto const& p : threadList) {
    auto state = getTaskState(p);
    VLOG(1) << "Task " << p << " state: " << taskStateToString(state) << " ("
            << static_cast<int>(state) << ")";
  }
}

int OIDebugger::getExtendedWaitEventType(int status) {
  return (status >> 16);
}

bool OIDebugger::isExtendedWait(int status) {
  return (getExtendedWaitEventType(status) != 0);
}

/*
 * Continue the main traced thread either by a detach request (default)
 * or via a continue. No provision is made for passing a signal.
 */
bool OIDebugger::contTargetThread(bool detach) const {
  VLOG(4) << "contTargetThread: About to " << (detach ? "detach" : "continue")
          << " pid " << std::dec << traceePid;

  if (!detach) {
    return contTargetThread(traceePid);
  }

  errno = 0;
  if ((ptrace(PTRACE_DETACH, traceePid, nullptr, nullptr)) < 0) {
    LOG(ERROR) << "contTargetThread failed to detach pid " << traceePid << " "
               << strerror(errno) << "(" << errno << ")";
    return false;
  }

  return true;
}

/*
 * Continue the thread specified in 'targetPid' passing an optional
 * signal via the optional 'data' parameter. The ptrace(2) data parameter
 * is a 'void*' but used for many purposes. Here the sole pupose in detach
 * or continue is to pass a signal value for the target thread and although
 * it's not explicitly stated in the man page, a value of 0 (default) is
 * treated as no signal passed.
 */
bool OIDebugger::contTargetThread(pid_t targetPid, unsigned long data) const {
  bool ret = true;

  VLOG(4) << "contTargetThread: About to continue pid " << std::dec
          << targetPid;

  errno = 0;
  if ((ptrace(PTRACE_CONT, targetPid, nullptr, data)) < 0) {
    LOG(ERROR) << "contTargetThread failed to continue pid " << targetPid << " "
               << strerror(errno) << "(" << errno << ")";
    ret = false;
  }

  return ret;
}

bool OIDebugger::replayTrappedInstr(const trapInfo& t,
                                    pid_t pid,
                                    struct user_regs_struct& regs,
                                    struct user_fpregs_struct& fpregs) const {
  /*
   * Single step the original instruction which has been patched over
   * with a breakpoint trap. The original instruction now resides in
   * our private text mapping in the target process (see
   * getReplayInstrAddr())
   */
  auto origRip = std::exchange(regs.rip, t.replayInstAddr);

  errno = 0;
  if (ptrace(PTRACE_SETREGS, pid, nullptr, &regs) < 0) {
    LOG(ERROR) << "Execute: Couldn't restore registers: " << strerror(errno);
  }

  errno = 0;
  if (ptrace(PTRACE_SETFPREGS, pid, nullptr, &fpregs) < 0) {
    LOG(ERROR) << "Execute: Couldn't restore fp registers: " << strerror(errno);
  }

  uintptr_t rip = singlestepInst(pid, regs);

  /*
   * On entry to this function the current threads %rip is pointing straight
   * after the int3 instruction which is a single byte opcode. The
   * actual instruction that this is patched over will most likely be
   * larger than that (e.g., a 'push <reg>' is 2 bytes) and therefore
   * we need to set the return %rip to point to the next instruction after
   * that (we have just single stepped it). This assumes that the patched
   * instruction at entry can't be a CTI which I'm not sure is valid. The
   * assert() below will hopefully catch that though.
   */

  /*
   * Hmm. Not sure what to do here. If the instruction is a control transfer
   * instruction (CTI) (e.g., jmp, ret, call) then we need to just go with
   * the rip that results from the single step. If it isn't a CTI then I
   * think we should just go with the original rip. Sounds OK on the surface
   * but not sure it is correct for all scenarios.
   *
   * The disassembler can probably tell us if this is a CTI so probably a
   * cleaner solution. For now we'll just see if the current rip is
   * outside of t->replayInstAddr + MAX_INST_SIZE then we assume a CTI.
   */

  if (rip > t.replayInstAddr && rip < t.replayInstAddr + 16) {
    /* See comment above in processFuncEntry() for this */
    VLOG(4) << "Hit rip adjustment code in replayTrappedInstr";
    size_t origInstSize = rip - t.replayInstAddr;
    /*
     * x64 instructions can be 16 bytes but we only use 8 bytes of patch. I
     * think things will have gone bad already but put this check here in case
     * we are lucky enough to make it here with such an instruction.
     */
    assert(origInstSize <= 8);
    regs.rip = origRip - sizeofInt3 + origInstSize;
  }

  errno = 0;
  if (ptrace(PTRACE_SETREGS, pid, NULL, &regs) < 0) {
    LOG(ERROR) << "Execute: Couldn't restore registers: " << strerror(errno);
  }

  return true;
}

bool OIDebugger::locateObjectsAddresses(const trapInfo& tInfo,
                                        struct user_regs_struct& regs) {
  /*
   * Write objects into prologue in target.
   */
  bool ret = true;
  for (const auto& arg : tInfo.args) {
    auto remoteObjAddr = remoteObjAddrs.find(arg);
    if (remoteObjAddr == remoteObjAddrs.end()) {
      LOG(ERROR) << "Entry: failed to find remoteObjAddr! Skipping...";
      ret = false;
      continue;
    }

    auto addr = arg->findAddress(&regs, tInfo.trapAddr);
    if (!addr.has_value()) {
      LOG(ERROR) << "Entry: failed to locate arg! Skipping...";
      ret = false;
      continue;
    }

    VLOG(4) << "Entry: arg addr: " << std::hex << *addr;
    if (!writeTargetMemory((void*)(&addr.value()),
                           (void*)remoteObjAddr->second,
                           sizeof(*addr))) {
      LOG(ERROR) << "Entry: writeTargetMemory remoteObjAddr failed!";
      ret = false;
      continue;
    }
  }

  return ret;
}

OIDebugger::processTrapRet OIDebugger::processFuncTrap(
    const trapInfo& tInfo,
    pid_t pid,
    struct user_regs_struct& regs,
    struct user_fpregs_struct& fpregs) {
  assert(tInfo.trapKind != OID_TRAP_JITCODERET);

  processTrapRet ret = OID_CONT;

  VLOG(4) << "\nProcess Function Trap for pid=" << pid << ": " << tInfo
          << "\n\n";

  auto t = std::make_shared<trapInfo>(tInfo);

  /* Save interrupted registers into trap information */
  memcpy((void*)&t->savedRegs, (void*)&regs, sizeof(t->savedRegs));

  /* Save fpregs into trap information */
  memcpy((void*)&t->savedFPregs, (void*)&fpregs, sizeof(t->savedFPregs));

  /* Start by locating each Target Object's address */
  if (!locateObjectsAddresses(*t, regs)) {
    LOG(ERROR) << "Failed to locate all objects addresses. Aborting...";
    replayTrappedInstr(*t, pid, t->savedRegs, t->savedFPregs);

    contTargetThread(pid);

    return OIDebugger::OID_ERR;
  }

  /*
   * This is a function return site introspection trap. For now
   * just grab the object address from rax but we will support
   * using entry parameters as well.
   *
   * Return probes need handling slightly differently to entry
   * probes. We may have instrumented multiple return sites and
   * all of them need returning to their original instructions
   * here - not just the one that caused the trap. For now just
   * iterate and locate all OID_TRAP_VECT_ENTRYRET and
   * OID_TRAP_VECT_RET entries and sort them out. This assumes
   * that we only have single enablings which is not the long term
   * strategy but OK for the short term.
   */

  /*
   * If this entry trap needs a corresponding entry trap then we need it
   * should be active so let's search the 'threadTrapState' map for it.
   * It *must* have a corresponding active entry in 'threadTrapState'
   * for this pid.
   *
   * XXX Think on this. We may have many threads executing in a function at
   * one point in time:
   * T: Thread A hits foo() entry site
   * T+1: Thread B hits foo() entry site
   * T+2: Thread B hits foo() return site
   * T+3: Thread A hits foo() return site
   *
   * Currently this would be bad for several reasons but the most important
   * is that we can't have one thread messing with another threads data while
   * it is still manipulating it. We have to ensure we match pairs correctly
   * or at least ensure we are using the correct data when we introspect
   * the argument. This means that we have to get rid of that stupid
   * 'segConfig.remoteObjAddr' mechanism which means the OID_TRAP_VECT_ENTRYRET
   * trap needs to stash the argument address somewhere that the return
   * trap can easily access it. We also need to be able to record data from
   * multiple return threads executing JIT'd code for a given function.
   */

  /* Execute prologue, if the trap kind probes the target objects */
  if (t->trapKind == OID_TRAP_VECT_ENTRYRET) {
    /* EntryRet traps locate Target Objects, but the capture is deferred until
     * return */

    /*
     * If this is an entry point site that is a helper for a return probe
     * site then all we need to do is squirel away the contents of the
     * specified register and execute the patched instruction. The content
     * of the register has already been saved above.
     *
     * First we need to ensure that there exists an associated return trap. If
     * we are inconsistent for some reason then we should do nothing. Should we
     * remove the trap though? If we instrument entry sites before return for
     * argument processing returns then we may be in that window. We would need
     * to ensure that this didn't keep happening though owing to a bug. Think
     * on it.
     */
    t->lifetime.rename("entry_arg");
    replayTrappedInstr(*t, pid, t->savedRegs, t->savedFPregs);
  } else {
    assert(t->trapKind == OID_TRAP_VECT_ENTRY ||
           t->trapKind == OID_TRAP_VECT_RET);

    if (t->trapKind == OID_TRAP_VECT_ENTRY) {
      t->lifetime.rename("entry_jit");
    } else {
      t->lifetime.rename("return_jit");
    }

    /* Execute from the start of the prologue */
    regs.rip = t->prologueObjAddr;
    t->fromVect = true;

    VLOG(4) << "processTrap: redirect pid " << std::dec << pid << " to address "
            << std::hex << (void*)tInfo.prologueObjAddr << " tInfo: " << tInfo
            << " " << tInfo.prologueObjAddr << " " << tInfo.fromVect;

    errno = 0;
    if (ptrace(PTRACE_SETREGS, pid, nullptr, &regs) < 0) {
      LOG(ERROR) << "Execute: Couldn't restore registers: " << strerror(errno);
    }
  }

  VLOG(4) << "Inserting Trapinfo for pid " << std::dec << pid;
  threadTrapState.insert_or_assign(pid, std::move(t));

  contTargetThread(pid);

  VLOG(1) << "Finished Function Trap processing\n";

  return ret;
}

OIDebugger::processTrapRet OIDebugger::processJitCodeRet(
    const trapInfo& tInfo __attribute__((unused)), pid_t pid) {
  OIDebugger::processTrapRet ret = OIDebugger::OID_CONT;

  assert(tInfo.trapKind == OID_TRAP_JITCODERET);

  VLOG(4) << "Process JIT Code Return Trap";

  /*
   * This is a trap from a non-vectored route. If an entry for this thread
   * exists in the threadTrapState map then this trap is the servicing of a
   * previously handled vector OID_TRAP_VECT_ENTRY. If so, redirect the thread
   * back to the original call site. If not, just let into continue.
   */
  if (auto iter = threadTrapState.find(pid);
      iter != std::end(threadTrapState)) {
    /*
     * This is the return path from JIT code. Before we re-vector the target
     * thread back to its original code path we must replay the instruction
     * that has been patched over by the breakpoint trap so that its effects
     * are visible to the target thread.
     */
    auto t{iter->second};
    t->lifetime.stop();

    auto jitTrapProcessTime = metrics::Tracing("jit_ret");

    VLOG(4) << "Hit the return path from vector. Redirect to " << std::hex
            << t->trapAddr;

    /*
     * Global variable handling sits outside the regular scheme and requires
     * a lot less work than other traps.
     */
    if (t->trapAddr != GLOBAL_VARIABLE_TRAP_ADDR) {
      replayTrappedInstr(*t, pid, t->savedRegs, t->savedFPregs);
    } else {
      VLOG(4) << "processJitCodeRet processing global variable return";

      errno = 0;
      if (ptrace(PTRACE_SETREGS, pid, nullptr, &t->savedRegs) < 0) {
        LOG(ERROR) << "Execute: Couldn't restore registers: "
                   << strerror(errno);
      }

      errno = 0;
      if (ptrace(PTRACE_SETFPREGS, pid, nullptr, &t->savedFPregs) < 0) {
        LOG(ERROR) << "Execute: Couldn't restore fp registers: "
                   << strerror(errno);
      }

      if (VLOG_IS_ON(4)) {
        errno = 0;
        struct user_regs_struct newregs {};
        if (ptrace(PTRACE_GETREGS, pid, NULL, &newregs) < 0) {
          LOG(ERROR) << "Execute: Couldn't restore registers: "
                     << strerror(errno);
        }

        dumpRegs("processJitCodeRet global: ", pid, &newregs);
      }
    }

    VLOG(4) << "Erasing Trapinfo for pid " << std::dec << iter->first;
    threadTrapState.erase(iter);

    jitTrapProcessTime.stop();

    contTargetThread(pid);

    if (++count == 1 || isInterrupted()) {
      VLOG(1) << "count: " << count << " oid done";
      ret = OIDebugger::OID_DONE;
    } else {
      ret = OIDebugger::OID_CONT;
    }
  }

  VLOG(1) << "Finished JIT Code Return trap processing\n";

  return ret;
}

/*
 * Although we follow the same naming scheme as for other probe types
 * (e.g., entry/return), this function is never called from trap handling
 * code. Global variables do not rely on a specific entry point to be
 * instrumented but instead we can simply hijack a thread (the main thread
 * in this case) and introspect the global data. It would be good if we had
 * a cheap way of asserting that the global thread is stopped.
 */
bool OIDebugger::processGlobal(const std::string& varName) {
  assert(mode == OID_MODE_THREAD);

  VLOG(1) << "Introspecting global variable: " << varName;

  errno = 0;
  if (ptrace(PTRACE_SYSCALL, traceePid, nullptr, nullptr) < 0) {
    LOG(ERROR) << "Couldn't attach to target pid " << traceePid
               << " (Reason: " << strerror(errno) << ")";
    return false;
  }

  VLOG(1) << "About to wait for process on syscall entry/exit";
  int status = 0;
  waitpid(traceePid, &status, 0);

  if (!WIFSTOPPED(status)) {
    LOG(ERROR) << "process not stopped!";
  }

  errno = 0;
  struct user_regs_struct regs {};
  if (ptrace(PTRACE_GETREGS, traceePid, nullptr, &regs) < 0) {
    LOG(ERROR) << "processGlobal: failed to read registers" << strerror(errno);
    return false;
  }

  errno = 0;
  struct user_fpregs_struct fpregs {};
  if (ptrace(PTRACE_GETFPREGS, traceePid, nullptr, &fpregs) < 0) {
    LOG(ERROR) << "processGlobal: Couldn't get fp registers: "
               << strerror(errno);
  }

  dumpRegs("After syscall stop", traceePid, &regs);

  auto t = std::make_shared<trapInfo>(OID_TRAP_JITCODERET,
                                      GLOBAL_VARIABLE_TRAP_ADDR);
  t->lifetime.rename("global_jit");
  threadTrapState.emplace(traceePid, t);

  regs.rip -= 2;
  /* Save interrupted registers into trap information */
  memcpy((void*)&t->savedRegs, (void*)&regs, sizeof(t->savedRegs));

  /* Save fpregs into trap information */
  memcpy((void*)&t->savedFPregs, (void*)&fpregs, sizeof(t->savedFPregs));
  regs.rip = segConfig.textSegBase;

  dumpRegs("processGlobal2", traceePid, &regs);

  /*
   * Get the variable address and push it into the target process patch area.
   */
  auto sym = symbols->locateSymbol(varName);
  if (!sym.has_value()) {
    LOG(ERROR) << "processGlobal: failed to get global's address!";
    return false;
  }

  uint64_t addr = sym->addr;

  auto gd = symbols->findGlobalDesc(varName);
  if (!gd) {
    LOG(ERROR) << "processGlobal: failed to find GlobalDesc!";
    return false;
  }

  auto remoteObjAddr = remoteObjAddrs.find(gd);
  if (remoteObjAddr == remoteObjAddrs.end()) {
    LOG(ERROR) << "processGlobal: no remote object addr for " << varName;
    return false;
  }

  if (!writeTargetMemory(
          (void*)&addr, (void*)remoteObjAddr->second, sizeof(addr))) {
    LOG(ERROR) << "processGlobal: writeTargetMemory remoteObjAddr failed!";
  }

  VLOG(1) << varName << " addr: " << std::hex << addr;

  /* Main target thread should already be stopped */

  errno = 0;
  if (ptrace(PTRACE_SETREGS, traceePid, nullptr, &regs) < 0) {
    LOG(ERROR) << "Execute: Couldn't restore registers: " << strerror(errno);
  }

  contTargetThread(traceePid);

  return true;
}

bool OIDebugger::canProcessTrapForThread(pid_t thread_pid) const {
  /*
   * We want to prevent multiple threads from running the JIT code at the same
   * time. We have only one data segment, so we don't support concurrent
   * probes, for the moment. This method checks if we have a probe already
   * running. It uses the threadTrapState map to do so. If the map is empty,
   * no probe is running, so we return true. Otherwise, we must check if the
   * the thread which encountered the trap is part of the map. If the new trap
   * is from the same thread, we also return true. This is to handle entry-ret
   * probes and the JIT code ret trap.
   */
  if (threadTrapState.empty()) {
    return true;
  }
  return threadTrapState.find(thread_pid) != end(threadTrapState);
}

/*
 * Wait for a thread to stop and process whatever caused it to stop. Which
 * thread is waited for is controlled by the 'anyPid' parameter: if true
 * (default) we wait for any thread to stop, if false we wait for the thread
 * specified by the first parameter 'pid' to stop.
 */
OIDebugger::processTrapRet OIDebugger::processTrap(pid_t pid,
                                                   bool blocking,
                                                   bool anyPid) {
  int status = 0;
  pid_t newpid = 0;
  processTrapRet ret = OIDebugger::OID_CONT;
  pid_t pidToWaitFor = -1;

  if (threadList.empty()) {
    LOG(ERROR)
        << "Thread list is empty: the target process must have died. Abort...";
    return OID_DONE;
  }

  if (!anyPid) {
    pidToWaitFor = pid;
    VLOG(1) << "About to wait for pid  " << std::dec << pidToWaitFor;
  } else {
    VLOG(1) << "About to wait for any child process ";
  }

  errno = 0;
  int options = blocking ? 0 : WNOHANG;
  if ((newpid = waitpid(pidToWaitFor, &status, options)) < 0) {
    LOG(ERROR) << "processTrap: Error in waitpid (pid " << pidToWaitFor << ")"
               << " " << strerror(errno);

    /* SIGINT handling */
    if (errno == EINTR && isInterrupted()) {
      return OIDebugger::OID_DONE;
    }
  }

  if (newpid == 0) {
    /* WNOHANG handling */
    VLOG(4) << "No child waiting for processTrap";
    return OIDebugger::OID_DONE;
  }

  if (!WIFSTOPPED(status)) {
    LOG(ERROR) << "contAndExecute: process not stopped!";
    return blocking ? OID_ERR : OID_CONT;
  }
  siginfo_t info;

  auto stopsig = WSTOPSIG(status);
  VLOG(4) << "Stop sig: " << std::dec << stopsig;
  if (ptrace(PTRACE_GETSIGINFO, newpid, nullptr, &info) < 0) {
    LOG(ERROR) << "PTRACE_GETSIGINFO failed with " << strerror(errno);
  } else {
    bool stopped = !!info.si_status;
    VLOG(4) << "PTRACE_GETSIGINFO pid " << newpid << " signo " << info.si_signo
            << " code " << info.si_code << " status " << info.si_status
            << " stopped: " << stopped;
  }

  /*
   * Process PTRACE_EVENT stops (extended waits).
   */
  if (isExtendedWait(status)) {
    int type = getExtendedWaitEventType(status);

    if (type == PTRACE_EVENT_CLONE || type == PTRACE_EVENT_FORK ||
        type == PTRACE_EVENT_VFORK) {
      unsigned long ptraceMsg = 0;
      if (ptrace(PTRACE_GETEVENTMSG, newpid, NULL, &ptraceMsg) < 0) {
        LOG(ERROR) << "processTrap: PTRACE_GETEVENTMSG failed: "
                   << strerror(errno);
      }

      pid_t childPid = static_cast<pid_t>(ptraceMsg);
      VLOG(4) << "New child created. pid " << std::dec << childPid
              << " (type: " << type << ")";
      threadList.push_back(childPid);

      /*
       * ptrace() semantics are opaque to say the least. The new child
       * has a pending SIGSTOP so we need to relieve it of that burden.
       */
      int tstatus = 0;
      int tret = 0;
      tret = waitpid(childPid, &tstatus, __WALL | WSTOPPED | WEXITED);

      if (tret == -1) {
        VLOG(4) << "processTrap: Error waiting for new child " << std::dec
                << childPid << " (Reason: " << strerror(errno) << ")";
      } else if (tret != childPid) {
        VLOG(4) << "processTrap: Unexpected pid waiting for child: " << std::dec
                << tret;
      } else if (!WIFSTOPPED(tstatus)) {
        VLOG(4) << "processTrap: Unexpected status returned when "
                   "waiting for child: "
                << std::hex << tstatus;
      }

      if (WIFSTOPPED(tstatus)) {
        VLOG(4) << "child was stopped with: " << WSTOPSIG(tstatus);
      }

      ptrace(PTRACE_SETOPTIONS,
             childPid,
             NULL,
             PTRACE_O_TRACESYSGOOD | PTRACE_O_TRACEFORK | PTRACE_O_TRACECLONE |
                 PTRACE_O_TRACEVFORK);

      contTargetThread(childPid);
    } else if (type == PTRACE_EVENT_EXIT) {
      VLOG(4) << "Thread exiting!! pid " << std::dec << newpid;
      threadList.erase(
          std::remove(threadList.begin(), threadList.end(), newpid),
          threadList.end());
    } else if (type == PTRACE_EVENT_STOP) {
      VLOG(4) << "PTRACE_EVENT_STOP!";
    } else {
      VLOG(4) << "unhandled extended wait event type: " << type;
    }

    contTargetThread(newpid);

    return ret;
  }

  int sig = WSTOPSIG(status);
  switch (sig) {
      /*
       * One of the massive advantages of controlling a target via the ptrace
       * interface is that fatal signals are not delivered to the target but
       * come to us first. This means that we can pretty much do whatever we
       * want in our jitted code and get away with it...
       */
    case SIGALRM: {
      VLOG(4) << "SIGALRM received!!";
      contTargetThread(newpid, SIGALRM);
      break;
    }

    case SIGSEGV: {
      {
        errno = 0;
        struct user_regs_struct regs {};
        if (ptrace(PTRACE_GETREGS, newpid, nullptr, &regs) < 0) {
          LOG(ERROR) << "SIGSEGV handling: failed to read registers"
                     << strerror(errno);
        }

        dumpRegs("SIGSEGV", newpid, &regs);
        LOG(ERROR) << "SIGSEGV: faulting addr " << std::hex << info.si_addr
                   << " rip " << regs.rip << " (pid " << std::dec << newpid
                   << ")";
      }

      if (auto iter{threadTrapState.find(newpid)};
          iter != std::end(threadTrapState)) {
        auto t{iter->second};

        LOG(ERROR) << "SEGV handling trap state found for " << std::dec
                   << newpid;

        errno = 0;
        if (ptrace(PTRACE_SETREGS, newpid, NULL, &t->savedRegs) < 0) {
          LOG(ERROR) << "processTrap: Couldn't restore registers: "
                     << strerror(errno);
        }
        dumpRegs("After1", newpid, &t->savedRegs);

        errno = 0;
        if (ptrace(PTRACE_SETFPREGS, newpid, NULL, &t->savedFPregs) < 0) {
          LOG(ERROR) << "processTrap: Couldn't restore fp registers: "
                     << strerror(errno);
        }

        replayTrappedInstr(*t, newpid, t->savedRegs, t->savedFPregs);
        threadTrapState.erase(newpid);

        contTargetThread(newpid);
      } else {
        // We are not the source of the SIGSEGV so forward it to the target.
        contTargetThread(newpid, sig);
      }
      return OIDebugger::OID_DONE;
    }

    case SIGBUS: {
      LOG(ERROR) << "SIGBUS: " << std::hex << info.si_addr << "(pid " << newpid
                 << ")";
      return OIDebugger::OID_DONE;
    }

    case SIGCHLD: {
      VLOG(4) << "SIGCHLD processing for pid " << newpid;
      contTargetThread(newpid, sig);

      break;
    }

    case SIGTRAP: {
      /*
       * Posix dictates that siginfo.si_addr is only used for SIGILL,
       * SIGFPE, SIGSEGV and SIGBUS. Use PTRACE_GETREGS for SIGTRAP.
       *
       * We use INT3 for several purposes: in segment setup,
       * instrumentation code execution and in pid provider style
       * entry/return point vectoring. We therefore must be able to match
       * which interrupt this is for and act accordingly.
       */
      struct user_regs_struct regs {};
      struct user_fpregs_struct fpregs {};

      errno = 0;
      if (ptrace(PTRACE_GETREGS, newpid, nullptr, &regs) < 0) {
        LOG(ERROR) << "processTrap failed to read registers for process "
                   << traceePid << " " << strerror(errno);

        return OIDebugger::OID_ERR;
      }

      errno = 0;
      if (ptrace(PTRACE_GETFPREGS, newpid, nullptr, &fpregs) < 0) {
        LOG(ERROR) << "processTrap failed to read fp regs for process "
                   << traceePid << " " << strerror(errno);

        return OIDebugger::OID_ERR;
      }

      /*
       * If we do not remove the breakpoint trap instructions then we
       * need to not adjust the %rip and ensure we replay the original
       * instruction that has been patched before we return the thread
       * to it's next instruction.
       */
      uint64_t bpaddr = regs.rip - sizeofInt3;

      if (auto it{activeTraps.find(bpaddr)}; it != std::end(activeTraps)) {
        /*
         * This HAS TO BE a COPY and not a reference! removeTrap() would
         * otherwise delete the only instance of the std::shared_ptr, leading
         * to the destruction of the tInfo and many use-after-free!
         */
        auto tInfo = it->second;
        assert(bpaddr == tInfo->trapAddr);

        if (!blocking || !canProcessTrapForThread(newpid)) {
          /*
           * Only one probe is allowed to run at a time. We skip the other
           * traps by replaying their instruction and continuing their thread.
           */
          if (blocking) {
            VLOG(4)
                << "Another probe already running, skipping trap for thread "
                << newpid;
          } else {
            VLOG(4) << "Resuming thread " << newpid;
          }

          replayTrappedInstr(*tInfo, newpid, regs, fpregs);
          contTargetThread(newpid);

          break;
        }

        /* Remove the trap right before we process it */
        removeTrap(newpid, *tInfo);

        if (tInfo->trapKind == OID_TRAP_JITCODERET) {
          ret = processJitCodeRet(*tInfo, newpid);
        } else {
          ret = processFuncTrap(*tInfo, newpid, regs, fpregs);
        }
      } else {
        LOG(ERROR) << "Error! SIGTRAP: " << std::hex << bpaddr
                   << " No activeTraps entry found, resuming thread "
                   << std::dec << newpid;

        // Resuming at the breakpoint
        regs.rip = bpaddr;

        errno = 0;
        if (ptrace(PTRACE_SETREGS, newpid, NULL, &regs) < 0) {
          LOG(ERROR) << "processTrap failed to set registers for process "
                     << newpid << " " << strerror(errno);
        }

        contTargetThread(newpid);
      }

      break;
    }

    case SIGILL: {
      VLOG(4) << "OOPS! SIGILL received for pid " << newpid
              << " addr: " << std::hex << info.si_addr;

      break;
    }

    default:
      VLOG(4) << "contAndExecute: Explictly unhandled signal. Forwarding "
              << strsignal(WSTOPSIG(status));

      contTargetThread(newpid, sig);
      break;
  }

  return ret;
}

std::optional<std::vector<uintptr_t>> OIDebugger::findRetLocs(FuncDesc& fd) {
  size_t maxSize = std::accumulate(
      fd.ranges.begin(), fd.ranges.end(), size_t(0), [](auto currMax, auto& r) {
        return std::max(currMax, r.size());
      });

  std::vector<uintptr_t> retLocs;
  std::vector<std::byte> text(maxSize);
  for (const auto& range : fd.ranges) {
    /*
     * We already have enough capacity to accomodate any range from the function
     * But we must ensure the actual `size` of the vector matches what is being
     * put into it. `locateOpcode` uses the vector's size to know for how long
     * it must decode instructions. As resize doesn't never reduces the capacity
     * of the vector, there is no risk of multiple memory allocations impacting
     * the performances.
     */
    text.resize(range.size());

    /* Copy the range of instruction into the text vector to be disassembled */
    if (!readTargetMemory((void*)range.start, text.data(), text.size())) {
      LOG(ERROR) << "Could not read function range " << fd.symName << "@"
                 << range;
      return std::nullopt;
    }

    /*
     * Locate the returns within the function instructions.
     * Immediate values have a size of 16-bits/2-bytes. Both 0xC2 and 0xCA have
     * a total size of 3 bytes, which fit in the 8 bytes window we capture with
     * PTRACE_PEEKTEXT. So we'll be able to capture and replay the instraction
     * without problem. We assume a flat memory model, meaning we only have 1
     * segment and we don't have returns across segments. So we don't have to
     * convert RETN into RETF during replay. We can't probe the return fo
     * tail-recursive functions as they typically ends with a JMP.
     */
    // clang-format off
    const std::array retInsts = {
        std::array{uint8_t(0xC2)}, /* Return from near procedure with immediate value */
        std::array{uint8_t(0xC3)}, /* Return from near procedure */
        std::array{uint8_t(0xCA)}, /* Return from far procedure with immediate value */
        std::array{uint8_t(0xCB)}, /* Return from far procedure */
    };
    // clang-format on
    auto locs = OICompiler::locateOpcodes(text, retInsts);
    if (!locs.has_value()) {
      LOG(ERROR)
          << "Failed to locate all Return instructions in function range "
          << fd.symName << "@" << range;
      return std::nullopt;
    }

    /* Add the range.start to the offsets returned above to get the absolute
     * address of the return instructions */
    for (auto loc : *locs) {
      retLocs.push_back(range.start + loc);
    }
  }

  return retLocs;
}

/*
 * Locate the address in the target address space where the patched
 * instruction that was at address 'addr' is found. We currently don't handle
 * instructions larger than 8 bytes.
 *
 * If it's not in the replayInstMap, return the address to the next free entry
 * in the cache and put the entry in the map.
 */
std::optional<uintptr_t> OIDebugger::nextReplayInstrAddr(const trapInfo& t) {
  if (auto it = replayInstMap.find(t.trapAddr); it != end(replayInstMap)) {
    VLOG(1) << "Found instruction for trap " << (void*)t.trapAddr
            << " at address " << (void*)it->second;
    return it->second;
  }

  /*
   * Not found so allocate the instruction into the replay instruction area and
   * create a map entry.
   */

  VLOG(1) << "Replay Instruction Base " << std::hex << segConfig.replayInstBase;
  auto newInstrAddr = segConfig.replayInstBase + replayInstsCurIdx++ * 8;
  if (newInstrAddr >= segConfig.textSegBase + segConfig.textSegSize) {
    LOG(ERROR) << "Text Segment's Replay Instruction buffer is full. Increase "
                  "replayInstSize in OIDebugger.h";
    return std::nullopt;
  }

  VLOG(1) << "Orig instructions for trap " << (void*)t.trapAddr
          << " will get saved at " << (void*)newInstrAddr;
  replayInstMap.emplace(t.trapAddr, newInstrAddr);

  return newInstrAddr;
}

/*
 * Insert a breakpoint trap at the first instruction of the function
 * supplied as the argument.
 *
 * This is a hybrid instrumentation function for entry-return patching. Pulling
 * the entry and return patching together for this case does create a
 * maintenance burden as we now have code doing exactly the same thing in
 * two different places However, it allows us to reduce the time window
 * between enabling entry and return sites (i.e., when we actually install
 * breakpoint traps).
 */

/*
 * This is a first pass at function return tracing. What does that mean
 * exactly. Well, we allow introspection of the actual return value or we
 * allow introspection of one of the input parameters *at return*. This
 * causes significant complications to what is already looking like a very
 * problematic problem (at least from a concurrency perspective).
 *
 * Currently I can't see a way to obtain function parameters without using
 * breakpoints at the function entry and of course this is a real pain.
 * Another problem is that we may well have multiple return locations
 * within a function so we need multiple points of instrumentation. The
 * main issue I see with this is how to do "atomically" and this has multiple
 * meanings according to context:
 *   - a thread executing instructions. Currently the only option we have
 *   for modifying read/execute text is via ptrace and this does 8 bytes
 *   at a time. How atomic is that 8 byte write w.r.t threads that are
 *   executing code within that region? Needs investigation but I'd be very
 *   surprised if it was truly atomic.
 *   - w.r.t Object Introspection. This is easier to deal with than the above
 *   problem but we have to have a consistent view within OI. This means that
 *   until all our ret's are instrumented (and possibly our entry point also)
 *   then we can't allow a thread to go down the introspection path. All
 *   instrumentation much be done as a single unit.
 */

bool OIDebugger::functionPatch(const prequest& req) {
  assert(req.type != "global");

  auto fd = symbols->findFuncDesc(req.getReqForArg(0));
  if (!fd) {
    LOG(ERROR) << "Could not find FuncDesc for " << req.func;
    return false;
  }

  /*
   * Patch the function according to the given request:
   * 1. Locate all TRAP points and create a corresponding empty trapInfo in
   * tiVec.
   * 2. Read the original instructions in their corresponding trapInfo.
   * 3. Finish building the trapInfo with the info collected above.
   * 4. Save the original instructions in our Replay Instruction buffer.
   * 5. Insert the traps in the target process.
   */

  std::vector<std::shared_ptr<trapInfo>> tiVec;

  /* 1. Locate all TRAP points and create a corresponding empty trapInfo in
   * tiVec */
  bool hasArg = std::any_of(begin(req.args), end(req.args), [](auto& arg) {
    return arg != "retval";
  });

  if (req.type == "entry" || hasArg) {
    trapType tType =
        req.type == "return" ? OID_TRAP_VECT_ENTRYRET : OID_TRAP_VECT_ENTRY;
    uintptr_t trapAddr = fd->ranges[0].start;
    if (req.args[0].starts_with("arg") || req.args[0] == "this") {
      auto* argument =
          dynamic_cast<FuncDesc::Arg*>(fd->getArgument(req.args[0]).get());
      if (argument->locator.locations_size > 0) {
        /*
         * The `std::max` is necessary because sometimes when a binary is
         * compiled in debug-mode, the compiler will state that the variable
         * becomes live at address 0, which is clearly not what we want.
         */
        trapAddr = std::max(trapAddr, argument->locator.locations[0].start);
      }
    }
    tiVec.push_back(
        std::make_shared<trapInfo>(tType, trapAddr, segConfig.textSegBase));
  }

  if (req.type == "return") {
    auto retLocs = findRetLocs(*fd);
    if (!retLocs.has_value() || retLocs.value().empty()) {
      return false;
    }

    for (auto addr : *retLocs) {
      tiVec.push_back(std::make_shared<trapInfo>(
          OID_TRAP_VECT_RET, addr, segConfig.textSegBase));
    }
  }

  assert(!tiVec.empty());

  /* 2. Read the original instructions in their corresponding trapInfo */
  std::vector<struct iovec> localIov;
  std::vector<struct iovec> remoteIov;
  localIov.reserve(tiVec.size());
  remoteIov.reserve(tiVec.size());

  for (auto& ti : tiVec) {
    localIov.push_back({(void*)ti->origTextBytes, sizeof(ti->origTextBytes)});
    remoteIov.push_back({(void*)ti->trapAddr, sizeof(ti->origTextBytes)});
  }

  errno = 0;
  auto readBytes = process_vm_readv(traceePid,
                                    localIov.data(),
                                    localIov.size(),
                                    remoteIov.data(),
                                    remoteIov.size(),
                                    0);
  if (readBytes < 0) {
    LOG(ERROR) << "Failed to get original instructions: " << strerror(errno);
    return false;
  }

  if ((size_t)readBytes != (tiVec.size() * sizeof(trapInfo::origTextBytes))) {
    LOG(ERROR) << "Failed to get all original instructions!";
    return false;
  }

  /* 3. Finish building the trapInfo with the info collected above */

  /* Re-use remoteIov to write the original instructions in our textSegment */
  remoteIov.clear();

  for (auto& trap : tiVec) {
    trap->patchedText = trap->origText;
    trap->patchedTextBytes[0] = int3Inst;

    auto replayInstrAddr = nextReplayInstrAddr(*trap);
    if (!replayInstrAddr.has_value()) {
      LOG(ERROR)
          << "Failed to allocate room for replay instructions in text segment";
      return false;
    }

    trap->replayInstAddr = *replayInstrAddr;
    remoteIov.push_back(
        {(void*)trap->replayInstAddr, sizeof(trap->origTextBytes)});

    if (trap->trapKind == OID_TRAP_VECT_ENTRY ||
        trap->trapKind == OID_TRAP_VECT_ENTRYRET) {
      /* Capture the arguments to probe */
      trap->args.reserve(req.args.size());
      for (const auto& arg : req.args) {
        if (auto targetObj = fd->getArgument(arg)) {
          trap->args.push_back(std::move(targetObj));
        } else {
          LOG(ERROR) << "Failed to get argument " << arg << " for function "
                     << req.func;
          return false;
        }
      }
    } else if (trap->trapKind == OID_TRAP_VECT_RET) {
      /* Capture retval, if this is a return-retval req */
      if (std::find(begin(req.args), end(req.args), "retval") !=
          end(req.args)) {
        trap->args.push_back(fd->retval);
      }
    }
  }

  /* 4. Save the original instructions in our Replay Instruction buffer */
  errno = 0;
  auto writtenBytes = process_vm_writev(traceePid,
                                        localIov.data(),
                                        localIov.size(),
                                        remoteIov.data(),
                                        remoteIov.size(),
                                        0);
  if (writtenBytes < 0) {
    LOG(ERROR) << "Failed to save original instructions: " << strerror(errno);
    return false;
  }

  if ((size_t)writtenBytes !=
      (tiVec.size() * sizeof(trapInfo::origTextBytes))) {
    LOG(ERROR) << "Failed to save all original instructions!";
    return false;
  }

  /* 5. Insert the traps in the target process */
  for (const auto& trap : tiVec) {
    VLOG(1) << "Patching function " << req.func << " @"
            << (void*)trap->trapAddr;
    activeTraps.emplace(trap->trapAddr, trap);

    errno = 0;
    if (ptrace(PTRACE_POKETEXT, traceePid, trap->trapAddr, trap->patchedText) <
        0) {
      /* We'll let our cleanup handling restore the original instructions */
      LOG(ERROR) << "functionPatch POKETEXT failed: " << strerror(errno);
      return false;
    }
  }

  return true;
}

/* See "syscall.h" for an explanation on the `Sys` template argument */
template <typename Sys, typename... Args>
std::optional<typename Sys::RetType> OIDebugger::remoteSyscall(Args... _args) {
  /* Check the number of arguments received match the syscall's requirement */
  static_assert(sizeof...(_args) == Sys::ArgsCount,
                "Wrong number of arguments");

  metrics::Tracing _("syscall");
  VLOG(1) << "syscall enter " << Sys::Name;

  auto sym = symbols->locateSymbol("main");
  if (!sym.has_value()) {
    LOG(ERROR) << "Failed to get address for 'main'";
    return std::nullopt;
  }

  uint64_t patchAddr = sym->addr;
  VLOG(1) << "Address of main: " << (void*)patchAddr;

  /* Saving current registers states */
  errno = 0;
  struct user_regs_struct oldregs {};
  if (ptrace(PTRACE_GETREGS, traceePid, nullptr, &oldregs) < 0) {
    LOG(ERROR) << "syscall: GETREGS failed for process " << traceePid << ": "
               << strerror(errno);
    return std::nullopt;
  }

  errno = 0;
  struct user_fpregs_struct oldfpregs {};
  if (ptrace(PTRACE_GETFPREGS, traceePid, nullptr, &oldfpregs) < 0) {
    LOG(ERROR) << "syscall: GETFPREGS failed for process " << traceePid << ": "
               << strerror(errno);
    return std::nullopt;
  }

  /*
   * Ensure we restore the registers before returning! BOOST_SCOPE_EXIT_ALL
   * sets up an object whose destructor executes the block below. Thanks to
   * RAII, it ensures that the block always gets executed, independently of
   * early returns or exception being thrown! This allow us to keep writing
   * code without having to keep "restoring the registers" in mind.
   */
  BOOST_SCOPE_EXIT_ALL(&) {
    errno = 0;
    if (ptrace(PTRACE_SETREGS, traceePid, nullptr, &oldregs) < 0) {
      VLOG(1) << "syscall: restore SETREGS failed: " << strerror(errno);
    }

    errno = 0;
    if (ptrace(PTRACE_SETFPREGS, traceePid, nullptr, &oldfpregs) < 0) {
      LOG(ERROR) << "syscall: restore SETFPREGS failed: " << strerror(errno);
    }
  };

  /* Prepare new registers to hijack the thread and execute the syscall */
  struct user_regs_struct newregs = oldregs;
  newregs.rip = patchAddr + 1;  // +1 to skip the INT3 safeguard
  newregs.rax = Sys::SysNum;

  {
    /*
     * See syscall(2) for the list of registers used for syscalls
     * arch/ABI      arg1  arg2  arg3  arg4  arg5  arg6  arg7
     * x86-64        rdi   rsi   rdx   r10   r8    r9    -
     */
    const std::array<unsigned long long*, 6> argToReg = {
        &newregs.rdi,
        &newregs.rsi,
        &newregs.rdx,
        &newregs.r10,
        &newregs.r8,
        &newregs.r9,
    };

    unsigned long long args[] = {(unsigned long long)_args...};
    for (size_t argIdx = 0; argIdx < Sys::ArgsCount; ++argIdx) {
      *argToReg[argIdx] = args[argIdx];
    }
  }

  /* Set the new registers with our syscall arguments */
  errno = 0;
  if (ptrace(PTRACE_SETREGS, traceePid, nullptr, &newregs)) {
    LOG(ERROR) << "syscall: SETREGS failed: " << strerror(errno);
    return std::nullopt;
  }

  /* Save the instructions so they can be restored */
  errno = 0;
  long oldinsts = ptrace(PTRACE_PEEKTEXT, traceePid, patchAddr, nullptr);
  if (errno != 0) {
    LOG(ERROR) << "syscall: PEEKTEXT failed: " << strerror(errno);
    return std::nullopt;
  }

  /*
   * Ensure we restore the instructions before returning!
   * See the BOOST_SCOPE_EXIT_ALL above for more explanations...
   */
  BOOST_SCOPE_EXIT_ALL(&) {
    errno = 0;
    if (ptrace(PTRACE_POKETEXT, traceePid, patchAddr, oldinsts) < 0) {
      LOG(ERROR) << "syscall: restore POKETEXT failed: " << strerror(errno);
    }
  };

  /*
   * Prepare our instructions to run the syscall.
   * This is little-endian, so read bytes from right to left.
   * We start with an INT3 to stop any other thread that might enter `main`
   * while we're fiddling with it (highly unlikely). We rely on `processTrap`
   * to resume them later on. Then we have the two bytes for the SYSCALL
   * instruction and the rest is filled with NOPs.
   */
  long newinsts = syscallInsts;

  /* Insert the SYSCALL instruction into the target */
  errno = 0;
  if (ptrace(PTRACE_POKETEXT, traceePid, patchAddr, newinsts) < 0) {
    LOG(ERROR) << "syscall: POKETEXT failed: " << strerror(errno);
    return std::nullopt;
  }

  { /* SINGLESTEP once to run the syscall */
    errno = 0;
    if (ptrace(PTRACE_SINGLESTEP, traceePid, nullptr, nullptr) < 0) {
      LOG(ERROR) << "syscall: SYSCALL " << Sys::Name
                 << " failed: " << strerror(errno);
      return std::nullopt;
    }

    int status = 0;
    waitpid(traceePid, &status, 0);

    if (!WIFSTOPPED(status)) {
      LOG(ERROR) << "process not stopped!";
    }
  }

  /* Read the new register state, so we can get the syscall's return value */
  errno = 0;
  if (ptrace(PTRACE_GETREGS, traceePid, nullptr, &newregs) < 0) {
    LOG(ERROR) << "syscall: return GETREGS failed: " << strerror(errno);
    return std::nullopt;
  }

  if ((long long)newregs.rax < 0) {
    LOG(ERROR) << "syscall: Syscall " << Sys::Name
               << " failed with error: " << strerror((int)-newregs.rax);
    return std::nullopt;
  }

  typename Sys::RetType retval = (typename Sys::RetType)newregs.rax;

  VLOG(1) << "syscall " << Sys::Name << " returned " << std::hex << retval;
  return retval;
}

bool OIDebugger::setupSegment(SegType seg) {
  metrics::Tracing _("setup_segment");

  std::optional<void*> segAddr;
  if (seg == SegType::text) {
    segAddr =
        remoteSyscall<SysMmap>(nullptr,
                               textSegSize,  // addr & size
                               PROT_READ | PROT_WRITE | PROT_EXEC,  // prot
                               MAP_PRIVATE | MAP_ANONYMOUS,         // flags
                               -1,
                               0);  // fd & offset
  } else {
    segAddr = remoteSyscall<SysMmap>(nullptr,
                                     dataSegSize,                 // addr & size
                                     PROT_READ | PROT_WRITE,      // prot
                                     MAP_SHARED | MAP_ANONYMOUS,  // flags
                                     -1,
                                     0);  // fd & offset
  }

  if (!segAddr.has_value()) {
    return false;
  }

  if (seg == SegType::text) {
    segConfig.textSegBase = (uintptr_t)segAddr.value();
    segConfig.constStart = segConfig.textSegBase + prologueLength;
    segConfig.jitCodeStart = segConfig.constStart + constLength;
    segConfig.textSegSize = textSegSize;

    /*
     * For the minute we'll use the last 512 bytes of text for storing
     * replay instructions.
     */
    segConfig.replayInstBase =
        segConfig.textSegBase + textSegSize - replayInstSize;
    VLOG(1) << "replayInstBase addr " << std::hex << segConfig.replayInstBase;
  } else {
    segConfig.dataSegBase = (uint64_t)*segAddr;
    segConfig.dataSegSize = dataSegSize;
  }

  return true;
}

bool OIDebugger::unmapSegment(SegType seg) {
  metrics::Tracing _("unmap_segment");
  auto addr =
      (seg == SegType::text) ? segConfig.textSegBase : segConfig.dataSegBase;
  auto size = (seg == SegType::text) ? textSegSize : dataSegSize;
  return remoteSyscall<SysMunmap>(addr, size).has_value();
}

bool OIDebugger::unmapSegments(bool deleteSegConfFile) {
  bool ret = true;

  if (!unmapSegment(SegType::text)) {
    LOG(ERROR) << "Problem unmapping target process text segment";
    ret = false;
  }

  if (ret && !unmapSegment(SegType::data)) {
    LOG(ERROR) << "Problem unmapping target process data segment";
    ret = false;
  }

  deleteSegmentConfig(deleteSegConfFile);

  return ret;
}

/*
 * The debugger is shutting things down so all breakpoint traps need to be
 * removed from the target process. As oid is currently single threaded
 * there isn't a whole lot to race over so its pretty easy. We just need
 * to be careful that no target threads end up waiting for signals to be
 * be dealt with by us.
 *
 * NOTE: This code can be called from a signal handler context so keep it
 * async-signal-safe. Obviously any output in debug or error modes are not
 * async-signal-safe but we'll live with that for now.
 *
 * The calling thread *must* be stopped before calling this interface.
 * Unfortunately there is no cheap way to assert this.
 */
bool OIDebugger::removeTraps(pid_t pid) {
  metrics::Tracing removeTrapsTracing("remove_traps");

  pid_t targetPid = pid ? pid : traceePid;

  /* Hijack the main thread to remove the traps and flush the JIT logs */
  errno = 0;
  if (ptrace(PTRACE_INTERRUPT, targetPid, nullptr, nullptr) < 0) {
    LOG(ERROR) << "Couldn't interrupt target pid " << targetPid << ": "
               << strerror(errno);
    return false;
  }

  errno = 0;
  if (waitpid(targetPid, 0, WSTOPPED) != targetPid) {
    LOG(ERROR) << "Failed to stop the target pid " << targetPid << ": "
               << strerror(errno);
  }

  for (auto it = activeTraps.begin(); it != activeTraps.end();) {
    const auto& tInfo = it->second;

    /* We don't care about our own traps */
    if (tInfo->trapKind == OID_TRAP_JITCODERET) {
      ++it;
      continue;
    }

    VLOG(1) << "removeTraps removing int3 at " << std::hex << tInfo->trapAddr;

    errno = 0;
    if (ptrace(PTRACE_POKETEXT, targetPid, tInfo->trapAddr, tInfo->origText) <
        0) {
      LOG(ERROR) << "Execute: Couldn't poke text: " << strerror(errno);
    }

    it = activeTraps.erase(it);
  }

  /* Resume the main thread now, so it doesn't have to wait on restoreState */
  if (!contTargetThread(targetPid)) {
    return false;
  }

  return true;
}

bool OIDebugger::removeTrap(pid_t pid, const trapInfo& t) {
  std::array<std::byte, 8> repatchedBytes{};
  memcpy(repatchedBytes.data(), t.origTextBytes, repatchedBytes.size());

  if (auto it = activeTraps.find(t.trapAddr); it != end(activeTraps)) {
    /*
     * `ptrace(POKETEXT)` writes 8 bytes in the target process. If multiple
     * traps are close to each others, the 8 bytes window might overlap with
     * multiple of them. We iter on the items that are within this window and
     * copy their patched bytes over ensuring we don't lose their INT3.
     */
    constexpr auto windowSize = repatchedBytes.size();
    while (++it != end(activeTraps)) {
      uintptr_t off = it->first - t.trapAddr;
      if (off >= windowSize) {
        break;
      }

      memcpy(repatchedBytes.data() + off,
             it->second->patchedTextBytes,
             windowSize - off);
    }
  }

  VLOG(4) << "removeTrap removing int3 at " << std::hex << t.trapAddr;
  if (ptrace(PTRACE_POKETEXT,
             (!pid ? traceePid : pid),
             t.trapAddr,
             *reinterpret_cast<uintptr_t*>(repatchedBytes.data())) < 0) {
    LOG(ERROR) << "Execute: Couldn't poke text: " << strerror(errno);
    return false;
  }

  activeTraps.erase(t.trapAddr);

  return true;
}

OIDebugger::OIDebugger(const OICodeGen::Config& genConfig,
                       OICompiler::Config ccConfig,
                       TreeBuilder::Config tbConfig)
    : cache{genConfig},
      compilerConfig{std::move(ccConfig)},
      generatorConfig{genConfig},
      treeBuilderConfig{std::move(tbConfig)} {
  debug = true;

  VLOG(1) << "CodeGen config: " << generatorConfig.toString();
}

OIDebugger::OIDebugger(pid_t pid,
                       const OICodeGen::Config& genConfig,
                       OICompiler::Config ccConfig,
                       TreeBuilder::Config tbConfig)
    : OIDebugger(genConfig, std::move(ccConfig), std::move(tbConfig)) {
  traceePid = pid;
  symbols = std::make_shared<SymbolService>(traceePid);
  setDataSegmentSize(dataSegSize);
  createSegmentConfigFile();
  cache.symbols = symbols;
}

OIDebugger::OIDebugger(fs::path debugInfo,
                       const OICodeGen::Config& genConfig,
                       OICompiler::Config ccConfig,
                       TreeBuilder::Config tbConfig)
    : OIDebugger(genConfig, std::move(ccConfig), std::move(tbConfig)) {
  symbols = std::make_shared<SymbolService>(std::move(debugInfo));
  cache.symbols = symbols;
}

/*
 * Copy local buffer from specified address into the remote address in
 * the target process. Note that this only works for mappings that have
 * write permissions set (i.e., mappings that we have created and not
 * standard text mappings).
 *
 * @param[in] local_buffer - buffer containing data to be written
 * @param[in] target_addr - target address where new data are to be written
 * @param[in] bufsz - length of 'target_addr' buffer in bytes
 */
bool OIDebugger::writeTargetMemory(void* local_buffer,
                                   void* target_addr,
                                   size_t bufsz) const {
  VLOG(1) << "Writing buffer " << std::hex << local_buffer << ", bufsz "
          << std::dec << bufsz << " into target " << std::hex << target_addr;

  struct iovec liovecs[] = {{
      .iov_base = local_buffer,
      .iov_len = bufsz,
  }};
  size_t liovcnt = sizeof(liovecs) / sizeof(struct iovec);

  struct iovec riovecs[] = {{
      .iov_base = target_addr,
      .iov_len = bufsz,
  }};
  size_t riovcnt = sizeof(riovecs) / sizeof(struct iovec);

  auto writtenBytesCount =
      process_vm_writev(traceePid, liovecs, liovcnt, riovecs, riovcnt, 0);
  if (writtenBytesCount == -1) {
    LOG(ERROR) << "process_vm_writev() error: " << strerror(errno) << " ("
               << errno << ")";
    return false;
  }

  if (static_cast<size_t>(writtenBytesCount) != bufsz) {
    LOG(ERROR) << "process_vm_writev() wrote only " << writtenBytesCount
               << " bytes, expected " << bufsz << " bytes";
    return false;
  }

  return true;
}

/*
 * Copy remote buffer from specified address in the target process into
 * the local address.
 *
 * @param[in] remote_buffer - buffer in the target process where the data is
 * read from
 * @param[in] local_addr - local address where new data are to be written
 * @param[in] bufsz - length of 'local_addr' buffer in bytes
 */
bool OIDebugger::readTargetMemory(void* remote_buffer,
                                  void* local_addr,
                                  size_t bufsz) const {
  VLOG(1) << "Reading buffer " << std::hex << remote_buffer << ", bufsz "
          << std::dec << bufsz << " into local " << std::hex << local_addr;

  struct iovec liovecs[] = {{
      .iov_base = local_addr,
      .iov_len = bufsz,
  }};
  size_t liovcnt = sizeof(liovecs) / sizeof(struct iovec);

  struct iovec riovecs[] = {{
      .iov_base = remote_buffer,
      .iov_len = bufsz,
  }};
  size_t riovcnt = sizeof(riovecs) / sizeof(struct iovec);

  auto readBytesCount =
      process_vm_readv(traceePid, liovecs, liovcnt, riovecs, riovcnt, 0);
  if (readBytesCount == -1) {
    LOG(ERROR) << "process_vm_readv() error: " << strerror(errno) << " ("
               << errno << ")";
    return false;
  }

  if (static_cast<size_t>(readBytesCount) != bufsz) {
    LOG(ERROR) << "process_vm_readv() wrote only " << readBytesCount
               << " bytes, expected " << bufsz << " bytes";
    return false;
  }

  return true;
}

std::optional<std::pair<OIDebugger::ObjectAddrMap::key_type, uintptr_t>>
OIDebugger::locateJitCodeStart(
    const irequest& req, const OICompiler::RelocResult::SymTable& jitSymbols) {
  // Get type of probed object to locate the JIT code start
  OIDebugger::ObjectAddrMap::key_type targetObj;
  if (req.type == "global") {
    const auto& gd = symbols->findGlobalDesc(req.func);
    if (!gd) {
      LOG(ERROR) << "Failed to find GlobalDesc for " << req.func;
      return std::nullopt;
    }

    targetObj = gd;
  } else {
    const auto& fd = symbols->findFuncDesc(req);
    if (!fd) {
      LOG(ERROR) << "Failed to find FuncDesc for " << req.func;
      return std::nullopt;
    }

    const auto& farg = fd->getArgument(req.arg);
    if (!farg) {
      LOG(ERROR) << "Failed to get argument for " << req.func << ':' << req.arg;
      return std::nullopt;
    }

    targetObj = farg;
  }

  auto& typeName = std::visit(
      [](auto&& obj) -> std::string& { return obj->typeName; }, targetObj);
  auto typeHash = std::hash<std::string_view>{}(typeName);
  auto jitCodeName = (boost::format("_Z24getSize_%016x") % typeHash).str();

  uintptr_t jitCodeStart = 0;
  for (const auto& [symName, symAddr] : jitSymbols) {
    if (symName.starts_with(jitCodeName)) {
      jitCodeStart = symAddr;
      break;
    }
  }

  if (jitCodeStart == 0) {
    LOG(ERROR) << "Couldn't find " << jitCodeName << " in symbol table";
    return std::nullopt;
  }

  return std::make_pair(std::move(targetObj), jitCodeStart);
}

/*
 * Hmmm. I'm pretty sure the JIT'd code sequence is always going to be a
 * non-leaf function at the top level and therefore it's going to finish
 * with a 'ret'. This means that we're going to need to get into it
 * with a 'call' (obviously!). I want to keep the generated code all
 * contained within the mmap'd text area and therefore I need to manually
 * craft a call sequence which will do the following:
 *
 *   movabs addr_of_object, %rdi
 *   movabs addr_of_entry_top_level_func, %rax
 *   call (*rax)
 *   movabs regs.rip, %rdi
 *   jmpq *(%rdi)
 *
 * Maybe we can do better than this shenanigans but not sure how at the
 * minute. This above sequence will go as a 'prologue' at the start of the
 * mmaped section, before our JIT'd sequence and this will affect the base
 * address of the relocations. NOTE: be very careful that we pass in an
 * address to setBaseRelocAddr() above that takes into account the prologue
 * sequence we are constructing here otherwise the relocations will be
 * wrong. Assume the prologue is 64 bytes.
 *
 * Note that the movabs is a whopper of an instruction at 10 bytes. I'm
 * sure I could do it with less if I need to. Absolute addressing keeps
 * things simple though.
 *
 * A note on rounding. The ptrace peek and poke commands deal with word
 * size operations and this means that we need to round up to a word
 * boundary for our instruction buffers. Therefore we need to pad out the
 * extra space in our new text buffer with NOP instructions to avoid side
 * effects.
 */

bool OIDebugger::writePrologue(
    const prequest& preq, const OICompiler::RelocResult::SymTable& jitSymbols) {
  size_t off = 0;
  uint8_t newInsts[prologueLength];

  /*
   * Global probes don't have multiple arguments, but calling `getReqForArg(X)`
   * on them still returns the corresponding irequest. We take advantage of that
   * to re-use the same code to generate prologue for both global and func
   * probes.
   */
  size_t argCount = preq.type == "global" ? 1 : preq.args.size();

  for (size_t i = 0; i < argCount; i++) {
    const auto& req = preq.getReqForArg(i);

    auto jitCodeStart = locateJitCodeStart(req, jitSymbols);
    if (!jitCodeStart.has_value()) {
      LOG(ERROR) << "Failed to locate JIT code start for " << req.func << ':'
                 << req.arg;
      return false;
    }

    VLOG(1) << "Generating prologue for argument '" << req.arg
            << "', using probe at " << (void*)jitCodeStart->second;

    newInsts[off++] = movabsrdi0Inst;
    newInsts[off++] = movabsrdi1Inst;
    remoteObjAddrs.emplace(std::move(jitCodeStart->first),
                           segConfig.textSegBase + off);
    std::visit([](auto&& obj) { obj = nullptr; },
               jitCodeStart->first);  // Invalidate ptr after move
    memcpy(newInsts + off, &objectAddr, sizeof(objectAddr));
    off += sizeof(objectAddr);

    newInsts[off++] = movabsrax0Inst;
    newInsts[off++] = movabsrax1Inst;
    memcpy(newInsts + off, &jitCodeStart->second, sizeof(jitCodeStart->second));
    off += sizeof(jitCodeStart->second);

    newInsts[off++] = callRaxInst0Inst;
    newInsts[off++] = callRaxInst1Inst;
  }

  VLOG(1) << "INT3 at offset " << std::hex << off;

  auto t = std::make_shared<trapInfo>(OID_TRAP_JITCODERET,
                                      segConfig.textSegBase + off);
  auto ret = activeTraps.emplace(t->trapAddr, t);
  if (ret.second == false) {
    LOG(ERROR) << "activeTrap element for " << std::hex << t->trapAddr
               << " already exists (writePrologue error!)";
  }

  newInsts[off++] = int3Inst;

  while (off <= prologueLength - sizeofUd2) {
    newInsts[off++] = ud2Inst0;
    newInsts[off++] = ud2Inst1;
  }

  assert(off <= prologueLength);

  return writeTargetMemory(
      &newInsts, (void*)segConfig.textSegBase, prologueLength);
}

/*
 * Compile the code that the OICompiler layer knows about. The result of this
 * is that the target processes text segment is populated and ready to go.
 */
bool OIDebugger::compileCode() {
  assert(pdata.numReqs() == 1);
  const auto& preq = pdata.getReq();

  OICompiler compiler{symbols, compilerConfig};
  std::set<fs::path> objectFiles{};

  /*
   * Global probes don't have multiple arguments, but calling `getReqForArg(X)`
   * on them still returns the corresponding irequest. We take advantage of that
   * to re-use the same code to generate prologue for both global and func
   * probes.
   */
  size_t argCount = preq.type == "global" ? 1 : preq.args.size();
  for (size_t i = 0; i < argCount; i++) {
    const auto& req = preq.getReqForArg(i);

    if (cache.isEnabled()) {
      // try to download cache artifacts if present
      if (!downloadCache()) {
#if OI_PORTABILITY_META_INTERNAL()
        // Send a request to the GOBS service
        char buf[PATH_MAX];
        const std::string procpath =
            "/proc/" + std::to_string(traceePid) + "/exe";
        size_t buf_size;
        if ((buf_size = readlink(procpath.c_str(), buf, PATH_MAX)) == -1) {
          LOG(ERROR)
              << "Failed to get binary path for tracee, could not call GOBS: "
              << strerror(errno);
        } else {
          LOG(INFO) << "Attempting to get cache request from gobs";
          ObjectIntrospection::GobsService::requestCache(
              procpath,
              std::string(buf, buf_size),
              req.toString(),
              generatorConfig.toOptions());
        }
#endif
        LOG(ERROR) << "No cache file found, exiting!";
        return false;
      }

      if (req.type == "global") {
        decltype(symbols->globalDescs) gds;
        if (cache.load(req, OICache::Entity::GlobalDescs, gds)) {
          symbols->globalDescs.merge(std::move(gds));
        }
      } else {
        decltype(symbols->funcDescs) fds;
        if (cache.load(req, OICache::Entity::FuncDescs, fds)) {
          symbols->funcDescs.merge(std::move(fds));
        }
      }
    }

    auto sourcePath = cache.getPath(req, OICache::Entity::Source);
    auto objectPath = cache.getPath(req, OICache::Entity::Object);
    auto typeHierarchyPath = cache.getPath(req, OICache::Entity::TypeHierarchy);
    auto paddingInfoPath = cache.getPath(req, OICache::Entity::PaddingInfo);

    if (!sourcePath || !objectPath || !typeHierarchyPath || !paddingInfoPath) {
      LOG(ERROR) << "Failed to get all cache paths, aborting!";
      return false;
    }

    bool skipCodeGen = cache.isEnabled() && fs::exists(*objectPath) &&
                       fs::exists(*typeHierarchyPath) &&
                       fs::exists(*paddingInfoPath);
    if (skipCodeGen) {
      std::pair<RootInfo, TypeHierarchy> th;
      skipCodeGen =
          skipCodeGen && cache.load(req, OICache::Entity::TypeHierarchy, th);

      std::map<std::string, PaddingInfo> pad;
      skipCodeGen =
          skipCodeGen && cache.load(req, OICache::Entity::PaddingInfo, pad);

      if (skipCodeGen) {
        typeInfos.emplace(req, std::make_tuple(th.first, th.second, pad));
      }
    }

    if (!skipCodeGen) {
      VLOG(2) << "Compiling probe for '" << req.arg
              << "' into: " << *objectPath;

      auto code = generateCode(req);
      if (!code.has_value()) {
        LOG(ERROR) << "Failed to generate code";
        return false;
      }

      bool doCompile = !cache.isEnabled() || !fs::exists(*objectPath);
      if (doCompile) {
        if (!compiler.compile(*code, *sourcePath, *objectPath)) {
          LOG(ERROR) << "Failed to compile code";
          return false;
        }
      }
    }

    if (cache.isEnabled() && !skipCodeGen) {
      if (req.type == "global") {
        cache.store(req, OICache::Entity::GlobalDescs, symbols->globalDescs);
      } else {
        cache.store(req, OICache::Entity::FuncDescs, symbols->funcDescs);
      }

      const auto& [rootType, typeHierarchy, paddingInfo] = typeInfos.at(req);
      cache.store(req,
                  OICache::Entity::TypeHierarchy,
                  std::make_pair(rootType, typeHierarchy));
      cache.store(req, OICache::Entity::PaddingInfo, paddingInfo);
    }

    objectFiles.insert(*objectPath);
  }

  if (traceePid) {  // we attach to a process
    std::unordered_map<std::string, uintptr_t> syntheticSymbols{
        {"dataBase", segConfig.constStart + 0 * sizeof(uintptr_t)},
        {"dataSize", segConfig.constStart + 1 * sizeof(uintptr_t)},
        {"cookieValue", segConfig.constStart + 2 * sizeof(uintptr_t)},
        {"logFile", segConfig.constStart + 3 * sizeof(uintptr_t)},
    };

    VLOG(2) << "Relocating...";
    for (const auto& o : objectFiles) {
      VLOG(2) << "  * " << o;
    }
    auto relocRes = compiler.applyRelocs(
        segConfig.jitCodeStart, objectFiles, syntheticSymbols);
    if (!relocRes.has_value()) {
      LOG(ERROR) << "Failed to relocate object code";
      return false;
    }

    const auto& [_, segments, jitSymbols] = relocRes.value();
    for (const auto& [symName, symAddr] : jitSymbols) {
      VLOG(2) << "sym " << symName << '@' << std::hex << symAddr;
    }

    const auto& lastSeg = segments.back();
    auto segmentsLimit = lastSeg.RelocAddr + lastSeg.Size;
    auto remoteSegmentLimit = segConfig.textSegBase + segConfig.textSegSize;
    if (segmentsLimit > remoteSegmentLimit) {
      size_t totalSegmentsSize = segmentsLimit - segConfig.textSegBase;
      LOG(ERROR) << "Generated instruction sequence too large for currently "
                    "mapped text segment. Instruction size: "
                 << totalSegmentsSize << " text mapping size: " << textSegSize;
      return false;
    }

    for (const auto& [BaseAddr, RelocAddr, Size] : segments) {
      if (!writeTargetMemory((void*)BaseAddr, (void*)RelocAddr, Size)) {
        return false;
      }
    }

    if (!writeTargetMemory(&segConfig.dataSegBase,
                           (void*)syntheticSymbols["dataBase"],
                           sizeof(segConfig.dataSegBase))) {
      LOG(ERROR) << "Failed to write dataSegBase in probe's dataBase";
      return false;
    }

    if (!writeTargetMemory(&dataSegSize,
                           (void*)syntheticSymbols["dataSize"],
                           sizeof(dataSegSize))) {
      LOG(ERROR) << "Failed to write dataSegSize in probe's dataSize";
      return false;
    }

    if (!writeTargetMemory(&segConfig.cookie,
                           (void*)syntheticSymbols["cookieValue"],
                           sizeof(segConfig.cookie))) {
      LOG(ERROR) << "Failed to write cookie in probe's cookieValue";
      return false;
    }

    int logFile =
        generatorConfig.features[Feature::JitLogging] ? logFds.traceeFd : 0;
    if (!writeTargetMemory(
            &logFile, (void*)syntheticSymbols["logFile"], sizeof(logFile))) {
      LOG(ERROR) << "Failed to write logFile in probe's cookieValue";
      return false;
    }

    if (!writePrologue(preq, jitSymbols)) {
      LOG(ERROR) << "Failed to write prologue";
      return false;
    }
  }

  return true;
}

/* TODO: Needs some cleanup and generally making more resilient */
void OIDebugger::restoreState(void) {
  /*
   * We are about to detach from the target process.
   * Ensure we don't have any trap in the target process still active.
   */
  const size_t activeTrapsCount = std::count_if(
      activeTraps.cbegin(), activeTraps.cend(), [](const auto& t) {
        return t.second->trapKind != OID_TRAP_JITCODERET;
      });
  VLOG(1) << "Active traps still within the target process: "
          << activeTrapsCount;
  assert(activeTrapsCount == 0);

  metrics::Tracing _("restore_state");

  int status = 0;

  /*
   * (1) Check if the thread is already stopped (because it hit a SIGTRAP),
   * (2) If thread is not already stopped, interrupt it so that we can detach
   * from it,
   * (3) If the thread was already stopped because of a SIGTRAP, reset register
   * state to that of the thread when it first came under oid control. The
   * thread could still be in oid JIT code here or trapping in from it normal
   * execution path.
   */
  for (auto const& p : threadList) {
    auto state = getTaskState(p);
    VLOG(1) << "Task " << p << " state: " << taskStateToString(state) << " ("
            << static_cast<int>(state) << ")";
    pid_t ret = waitpid(p, &status, WNOHANG | WSTOPPED);
    if (ret < 0) {
      LOG(ERROR) << "Error in waitpid (pid " << p << ")" << strerror(errno);
    } else if (ret == 0) {
      if (WIFSTOPPED(status)) {
        VLOG(1) << "Process " << p << " signal-delivery-stopped "
                << WSTOPSIG(status);
      } else {
        VLOG(1) << "Process " << p << " not signal-delivery-stopped state";
      }

      if (WIFSIGNALED(status)) {
        VLOG(1) << "Process " << p << " terminated with signal "
                << WTERMSIG(status);
      } else {
        VLOG(1) << "Process " << p << " WIFSIGNALED is false ";
      }
      VLOG(1) << "Stopping PID : " << p;

      if (ptrace(PTRACE_INTERRUPT, p, NULL, NULL) < 0) {
        VLOG(1) << "Couldn't interrupt target pid " << p
                << " (Reason: " << strerror(errno) << ")";
      }
      VLOG(1) << "Waiting to stop PID : " << p;

      if (waitpid(p, 0, WSTOPPED) != p) {
        LOG(ERROR) << "failed to wait for process " << p
                   << " (Reason: " << strerror(errno) << ")";
      }

      VLOG(1) << "Stopped PID : " << p;
    } else if (WSTOPSIG(status) == SIGTRAP) {
      VLOG(1) << "Thread already stopped PID : " << p << " signal is "
              << WSTOPSIG(status);

      /*
       * PTRACE_EVENT stop. As noted previously, not sure if this is
       * the correct way for testing for the existence of a ptrace event
       * stop but it seems to work.
       */
      if (status >> 16) {
        int type = (status >> 16);

        if (type == PTRACE_EVENT_CLONE || type == PTRACE_EVENT_FORK ||
            type == PTRACE_EVENT_VFORK) {
          /*
           * Detach the new child as it appears to be under our control
           * at this point in its life.
           */
          unsigned long childPid = 0;
          ptrace(PTRACE_GETEVENTMSG, p, NULL, &childPid);

          VLOG(1) << "New child being created!! pid " << std::dec << childPid;

          if (ptrace(PTRACE_DETACH, childPid, 0L, 0L) < 0) {
            LOG(ERROR) << "Couldn't detach target pid " << childPid
                       << " (Reason: " << strerror(errno) << ")";
          } else {
            VLOG(1) << "Successfully detached from pid " << childPid;
          }
        }

        if (ptrace(PTRACE_DETACH, p, 0L, 0L) < 0) {
          LOG(ERROR) << "Couldn't detach target pid " << p
                     << " (Reason: " << strerror(errno) << ")";
        } else {
          VLOG(1) << "Successfully detached from pid " << p;
        }

        continue;
      }

      struct user_regs_struct regs {};

      /* Find the trapInfo for this tgid */
      if (auto iter{threadTrapState.find(p)};
          iter != std::end(threadTrapState)) {
        auto t{iter->second};

        /* Paranoia really */
        assert(p == iter->first);

        struct user_fpregs_struct fpregs {};

        if (VLOG_IS_ON(1)) {
          errno = 0;
          if (ptrace(PTRACE_GETREGS, p, NULL, &regs) < 0) {
            LOG(ERROR) << "restoreState failed to read registers: "
                       << strerror(errno);
          }
          dumpRegs("Before1", p, &regs);
        }

        memcpy((void*)&regs, (void*)&t->savedRegs, sizeof(regs));
        memcpy((void*)&fpregs, (void*)&t->savedFPregs, sizeof(fpregs));

        /*
         * Note that we need to rewind the original %rip as it has trapped
         * on an INT3 (which has now been replaced by the original
         * instruction.
         */
        regs.rip -= sizeofInt3;

        errno = 0;
        if (ptrace(PTRACE_SETREGS, p, NULL, &regs) < 0) {
          LOG(ERROR) << "restoreState: Couldn't restore registers: "
                     << strerror(errno);
        }
        dumpRegs("After1", p, &regs);

        errno = 0;
        if (ptrace(PTRACE_SETFPREGS, p, NULL, &fpregs) < 0) {
          LOG(ERROR) << "restorState: Couldn't restore fp registers: "
                     << strerror(errno);
        }

        VLOG(1) << "Set registers for pid " << std::dec << iter->first;
      } else {
        /*
         * If no trapinfo exists for this thread then it must have just trapped
         * as a result of hitting a traced function and not JIT code. As so,
         * just rewind the %rip register to back it up and let it continue.
         * We're safe to just back it up as the original instructions have
         * been replaced at this point.
         */
        VLOG(1) << "Couldn't find trapInfo for pid " << std::dec << p;

        errno = 0;
        if (ptrace(PTRACE_GETREGS, p, NULL, &regs) < 0) {
          LOG(ERROR) << "restoreState SIGTRAP handling: getregs failed - "
                     << strerror(errno);
        }

        dumpRegs("Before2", p, &regs);
        regs.rip -= sizeofInt3;
        dumpRegs("After2", p, &regs);

        errno = 0;
        if (ptrace(PTRACE_SETREGS, p, NULL, &regs) < 0) {
          LOG(ERROR) << "restoreState SIGTRAP handling: setregs failed - "
                     << strerror(errno);
        }
        VLOG(1) << "Set registers for thread " << std::dec << p;
      }
    } else {
      LOG(WARNING) << "Thread " << p
                   << " stopped with unknown signal : " << WSTOPSIG(status)
                   << "ret: " << ret << " state: " << taskStateToString(state)
                   << "(" << static_cast<int>(state) << ")";

      if (state == StatusType::zombie || state == StatusType::dead) {
        /*
         * Pretty sure we don't need to do anything else if the thread
         * is a zombie or dead.
         */
        continue;
      }

      if (VLOG_IS_ON(1)) {
        errno = 0;
        struct user_regs_struct regs {};
        if (ptrace(PTRACE_GETREGS, p, NULL, &regs) < 0) {
          LOG(ERROR) << "restoreState unknown sig handling: getregs failed- "
                     << strerror(errno);
        }
        dumpRegs("Unknown Sig", p, &regs);
      }
    }

    if (!cleanupLogFile())
      LOG(ERROR) << "failed to cleanup log file!";

    if (ptrace(PTRACE_DETACH, p, 0L, 0L) < 0) {
      LOG(ERROR) << "restoreState Couldn't detach target pid " << p
                 << " (Reason: " << strerror(errno) << ")";
    } else {
      VLOG(1) << "Successfully detached from pid " << p;
    }
  }
}

bool OIDebugger::targetAttach() {
  /*
   * ptrace sucks. It doesn't have a mechanism for grabbing all the threads in
   * a process (even if it's a SEIZE operation). Grabbing all threads seems to
   * be a huge race condition so unless we find a better way to do it, we need
   * to be as defensive as we can.
   */
  if (mode == OID_MODE_THREAD) {
    if (ptrace(PTRACE_ATTACH, traceePid, NULL, NULL) < 0) {
      LOG(ERROR) << "Couldn't attach to target pid " << traceePid
                 << " (Reason: " << strerror(errno) << ")";
      return false;
    }
    std::cout << "Attached to pid " << traceePid << std::endl;
  } else {
    /* TODO - Handle exceptions */
    auto pidPath = fs::path("/proc") / std::to_string(traceePid) / "task";
    try {
      for (const auto& entry : fs::directory_iterator(pidPath)) {
        auto file = entry.path().filename().string();
        auto pid = std::stoi(file);

        VLOG(1) << "Seizing thread: " << pid;

        /*
         * As mentioned above, this code is terribly racy and we need a better
         * solution. If the seize operation fails we will carry on and assume
         * that the thread has exited in between readinf the directory and
         * here (note: ptrace(2) overloads the ESRCH return but with a seize
         * I think it can only mean one thing).
         */
        if (ptrace(PTRACE_SEIZE,
                   pid,
                   NULL,
                   PTRACE_O_TRACECLONE | PTRACE_O_TRACEFORK |
                       PTRACE_O_TRACEVFORK | PTRACE_O_TRACEEXIT) < 0) {
          LOG(ERROR) << "Couldn't seize thread " << pid
                     << " (Reason: " << strerror(errno) << ")";
        } else {
          threadList.push_back(pid);
        }
      }
    } catch (std::filesystem::filesystem_error const& ex) {
      LOG(ERROR) << "directory_iterator exception: " << ex.path1()
                 << ex.code().message();

      /*
       * As we haven't interrupted any threads yet, any threads which have
       * been seized should be released explicitly when this process exits.
       */
      return false;
    }
  }

  return true;
}

bool OIDebugger::stopTarget(void) {
  assert(traceePid > 0);

  /*
   * Note: PTRACE_ATTACH sends a SIGSTOP to the traced process but PTRACE_SEIZE
   * doesn't do this. I think that both of these interfaces will still end up
   * with the tracee reparented to us for the duration of the trace session
   * though. To interrupt a traced process if PTRACE_SEIZE has been used just
   * use PTRACE_INTERRUPT (this means no signals are delivered which seems
   * like goodness to me).
   */

  /*
   * If we are in OID_MODE_FUNC then all threads should have been seized
   * but now but we are only going to stop the main target (as supplied to us).
   * This will be the thread that we will use to set everything up. I would
   * kill for an lwp-agent thread.
   */
  if (!targetAttach()) {
    return false;
  }

  if (mode == OID_MODE_FUNC) {
    if (VLOG_IS_ON(1)) {
      OIDebugger::StatusType s = getTaskState(traceePid);
      VLOG(1) << "stopTarget: Interrupting pid " << traceePid
              << " state: " << static_cast<int>(s);
    } else {
      VLOG(1) << "stopTarget: Interrupting pid " << traceePid;
    }

    if (ptrace(PTRACE_INTERRUPT, traceePid, NULL, NULL) < 0) {
      LOG(ERROR) << "Couldn't interrupt target pid " << traceePid
                 << " (Reason: " << strerror(errno) << ")";
      return false;
    }
  }

  /*
   * The PTRACE_ATTACH/INTERRUPT cmds aren't synchronous so the tracee may
   * not be stopped by the time we are here. Wait for it to actually stop.
   */

  if (waitpid(traceePid, 0, WSTOPPED) != traceePid) {
    LOG(ERROR) << "stopTarget failed to wait for process " << traceePid
               << " (Reason: " << strerror(errno) << ")";

    if (ptrace(PTRACE_DETACH, traceePid, NULL, NULL) < 0) {
      LOG(ERROR) << "stopTarget failed to detach from process " << traceePid
                 << "before exiting";
    }

    return false;
  }

  if (VLOG_IS_ON(1)) {
    errno = 0;
    struct user_regs_struct stopregs {};
    if (ptrace(PTRACE_GETREGS, traceePid, NULL, &stopregs) < 0) {
      LOG(ERROR) << "stopTarget getregs failed for process " << traceePid
                 << " : " << strerror(errno);

      return false;
    }

    dumpRegs("stopregs: ", traceePid, &stopregs);
  }

  return true;
}

/*
 * Used by SIGINT handler to close down debugging of the target as the
 * debugger must exit. This means that all tracing must be removed from the
 * target when it is safe to do so.
 */
void OIDebugger::stopAll() {
  oidShouldExit = true;
}

void OIDebugger::setDataSegmentSize(size_t size) {
  /* round up to the next page boundary if not aligned */
  int pgsz = getpagesize();
  dataSegSize = ((size + pgsz - 1) & ~(pgsz - 1));

  VLOG(1) << "setDataSegmentSize: segment size: " << dataSegSize;
}

bool OIDebugger::decodeTargetData(const DataHeader& dataHeader,
                                  std::vector<uint64_t>& outVec) const {
  VLOG(1) << "== magicId: " << std::hex << dataHeader.magicId;
  VLOG(1) << "== cookie: " << std::hex << dataHeader.cookie;
  VLOG(1) << "== size: " << dataHeader.size;

  if (dataHeader.magicId != oidMagicId) {
    LOG(ERROR) << "Got a wrong magic ID: " << std::hex << dataHeader.magicId;
    return false;
  }

  if (dataHeader.cookie != segConfig.cookie) {
    LOG(ERROR) << "Got a wrong cookie: " << std::hex << dataHeader.cookie;
    return false;
  }

  VLOG(1) << "Total bytes in data segment " << dataHeader.size;
  if (dataHeader.size == 0) {
    LOG(ERROR)
        << "Data segment is empty. Something went wrong while probing...";
    return false;
  }

  if (dataSegSize < dataHeader.size) {
    LOG(ERROR) << "Error: Data segment is too small. Needed: "
               << dataHeader.size << " bytes, dataseg size " << dataSegSize
               << " bytes";
    return false;
  }

  if (generatorConfig.features[Feature::JitTiming]) {
    LOG(INFO) << "JIT Timing: " << dataHeader.timeTakenNs << "ns";
  }

  VLOG(1) << "Pointer tracking stats: " << dataHeader.pointersCapacity << "/"
          << dataHeader.pointersSize;

  if (dataHeader.pointersCapacity == dataHeader.pointersSize) {
    VLOG(1) << "Pointer tracking array is exhausted! Results may be"
               " partial.";
  }

  /*
   * Currently  we use MAX_INT to indicate two things:
   *  - a single MAX_INT indicates the end of results for  the current object
   *  - two consecutive MAX_INT's indicate we have finished completely.
   */
  folly::ByteRange range(dataHeader.data, dataHeader.size - sizeof(dataHeader));

  outVec.push_back(0);
  outVec.push_back(0);
  outVec.push_back(0);
  outVec.push_back(0);
  uint64_t prevVal = 0;

  while (true) {
    /* XXX Sort out the sentinel value!!! */
    auto expected = tryDecodeVarint(range);
    if (!expected) {
      std::string s =
          (expected.error() == folly::DecodeVarintError::TooManyBytes)
              ? "Invalid varint value: too many bytes."
              : "Invalid varint value: too few bytes.";
      LOG(ERROR) << s;
      return false;
    }
    uint64_t currVal = expected.value();

    if (currVal == 123456789) {
      if (prevVal == 123456789) {
        break;
      }
    } else {
      outVec.push_back(currVal);
    }
    prevVal = currVal;
  }

  return true;
}

static bool dumpDataSegment(const irequest& req,
                            const std::vector<uint64_t>& dataSeg) {
  char dumpPath[PATH_MAX] = {0};
  auto dumpPathSize = snprintf(dumpPath,
                               sizeof(dumpPath),
                               "/tmp/dataseg.%d.%s.dump",
                               getpid(),
                               req.arg.c_str());
  if (dumpPathSize < 0 || (size_t)dumpPathSize > sizeof(dumpPath)) {
    LOG(ERROR) << "Failed to generate data-segment path";
    return false;
  }

  std::ofstream dumpFile{dumpPath, std::ios_base::binary};
  if (!dumpFile) {
    LOG(ERROR) << "Failed to open data-segment file '" << dumpPath
               << "': " << strerror(errno);
    return false;
  }

  const auto outVecBytes = std::as_bytes(std::span{dataSeg});
  dumpFile.write((const char*)outVecBytes.data(), outVecBytes.size());
  if (!dumpFile) {
    LOG(ERROR) << "Failed to write to data-segment file '" << dumpPath
               << "': " << strerror(errno);
    return false;
  }

  return true;
}

bool OIDebugger::processTargetData() {
  metrics::Tracing _("process_target_data");

  std::vector<std::byte> buf{dataSegSize};
  if (!readTargetMemory(reinterpret_cast<void*>(segConfig.dataSegBase),
                        buf.data(),
                        dataSegSize)) {
    LOG(ERROR) << "Failed to read data segment from target process";
    return false;
  }

  auto res = reinterpret_cast<uintptr_t>(buf.data());

  assert(pdata.numReqs() == 1);
  const auto& preq = pdata.getReq();

  PaddingHunter paddingHunter{};
  TreeBuilder typeTree(treeBuilderConfig);

  /*
   * Global probes don't have multiple arguments, but calling `getReqForArg(X)`
   * on them still returns the corresponding irequest. We take advantage of that
   * to re-use the same code to generate prologue for both global and func
   * probes.
   */
  size_t argCount = preq.type == "global" ? 1 : preq.args.size();

  std::vector<uint64_t> outVec{};
  for (size_t i = 0; i < argCount; i++) {
    const auto& req = preq.getReqForArg(i);
    LOG(INFO) << "Processing data for argument: " << req.arg;

    const auto& dataHeader = *reinterpret_cast<DataHeader*>(res);
    res += dataHeader.size;

    outVec.clear();
    if (!decodeTargetData(dataHeader, outVec)) {
      LOG(ERROR) << "Failed to decode target data for arg: " << req.arg;
      return false;
    }

    if (treeBuilderConfig.dumpDataSegment) {
      if (!dumpDataSegment(req, outVec)) {
        LOG(ERROR) << "Failed to dump data-segment for " << req.arg;
      }

      // Skip running Tree Builder
      continue;
    }

    auto typeInfo = typeInfos.find(req);
    if (typeInfo == end(typeInfos)) {
      LOG(ERROR) << "Failed to find corresponding typeInfo for arg: "
                 << req.arg;
      return false;
    }

    const auto& [rootType, typeHierarchy, paddingInfos] = typeInfo->second;
    VLOG(1) << "Root type addr: " << (void*)rootType.type.type;

    if (treeBuilderConfig.features[Feature::GenPaddingStats]) {
      paddingHunter.localPaddedStructs = paddingInfos;
      typeTree.setPaddedStructs(&paddingHunter.localPaddedStructs);
    }

    try {
      typeTree.build(
          outVec, rootType.varName, rootType.type.type, typeHierarchy);
    } catch (std::exception& e) {
      LOG(ERROR) << "Failed to run TreeBuilder for " << req.arg;
      LOG(ERROR) << e.what();

      if (treeBuilderConfig.dumpDataSegment) {
        LOG(ERROR) << "Data-segment has been dumped for " << req.arg;
      } else {
        LOG(ERROR) << "Dumping data-segment for " << req.arg;
        if (!dumpDataSegment(req, outVec)) {
          LOG(ERROR) << "Failed to dump data-segment for " << req.arg;
        }
      }

      continue;
    }

    if (treeBuilderConfig.features[Feature::GenPaddingStats]) {
      paddingHunter.processLocalPaddingInfo();
    }
  }

  if (treeBuilderConfig.dumpDataSegment) {
    // Tree Builder was not run
    return true;
  }

  if (typeTree.emptyOutput()) {
    LOG(FATAL)
        << "Nothing to output: failed to run TreeBuilder on any argument";
  }

  if (treeBuilderConfig.jsonPath.has_value()) {
    typeTree.dumpJson();
  }

  if (treeBuilderConfig.features[Feature::GenPaddingStats]) {
    paddingHunter.outputPaddingInfo();
  }

  return true;
}

std::optional<std::string> OIDebugger::generateCode(const irequest& req) {
  auto root = symbols->getRootType(req);
  if (!root.has_value()) {
    return std::nullopt;
  }

  std::string code(headers::oi_OITraceCode_cpp);

  auto codegen = OICodeGen::buildFromConfig(generatorConfig, *symbols);
  if (!codegen) {
    return nullopt;
  }

  RootInfo rootInfo = *root;
  codegen->setRootType(rootInfo.type);
  if (!codegen->generate(code)) {
    LOG(ERROR) << "Failed to generate code for probe: " << req.type << ":"
               << req.func << ":" << req.arg;
    return std::nullopt;
  }

  typeInfos.emplace(
      req,
      std::make_tuple(RootInfo{rootInfo.varName, codegen->getRootType()},
                      codegen->getTypeHierarchy(),
                      codegen->getPaddingInfo()));

  if (generatorConfig.features[Feature::TypeGraph]) {
    CodeGen codegen2{generatorConfig, *symbols};
    codegen2.codegenFromDrgn(root->type.type, code);
  }

  if (auto sourcePath = cache.getPath(req, OICache::Entity::Source)) {
    std::ofstream(*sourcePath) << code;
  }

  if (!customCodeFile.empty()) {
    auto ifs = std::ifstream(customCodeFile);
    code.assign(std::istreambuf_iterator<char>(ifs),
                std::istreambuf_iterator<char>());
  }

  // Output JIT source-code after codegen has appended the necessary macros
  // TODO: Maybe accept file path as an arg to dump the generated code
  // instead of stdout
  if (VLOG_IS_ON(3)) {
    // VLOG truncates output, so use std::cout
    VLOG(3) << "Trace code is \n";
    std::cout << code << std::endl;
    VLOG(3) << "Trace code dump finished";
  }

  return code;
}

}  // namespace oi::detail
