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

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "oi/arch/Arch.h"

extern "C" {
#include <drgn.h>
}

struct FuncDesc {
  struct TargetObject;
  struct Arg;
  struct Retval;

  std::string symName{};

  struct Range {
    uintptr_t start;
    uintptr_t end;

    Range() = default;
    Range(uintptr_t _start, uintptr_t _end) : start{_start}, end{_end} {
      assert(end >= start);
    };

    constexpr uintptr_t size() const noexcept {
      return end - start;
    }
  };

  std::vector<Range> ranges;
  std::vector<std::shared_ptr<FuncDesc::Arg>> arguments{};
  std::shared_ptr<FuncDesc::Retval> retval{};
  bool isMethod{false};

  FuncDesc() = default;
  FuncDesc(std::string func) : symName{std::move(func)} {};

  std::shared_ptr<FuncDesc::Arg> addArgument() {
    return arguments.emplace_back(std::make_shared<FuncDesc::Arg>());
  }

  std::shared_ptr<FuncDesc::TargetObject> getThis() {
    if (!isMethod) {
      return nullptr;
    }
    return arguments[0];
  }

  std::shared_ptr<FuncDesc::TargetObject> getArgument(size_t argPos) {
    // Offset by 1, as methods have 'this' at arg 0
    if (isMethod) {
      argPos += 1;
    }
    return arguments[argPos];
  }

  std::shared_ptr<FuncDesc::TargetObject> getArgument(const std::string&);
  std::optional<uint8_t> getArgumentIndex(const std::string&,
                                          bool = true) const;

  size_t numArgs() const {
    if (isMethod) {
      return arguments.size() - 1;
    }
    return arguments.size();
  }

  std::optional<Range> getRange(uintptr_t addr) {
    for (const auto& range : ranges) {
      if (addr >= range.start && addr < range.end) {
        return range;
      }
    }

    return std::nullopt;
  }

  struct TargetObject {
    bool valid = false;
    std::string typeName{};

    virtual ~TargetObject() = default;

    /* Given a register set return the address where the object position
     * can be found at the given pc (what about if we don't have this
     * location?).
     */
    virtual std::optional<uintptr_t> findAddress(const user_regs_struct* regs,
                                                 uintptr_t pc) const = 0;
  };

  struct Arg final : virtual TargetObject {
    uint8_t index;
    drgn_object_locator locator;

    ~Arg() final {
      drgn_object_locator_deinit(&locator);
    }

    std::optional<uintptr_t> findAddress(const user_regs_struct* regs,
                                         uintptr_t pc) const final;
  };

  struct Retval final : virtual TargetObject {
    ~Retval() final = default;

    std::optional<uintptr_t> findAddress(const user_regs_struct* regs,
                                         uintptr_t /* pc */) const final {
      return oi::detail::arch::getReturnValueAddress(*regs);
    }
  };
};

std::ostream& operator<<(std::ostream& os, const FuncDesc::Range& r);

class GlobalDesc {
 public:
  GlobalDesc() = default;
  GlobalDesc(std::string name, uintptr_t addr)
      : symName{std::move(name)}, baseAddr{addr} {};

  std::string symName{};
  std::string typeName{};
  uintptr_t baseAddr{0};
};
