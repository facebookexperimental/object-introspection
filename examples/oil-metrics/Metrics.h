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
#include <ObjectIntrospection.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace Metrics {
enum ArgTiming {
  ENTRY,
  EXIT,
};

class Metrics {
  friend class Tracing;

 public:
  Metrics(ObjectIntrospection::options opts, const std::string& savePath);
  ~Metrics();

  void enable() {
    enableAll = true;
  }

 private:
  static std::atomic<Metrics*> singleton;

  ObjectIntrospection::options opts;
  std::fstream writer;
  std::mutex writerLock;
  bool hasWritten = false;
  bool enableAll = false;

  static ObjectIntrospection::options& getOptions() {
    return singleton.load()->opts;
  }

  static bool isEnabled() {
    return singleton.load()->enableAll;
  }

  static void save(std::string object);
  static void saveArg(const char* name,
                      const char* argName,
                      ArgTiming timing,
                      size_t size);
  static void saveDuration(const char* name,
                           std::chrono::milliseconds duration);
};

class Tracing {
 public:
  Tracing(const char* name, bool enabled = false);
  ~Tracing();

  void start();

  template <class T>
  void registerArg(const char* argName, T* value);

 private:
  bool isTimingEnabled();
  bool isArgEnabled(const char* argName, ArgTiming timing);
  void saveArg(const char* argName, ArgTiming timing, size_t size);
  void saveDuration(std::chrono::milliseconds duration);

  template <class T>
  void inspectArg(const char* argName, ArgTiming timing, T* value);

  const char* name;
  bool enabled;
  std::chrono::high_resolution_clock::time_point startTime;
  std::vector<std::function<void()>> exitFuncs;
};

template <class T>
void Tracing::registerArg(const char* argName, T* value) {
  if (isArgEnabled(argName, ArgTiming::ENTRY)) {
    inspectArg(argName, ArgTiming::ENTRY, value);
  }

  if (isArgEnabled(argName, ArgTiming::EXIT)) {
    if (exitFuncs.capacity() == 0)
      exitFuncs.reserve(8);

    std::function<void()> exitFunc = [this, argName, value]() {
      inspectArg(argName, ArgTiming::EXIT, value);
    };

    exitFuncs.push_back(exitFunc);
  }
}

template <class T>
void Tracing::inspectArg(const char* argName, ArgTiming timing, T* value) {
  size_t size;
  if (int responseCode = ObjectIntrospection::getObjectSize(
          value, &size, Metrics::getOptions(), false);
      responseCode > ObjectIntrospection::Response::OIL_INITIALISING) {
    throw std::runtime_error("object introspection failed");
  } else if (responseCode == ObjectIntrospection::Response::OIL_INITIALISING) {
    return;  // do nothing to avoid blocking
  }

  saveArg(argName, timing, size);
}
}  // namespace Metrics
