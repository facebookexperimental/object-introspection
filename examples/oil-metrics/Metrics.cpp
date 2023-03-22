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
#include "Metrics.h"

namespace Metrics {
std::atomic<Metrics*> Metrics::singleton = nullptr;

const char* to_string(ArgTiming t) {
  switch (t) {
    case ENTRY:
      return "entry";
    case EXIT:
      return "exit";
    default:
      return "";
  }
}

Metrics::Metrics(ObjectIntrospection::options opts, const std::string& savePath)
    : opts(opts) {
  writer = std::fstream(savePath, std::ios_base::out);
  writer << "{ \"metrics\": [" << std::endl;

  // set the singleton to this once fully constructed
  Metrics::singleton = this;
}

Metrics::~Metrics() {
  writer << "]}" << std::endl;
}

void Metrics::save(std::string object) {
  Metrics* m = singleton.load();
  std::lock_guard<std::mutex> guard(m->writerLock);

  if (m->hasWritten) {
    m->writer << ',' << object << std::endl;
  } else {
    m->hasWritten = true;
    m->writer << object << std::endl;
  }
}

void Metrics::saveArg(const char* name, const char* argName, ArgTiming timing,
                      size_t size) {
  std::string out = "{\"type\": \"size\", \"traceName\": \"";
  out += name;
  out += "\", \"argName\": \"";
  out += argName;
  out += "\", \"timing\": \"";
  out += to_string(timing);
  out += "\", \"size\": ";
  out += std::to_string(size);
  out += '}';

  save(out);
}

void Metrics::saveDuration(const char* name,
                           std::chrono::milliseconds duration) {
  std::string out = "{\"type\": \"duration\", \"traceName\": \"";
  out += name;
  out += "\", \"duration\": ";
  out += std::to_string(duration.count());
  out += '}';

  save(out);
}

Tracing::Tracing(const char* name, bool enabled)
    : name(name), enabled(enabled) {
}

Tracing::~Tracing() {
  if (isTimingEnabled()) {
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - startTime);
    saveDuration(duration);
  }

  for (auto const& exitFunc : exitFuncs)
    exitFunc();
}

bool Tracing::isTimingEnabled() {
  return enabled || Metrics::isEnabled();
}

bool Tracing::isArgEnabled(const char* argName, ArgTiming timing) {
  return enabled || Metrics::isEnabled();
}

void Tracing::start() {
  if (isTimingEnabled()) {
    startTime = std::chrono::high_resolution_clock::now();
  }
}

void Tracing::saveArg(const char* argName, ArgTiming timing, size_t size) {
  Metrics::saveArg(name, argName, timing, size);
}

void Tracing::saveDuration(std::chrono::milliseconds duration) {
  Metrics::saveDuration(name, duration);
}
}  // namespace Metrics
