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
#include <cassert>
#include <list>
#include <memory>
#include <string>
#include <vector>

struct irequest {
  irequest(std::string t, std::string f, std::string a) noexcept
      : type(std::move(t)), func(std::move(f)), arg(std::move(a)) {
  }

  const std::string type{}, func{}, arg{};

  [[nodiscard]] bool isReturnRetVal() const noexcept {
    return type == "return" && arg == "retval";
  }

  [[nodiscard]] const std::string toString() const {
    return type + ":" + func + ":" + arg;
  }
};

namespace std {

template <>
struct hash<irequest> {
  std::size_t operator()(const irequest& req) const noexcept {
    auto h = hash<std::string>();
    return h(req.type) ^ h(req.func) ^ h(req.arg);
  }
};

template <>
struct equal_to<irequest> {
  bool operator()(const irequest& lhs, const irequest& rhs) const noexcept {
    return lhs.type == rhs.type && lhs.func == rhs.func && lhs.arg == rhs.arg;
  }
};

}  // namespace std

struct prequest {
  prequest(std::string t, std::string f, std::vector<std::string> as) noexcept
      : type(std::move(t)), func(std::move(f)), args(std::move(as)) {
  }

  const std::string type{}, func{};
  const std::vector<std::string> args{};

  [[nodiscard]] irequest getReqForArg(size_t idx = 0) const {
    if (type == "global")
      return {type, func, ""};

    assert(idx < args.size());
    return {type, func, args[idx]};
  }
};

class ParseData {
 private:
  using RequestVector = std::vector<prequest>;
  RequestVector reqs{};

 public:
  void addReq(std::string type, std::string func, std::list<std::string> args) {
    // Convert the args std::list into a more efficient std::vector
    reqs.emplace_back(std::move(type),
                      std::move(func),
                      std::vector(std::make_move_iterator(args.begin()),
                                  std::make_move_iterator(args.end())));
  }

  size_t numReqs() const noexcept {
    return reqs.size();
  }

  [[nodiscard]] const prequest& getReq(size_t idx = 0) const noexcept {
    assert(idx < reqs.size());
    return reqs[idx];
  }

  /* Delegate iterator to the RequestVector */
  using iterator = RequestVector::iterator;
  using const_iterator = RequestVector::const_iterator;

  iterator begin() noexcept {
    return reqs.begin();
  }
  const_iterator begin() const noexcept {
    return reqs.begin();
  }
  const_iterator cbegin() const noexcept {
    return reqs.begin();
  }

  iterator end() noexcept {
    return reqs.end();
  }
  const_iterator end() const noexcept {
    return reqs.end();
  }
  const_iterator cend() const noexcept {
    return reqs.end();
  }
};
