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
#ifdef __aarch64__

extern "C" {
#include <sys/user.h>
}

#include "Arch.h"

namespace oi::detail::arch {

std::optional<uintptr_t> getReturnValueAddress(const user_regs_struct& regs) {
  return regs.regs[0];
}

void setProgramCounter(user_regs_struct& regs, uintptr_t pc) {
  regs.pc = pc;
}

std::optional<uintptr_t> naiveReadArgument(const user_regs_struct&,
                                           uint8_t idx) {
  /*
   * The ARM64 argument passing practices are surprisingly well documented. The
   * PDF I am using here is
   * https://github.com/ARM-software/abi-aa/releases/download/2023Q3/aapcs32.pdf
   * from https://github.com/ARM-software/abi-aa/releases/tag/2023Q3. Relevant
   * information appears to be in §6.8.2.
   *
   * This is an extremely naïve estimation of register placement. It is expected
   * to work when all preceding arguments (this, arg0, arg.., argIdx) are:
   * - Pointers. Pointers are all placed in general purpose registers
   * incrementing as expected.
   * - >16 byte by-value structures. These are defined to be placed on the stack
   * and have a pointer placed in a general purpose registers, so increment in
   * the same way.
   * - <=8 byte integers. Also placed in general purpose registers.
   *
   * Any other types, including floats, will mess up our indexing. Looking at
   * the types of all the preceding arguments could get us a lot closer. For
   * now, we rely on OID correctly restoring the process if we get this wrong,
   * and might produce garbage data.
   */
  if (idx < 8)
    return regs.regs[idx];

  return std::nullopt;
}

}  // namespace oi::detail::arch

#endif
