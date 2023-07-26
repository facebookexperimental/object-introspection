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
#include "oi/Metrics.h"

#include <unistd.h>

#include <cstring>
#include <fstream>

/*
 * NOTA BENE: Metrics are disabled by default. They are enabled by setting the
 * 'OID_METRICS_TRACE' environment variable as detailed in Metrics.h.
 *
 * This small library to track how much time and other resources we are
 * spending on different phases of the execution of OI.
 *
 * You can instrument some code with:
 * ```
 * auto t = metrics::Tracing("name_of_your_trace");
 * [... some code ...]
 * t.stop();
 * ```
 *
 * The code above will create a new trace that, when the `.stop()` method
 * is called will be appended to the list of traces.
 *
 * Alternatively, you can use automatically deal with this in every return
 * point thanks to C++'s RAII:
 * ```
 * metrics::Tracing unused_var("name_of_your_trace");
 * ```
 *
 * When you want to collect the data, `::showTraces()` to print the data to
 * stdout, and `::saveTraces(file)` to save it to disk using JSON.
 */
namespace oi::detail::metrics {

static inline TraceFlags parseTraceFlags(const char* flags) {
  if (flags == nullptr) {
    return TraceFlags{};
  }
  return {
      .time = strcasestr(flags, "time") != nullptr,
      .rss = strcasestr(flags, "rss") != nullptr,
  };
}

Tracing::Static Tracing::static_{};

Tracing::Static::Static() {
  traceEnabled = parseTraceFlags(std::getenv(traceEnvKey));

  errno = 0;
  if (auto pageSizeBytes = sysconf(_SC_PAGESIZE); pageSizeBytes > 0) {
    pageSizeKB = pageSizeBytes / 1024;
  } else {
    std::perror("Failed to retrieve page size");
  }
}

Tracing::Static::~Static() {
  if (!traceEnabled) {
    return;
  }

  Tracing::saveTraces(Tracing::outputPath());
  traces.clear();
}

uint32_t Tracing::getNextIndex() {
  // NOTE: we already own the lock on static_.traces
  return static_cast<uint32_t>(static_.traces.size());
}

Tracing::TimePoint Tracing::fetchTime() {
  if (!static_.traceEnabled.time) {
    return TimePoint{};
  }

  return std::chrono::high_resolution_clock::now();
}

long Tracing::fetchRssUsage() {
  if (!static_.traceEnabled.rss) {
    return 0;
  }

  std::ifstream statStream("/proc/self/stat");

  // Placeholders as we don't care about these at the minute. There are more
  // fields in /stat that we don't have here
  /* std::string pid, comm, state, ppid, pgrp, session, tty_nr, tpgid, flags,
         minflt, cminflt, majflt, cmajflt, utime, stime, cutime, cstime,
         priority, nice, num_threads, itrealvalue, starttime, vsize;
   */
  for (size_t i = 0; i < 23; ++i) {
    statStream.ignore(std::numeric_limits<std::streamsize>::max(), ' ');
  }

  // We care about this field
  long rss = 0;
  statStream >> rss;

  return rss * static_.pageSizeKB;
}

void Tracing::stop() {
  ended = true;
  if (!static_.traceEnabled) {
    return;
  }

  using namespace std::chrono;
  auto stopTs = fetchTime();
  auto duration = duration_cast<nanoseconds>(stopTs - startTs);
  auto rssAfterBytes = fetchRssUsage();

  std::lock_guard<std::mutex> guard{static_.mutex};
  // Can't use emplace_back() because of old clang++ on CI
  static_.traces.push_back({getNextIndex(), std::move(traceName),
                            duration.count(), rssBeforeBytes, rssAfterBytes});
}

void Tracing::saveTraces(const std::filesystem::path& output) {
  std::ofstream osf{output};
  if (!osf) {
    perror("Failed to open output file");
    return;
  }

  osf << "[";
  for (const auto& span : static_.traces) {
    if (span.index > 0) {
      osf << ",";
    }

    osf << "{\"name\":\"" << span.name << "\"";
    osf << ",\"index\":" << span.index;

    if (static_.traceEnabled.time) {
      osf << ",\"duration_ns\":" << span.duration;
    }

    if (static_.traceEnabled.rss) {
      osf << ",\"rss_before_bytes\":" << span.rssBeforeBytes;
      osf << ",\"rss_after_bytes\":" << span.rssAfterBytes;
    }

    osf << "}";
  }
  osf << "]\n";
}

const char* Tracing::outputPath() {
  const char* output = std::getenv(outputEnvKey);

  if (output == nullptr) {
    output = "oid_metrics.json";
  }
  return output;
}

std::ostream& operator<<(std::ostream& out, const TraceFlags& tf) {
  if (tf.time && tf.rss) {
    out << "time, rss";
  } else if (tf.time) {
    out << "time";
  } else if (tf.rss) {
    out << "rss";
  } else {
    out << "disabled";
  }
  return out;
}

std::ostream& operator<<(std::ostream& out, const Span& span) {
  out << "Span for: " << span.name << " (" << span.index << ")\n";
  out << "  Duration: " << span.duration << " ns\n";
  out << "  RSS before: " << span.rssBeforeBytes << " bytes\n";
  out << "  RSS after: " << span.rssAfterBytes << " bytes\n";
  return out;
}

std::ostream& operator<<(std::ostream& out, const std::vector<Span>& spans) {
  out << "Showing all spans:\n";
  out << "==================\n\n";

  for (const auto& span : spans) {
    out << span;
  }
  return out;
}

}  // namespace oi::detail::metrics
