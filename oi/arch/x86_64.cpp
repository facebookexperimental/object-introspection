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

}  // namespace oi::detail::arch

#endif
