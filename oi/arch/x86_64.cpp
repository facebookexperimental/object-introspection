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
#ifdef __x86_64__

extern "C" {
#include <sys/user.h>
}

#include "Arch.h"

namespace oi::detail::arch {

std::optional<uintptr_t> getReturnValueAddress(const user_regs_struct& regs) {
  return regs.rax;
}

void setProgramCounter(user_regs_struct& regs, uintptr_t pc) {
  regs.rip = pc;
}

std::optional<uintptr_t> naiveReadArgument(const user_regs_struct& regs,
                                           uint8_t idx) {
  /*
   * This function is based on the information available at
   * http://6.s081.scripts.mit.edu/sp18/x86-64-architecture-guide.html. I have
   * no idea under which conditions these registers are selected. We rely on the
   * fact that OID will safely exit if incorrect, potentially producing some
   * incorrect data but otherwise leaving the process unharmed.
   */
  switch (idx) {
    case 0:
      return regs.rdi;
    case 1:
      return regs.rsi;
    case 2:
      return regs.rdx;
    case 3:
      return regs.rcx;
    case 4:
      return regs.r8;
    case 5:
      return regs.r9;
    default:
      return std::nullopt;
  }
}

}  // namespace oi::detail::arch

#endif
