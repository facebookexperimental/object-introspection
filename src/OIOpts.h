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

#include <array>
#include <cstring>
#include <iomanip>
#include <ostream>

extern "C" {
#include <getopt.h>
}

struct OIOpt {
  char shortName;
  const char *longName;
  int has_arg;
  const char *argName;
  const char *usage;
};

template <size_t N>
class OIOpts {
 public:
  template <typename... Opts>
  constexpr explicit OIOpts(Opts &&...options)
      : _opts{std::forward<decltype(options)>(options)...} {
    // Create the short opts string
    size_t shortOptIndex = 0;
    for (const auto &opt : _opts) {
      _shortOpts[shortOptIndex++] = opt.shortName;
      for (int i = 0; i < opt.has_arg; ++i)
        _shortOpts[shortOptIndex++] = ':';
    }

    // Pad the remaining with NULL bytes
    while (shortOptIndex < _shortOpts.size())
      _shortOpts[shortOptIndex++] = '\0';

    // Create the array of long opts
    for (size_t i = 0; i < _opts.size(); ++i) {
      const auto &opt = _opts[i];
      _longOpts[i] = {opt.longName, opt.has_arg, nullptr, opt.shortName};
    }

    // Add empty record to mark the end of long opts
    _longOpts[_opts.size()] = {nullptr, no_argument, nullptr, '\0'};
  }

  constexpr const char *shortOpts() const {
    return _shortOpts.data();
  }
  constexpr const struct option *longOpts() const {
    return _longOpts.data();
  }

  template <size_t M>
  friend std::ostream &operator<<(std::ostream &os, const OIOpts<M> &opts);

 private:
  std::array<OIOpt, N> _opts;
  std::array<char, 3 * N + 1> _shortOpts{};
  std::array<struct option, N + 1> _longOpts{};
};

template <size_t M>
std::ostream &operator<<(std::ostream &os, const OIOpts<M> &opts) {
  int maxLongName = 0;
  for (const auto &opt : opts._opts) {
    size_t longNameWidth = strlen(opt.longName);
    if (opt.argName)
      longNameWidth += 1 + strlen(opt.argName);
    maxLongName = std::max(maxLongName, (int)longNameWidth);
  }

  for (const auto &opt : opts._opts) {
    auto fullName = std::string(opt.longName);
    if (opt.argName) {
      fullName += ' ';
      fullName += opt.argName;
    }

    os << "  -" << opt.shortName << ",--";
    os << std::setw(maxLongName) << std::left;
    os << fullName << "  ";

    std::string_view usage = opt.usage;
    std::string_view::size_type old_pos = 0, new_pos = 0;
    while ((new_pos = usage.find('\n', old_pos)) != std::string::npos) {
      os << usage.substr(old_pos, new_pos - old_pos + 1);
      os << std::setw(maxLongName + 9) << ' ';
      old_pos = new_pos + 1;
    }
    os << usage.substr(old_pos) << '\n';
  }

  return os;
}

template <typename... Opts>
OIOpts(Opts... opts) -> OIOpts<sizeof...(opts)>;
