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

#include <chrono>
#include <filesystem>
#include <mutex>
#include <ostream>
#include <string>
#include <vector>

namespace oi::detail::metrics {

constexpr auto traceEnvKey = "OID_METRICS_TRACE";
constexpr auto outputEnvKey = "OID_METRICS_OUTPUT";

/*
 * Control which metric are collected using the environment variable
 * OID_METRICS_TRACE. It accepts a comma-separated list of metrics: time, rss.
 * By default, no metrics are collected.
 * The metrics are written to the path specified by the environment variable
 * OID_METRICS_OUTPUT. If not specified, they are written into
 * "oid_metrics.json".
 */
struct TraceFlags {
  bool time = false;
  bool rss = false;

  operator bool() const {
    return time || rss;
  }
};

struct Span {
  uint32_t index;
  std::string name;
  int64_t duration;
  long rssBeforeBytes;
  long rssAfterBytes;
};

class Tracing final {
 private:
  /*
   * Independent static variables might be destroyed before our std::atexit()
   * handler is called, leading to an use-after-free error. Instead, we group
   * all static variables in the following structure and put our atexit()
   * handler's code in its destructor. This ensure that all variables are
   * destroyed **AFTER** the call to ~Static().
   */
  static struct Static {
    long pageSizeKB;
    TraceFlags traceEnabled;
    std::vector<Span> traces;
    std::mutex mutex;

    Static();
    ~Static();
  } static_;

  using TimePoint = std::chrono::high_resolution_clock::time_point;

 public:
  /*
   *   metrics::Tracing("bad");
   *
   * Usage is metrics::Tracing __varname_(...) The code above is an improper
   * use of the Tracing library. The tracing object above is not stored in a
   * variable. So it is immediately destroyed and won't record the expected
   * results. [[nodiscard]] flags the code above with a warning, which we
   * enforce as an error in our cmake/CompilerWarnings.cmake.
   */
  [[nodiscard]] explicit Tracing(const char* name) {
    if (!Tracing::isEnabled()) {
      return;
    }
    traceName = name;
  }

  [[nodiscard]] explicit Tracing(const std::string& name) {
    if (!Tracing::isEnabled()) {
      return;
    }
    traceName = name;
  }

  [[nodiscard]] explicit Tracing(std::string&& name) {
    if (!Tracing::isEnabled()) {
      return;
    }
    traceName = std::move(name);
  }

  Tracing() = delete;
  Tracing(const Tracing& other) : Tracing{other.traceName} {
  }
  Tracing(Tracing&&) noexcept = default;

  Tracing& operator=(Tracing&) = delete;
  Tracing& operator=(Tracing&&) = delete;

  ~Tracing() {
    if (!ended) {
      stop();
    }
  }

  void reset() {
    if (!Tracing::isEnabled()) {
      return;
    }
    startTs = fetchTime();
    rssBeforeBytes = fetchRssUsage();
  }

  void rename(const char* name) {
    if (!Tracing::isEnabled()) {
      return;
    }
    traceName = name;
  }

  void rename(const std::string& name) {
    if (!Tracing::isEnabled()) {
      return;
    }
    traceName = name;
  }

  void rename(std::string&& name) {
    if (!Tracing::isEnabled()) {
      return;
    }
    traceName = std::move(name);
  }

  void stop();

  static TraceFlags& isEnabled() {
    return static_.traceEnabled;
  }
  static const char* outputPath();
  static void saveTraces(const std::filesystem::path&);

 private:
  static uint32_t getNextIndex();
  static TimePoint fetchTime();
  static long fetchRssUsage();

  bool ended{false};
  std::string traceName{};
  TimePoint startTs{Tracing::fetchTime()};
  long rssBeforeBytes{Tracing::fetchRssUsage()};
};

std::ostream& operator<<(std::ostream&, const TraceFlags&);
std::ostream& operator<<(std::ostream&, const Span&);
std::ostream& operator<<(std::ostream&, const std::vector<Span>&);

}  // namespace oi::detail::metrics
