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
#include <cstddef>
#include <cstdint>

extern "C" {
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/types.h>
}

namespace {
template <size_t N>
struct StringLiteral {
  constexpr StringLiteral(const char (&str)[N]) {
    std::copy_n(str, N, value);
  }

  char value[N];
};
}  // namespace

/*
 * The Syscall structure describes a `syscall(2)` in a generic manner.
 * The struct is used by `OIDebugger::remoteSyscall` to define the return type
 * and statically check the number of arguments being passed. Currently, I don't
 * know how to use the _Args to also statically check the type of the arguments.
 * In the meantime, I can use the size of _Args to do a simple count check.
 */
template <StringLiteral _Name,
          unsigned long _SysNum,
          typename _RetType,
          typename... _Args>
struct Syscall {
  /* User friendly syscall name */
  static constexpr auto Name = _Name.value;

  /* The syscall's number (see <sys/syscall.h>) */
  static constexpr unsigned long SysNum = _SysNum;

  /* The syscall's return type */
  using RetType = _RetType;

  /* The number of arguments the syscall takes */
  static constexpr size_t ArgsCount = sizeof...(_Args);
  static_assert(ArgsCount <= 6,
                "X64 syscalls support a maximum of 6 arguments");
};

/*
 * The list of syscalls we want to be able to use on the remote process.
 * The types passed to `struct Syscall` come directly from `man 2 <SYSCALL>`.
 * Don't hesitate to expand this list if you need more syscalls!
 */
using SysOpen = Syscall<"open", SYS_open, int, const char*, int, mode_t>;
using SysClose = Syscall<"close", SYS_close, int, int>;
using SysFsync = Syscall<"fsync", SYS_fsync, int, int>;
using MemfdCreate =
    Syscall<"memfd_create", SYS_memfd_create, int, const char*, unsigned int>;

using SysMmap =
    Syscall<"mmap", SYS_mmap, void*, void*, size_t, int, int, int, off_t>;
using SysMunmap = Syscall<"munmap", SYS_munmap, int, void*, size_t>;
