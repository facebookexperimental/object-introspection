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
#include "oi/Descs.h"

#include <glog/logging.h>

#include <algorithm>
#include <boost/scope_exit.hpp>
#include <charconv>
#include <iostream>
#include <utility>

extern "C" {
#include <drgn.h>
#include <sys/user.h>
}

std::ostream& operator<<(std::ostream& os, const FuncDesc::Range& r) {
  return os << (void*)r.start << ':' << (void*)r.end;
}

/*
 * Given a register set return the address where the supplied argument
 * position can be found at the given pc (what about if we don't have this
 * location?).
 */
std::optional<uintptr_t> FuncDesc::Arg::findAddress(
    const user_regs_struct* regs, uintptr_t pc) const {
  user_regs_struct modifiedRegs = *regs;
  oi::detail::arch::setProgramCounter(modifiedRegs, pc);

  struct drgn_object object {};
  BOOST_SCOPE_EXIT_ALL(&) {
    drgn_object_deinit(&object);
  };

  if (auto* err = drgn_object_locate(&locator, &modifiedRegs, &object)) {
    LOG(ERROR) << "Error while finding address of argument: " << err->message;
    drgn_error_destroy(err);
  } else {
    return object.address;
  }

  LOG(WARNING) << "failed to locate argument with drgn! failing over to naive "
                  "argument location";
  return oi::detail::arch::naiveReadArgument(*regs, index);
}

std::optional<uint8_t> FuncDesc::getArgumentIndex(const std::string& arg,
                                                  bool validateIndex) const {
  if (arg == "retval") {
    return std::nullopt;
  }

  if (arg == "this") {
    if (!isMethod) {
      LOG(ERROR) << "Function " << symName << " has no 'this' parameter";
      return std::nullopt;
    }
    return 0;
  }
  //
  // Extract arg's number
  auto it = arg.find_first_of("0123456789");
  if (it == std::string::npos) {
    LOG(ERROR) << "Invalid argument: " << arg;
    return std::nullopt;
  }

  const auto* argIdxBegin = arg.data() + it;
  const auto* argIdxEnd = arg.data() + arg.size();

  uint8_t argIdx = 0;
  if (auto res = std::from_chars(argIdxBegin, argIdxEnd, argIdx);
      res.ec != std::errc{}) {
    LOG(ERROR) << "Failed to convert " << arg
               << " digits: " << strerror((int)res.ec);
    return std::nullopt;
  }

  // Check and offset for methods
  if (validateIndex && argIdx >= numArgs()) {
    LOG(ERROR) << "Argument index " << (int)argIdx
               << " too large. Args count: " << numArgs();
    return std::nullopt;
  }

  if (isMethod) {
    argIdx += 1;
  }

  return argIdx;
}

std::shared_ptr<FuncDesc::TargetObject> FuncDesc::getArgument(
    const std::string& arg) {
  std::shared_ptr<FuncDesc::TargetObject> outArg;

  if (arg == "retval") {
    outArg = retval;
  } else {
    auto argIdx = getArgumentIndex(arg);
    if (!argIdx.has_value()) {
      return nullptr;
    }

    outArg = arguments[*argIdx];
  }

  if (!outArg || !outArg->valid) {
    LOG(ERROR) << "Argument " << arg << " for " << symName << " is invalid";
    return nullptr;
  }

  return outArg;
}
