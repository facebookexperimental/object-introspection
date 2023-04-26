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

#include <cstdint>
#include <string>

#include "oi/Metrics.h"
#include "oi/OICompiler.h"

extern "C" {
#include <sys/user.h>
}

/*
 * Breakpoint traps (INT3) instructions are the primary mechanism used to
 * transfer control from the traced process to the debugger. There are several
 * variants of traps :
 *  OID_TRAP_JITCODERET:    transfers control from JIT'd code sequences and
 *                          other setup operations.
 *  OID_TRAP_VECT_ENTRY:    transfers control from function entry sites.
 *  OID_TRAP_VECT_ENTRYRET: transfer control from function entry sites as
 *                          part of function return tracing. Used to capture
 *                          function argument parameters for use in function
 *                          return introspection.
 *  OID_TRAP_VECT_RET:      transfers control from function return sites.
 *
 * The differing types of re-vectoring operations share a lot of state but
 * differ in a few ways. For example, we don't need to stash the original
 * instructions for a OID_TRAP_JITCODERET sequence.
 *
 * Note that we have two maps of trapInfo objects:
 *  'activeTraps': a mapping of active breakpoints in the target process
 *  'threadTrapState': a mapping of thread to breakpoint. Used when a thread
 *  is executing a OID_TRAP_JITCODERET sequence as a result of a OID_TRAP_VECT
 *  breakpoint (i.e., executing JIT'd measurement code as a result of of a
 *  thread vectoring in from an instrumented application).
 */
enum trapType {
  OID_TRAP_JITCODERET = 0,
  OID_TRAP_VECT_ENTRY = 1,
  OID_TRAP_VECT_ENTRYRET = 2,
  OID_TRAP_VECT_RET = 3
};

const uint64_t GLOBAL_VARIABLE_TRAP_ADDR = 0xfeedfacefeedface;

class trapInfo {
 public:
  trapType trapKind{OID_TRAP_JITCODERET};

  /* The text address of the breakpoint trap (used for id of trap) */
  uintptr_t trapAddr{};

  /*
   * Relocated memory location in prologue of target object - only used for
   * OID_TRAP_VECT* traps.
   */
  uintptr_t prologueObjAddr{};

  /*
   * If this is a OID_TRAP_JITCODERET trap and this is true then vector the
   * thread back.
   */
  bool fromVect{false};

  /*
   * For function entry traps we need to stash the first 8 bytes of text.
   * (NOTE: we actually only need 1 but ptrace() minimum unit is 8 bytes.
   */
  union {
    unsigned long origText{0};
    uint8_t origTextBytes[8];
  };

  /*
   * For OID_TRAP_VECT_ENTRYRET traps we construct the patched version
   * of all traps before enabling them. The following 8 bytes just make
   * the code a bit cleaner for that case and are not used when processing
   * traps.
   */
  union {
    unsigned long patchedText{0};
    uint8_t patchedTextBytes[8];
  };

  /* Populated with registers of interrupted thread on entry to trap */
  struct user_regs_struct savedRegs;

  /* Floating point registers */
  struct user_fpregs_struct savedFPregs;

  /* This is just temp while we implement proper register/argument support */
  std::vector<std::shared_ptr<FuncDesc::TargetObject>> args;

  /*
   * Instructions that have been patched over must be replayed so that the
   * effects are observable in the thread. To do this we stash the original
   * instruction in the target process at this address which is used as a
   * single step target for execution.
   */
  uintptr_t replayInstAddr{};

  ObjectIntrospection::Metrics::Tracing lifetime{"trap"};

  trapInfo() = default;
  trapInfo(trapType t, uint64_t ta, uint64_t po = 0, bool fv = false)
      : trapKind{t}, trapAddr{ta}, prologueObjAddr{po}, fromVect{fv} {
  }
};

inline std::ostream& operator<<(std::ostream& out, const trapInfo& t) {
  static const char* trapTypeDescs[] = {
      "JIT Code Return",      // OID_TRAP_JITCODERET
      "Vector Entry",         // OID_TRAP_VECT_ENTRY
      "Vector Entry Return",  // OID_TRAP_VECT_ENTRYRET
      "Vector Return",        // OID_TRAP_VECT_RET
  };
  return out << "Trap " << trapTypeDescs[t.trapKind] << " @"
             << (void*)t.trapAddr;
}
