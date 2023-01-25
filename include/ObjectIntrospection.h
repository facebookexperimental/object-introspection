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

#include <atomic>
#include <cstddef>
#include <string>

/*
 * Library interface for Object Introspection
 *
 * On the first call for each type these library functions perform significant
 * setup. In a single-threaded context, the calling thread blocks on the first
 * call. In a multi-threaded context, the first caller blocks, and other callers
 * see Response::OIL_INITIALISING until initialisation completes.
 *
 * The options passed to library functions MUST NOT change after the first call
 * for each type. By default, this will result in Response::OIL_CHANGED_OPTIONS.
 * This check can be disabled for decreased latency by passing checkOptions as
 * false.
 *
 * Generally the only required option is configFilePath. See sample.oid.toml for
 * an example configuration file.
 *
 * -- SINGLE-THREADED
 * ObjectIntrospection::options opts = { .configFilePath = "sample.oid.toml" };
 * size_t size;
 * int responseCode = ObjectIntrospection::getObjectSize(&obj, &size, opts);
 * if (responseCode != ObjectIntrospection::Response::OIL_SUCCESS) {
 *   // handle error
 * }
 *
 * -- MULTI-THREADED (NO SETUP)
 * ObjectIntrospection::options opts = { .configFilePath = "sample.oid.toml" };
 * size_t size;
 * int responseCode = ObjectIntrospection::getObjectSize(&obj, &size, opts);
 * if (responseCode > ObjectIntrospection::Response::OIL_INITIALISING) {
 *   // handle error
 * } else if (responseCode == ObjectIntrospection::Response::OIL_SUCCESS) {
 *   // do something
 * } // do nothing if still initialising
 *
 * -- MULTI-THREADED (WITH SETUP)
 * ObjectIntrospection::options opts = { .configFilePath = "sample.oid.toml" };
 * int responseCode = ObjectIntrospection::CodegenHandler<T>::init(opts);
 * if (responseCode != ObjectIntrospection::Response::OIL_SUCCESS) {
 *   // handle error
 * }
 * size_t size;
 * int responseCode = ObjectIntrospection::getObjectSize(&obj, &size);
 * if (responseCode == ObjectIntrospection::Response::OIL_UNINITIALISED) {
 *   // handle error - impossible if successfully inited
 * }
 *
 */
namespace ObjectIntrospection {

enum Response : int {
  OIL_SUCCESS = 0,
  OIL_INITIALISING = 1,
  OIL_CHANGED_OPTIONS = 2,
  OIL_BAD_CONFIG_FILE = 3,
  OIL_SEGMENT_INIT_FAIL = 4,
  OIL_COMPILATION_FAILURE = 5,
  OIL_RELOCATION_FAILURE = 6,
  OIL_UNINITIALISED = 7,
};

#ifndef OIL_AOT_COMPILATION

struct options {
  std::string configFilePath{};
  std::string debugFilePath{};
  std::string cacheDirPath{};

  int debugLevel = 0;
  std::string sourceFileDumpPath{};

  bool layout = false;
  bool chaseRawPointers = false;
  bool generateJitDebugInfo = false;

  bool enableUpload = false;
  bool enableDownload = false;
  bool abortOnLoadFail = false;
  bool forceJIT = true;

  friend bool operator==(const options &lhs, const options &rhs);
  friend bool operator!=(const options &lhs, const options &rhs);
};

constexpr std::string_view OI_SECTION_PREFIX = ".oi.";

class OILibrary {
  friend class OILibraryImpl;

 public:
  OILibrary(void *TemplateFunc, options opt);
  ~OILibrary();
  int init();
  int getObjectSize(void *ObjectAddr, size_t *size);

  options opts;

 private:
  class OILibraryImpl *pimpl_;

  size_t (*fp)(void *) = nullptr;
};

template <class T>
class CodegenHandler {
 public:
  static int init(const options &opts = {}, bool checkOptions = true) {
    OILibrary *lib;
    return getLibrary(lib, opts, checkOptions);
  }

  static void teardown() {
    OILibrary *lib;
    if (int responseCode = getLibrary(lib);
        responseCode != Response::OIL_SUCCESS) {
      return;
    }

    getLib()->store(nullptr);
    getBoxedLib()->store(nullptr);
    delete lib;
  }

  static int getObjectSize(T *ObjectAddr, size_t *ObjectSize) {
    OILibrary *lib;
    if (int responseCode = getLibrary(lib);
        responseCode != Response::OIL_SUCCESS) {
      return responseCode;
    }

    return lib->getObjectSize((void *)ObjectAddr, ObjectSize);
  }

  static int getObjectSize(T *ObjectAddr, size_t *ObjectSize,
                           const options &opts, bool checkOptions = true) {
    OILibrary *lib;
    if (int responseCode = getLibrary(lib, opts, checkOptions);
        responseCode != Response::OIL_SUCCESS) {
      return responseCode;
    }

    return lib->getObjectSize((void *)ObjectAddr, ObjectSize);
  }

 private:
  static std::atomic<OILibrary *> *getLib() {
    static std::atomic<OILibrary *> lib = nullptr;
    return &lib;
  }

  static std::atomic<std::atomic<OILibrary *> *> *getBoxedLib() {
    static std::atomic<std::atomic<OILibrary *> *> boxedLib = nullptr;
    return &boxedLib;
  }

  static int getLibrary(OILibrary *&result) {
    std::atomic<OILibrary *> *curBoxedLib = getBoxedLib()->load();
    if (curBoxedLib == nullptr)
      return Response::OIL_UNINITIALISED;

    OILibrary *curLib = curBoxedLib->load();
    if (curLib == nullptr)
      return Response::OIL_UNINITIALISED;

    result = curLib;
    return Response::OIL_SUCCESS;
  }

  static int getLibrary(OILibrary *&result, const options &opts,
                        bool checkOptions) {
    std::atomic<OILibrary *> *curBoxedLib = getBoxedLib()->load();

    if (curBoxedLib == nullptr) {
      if (!getBoxedLib()->compare_exchange_strong(curBoxedLib, getLib())) {
        return Response::OIL_INITIALISING;
      }
      curBoxedLib = getLib();

      int (*sizeFp)(T *, size_t *) = &getObjectSize;
      void *typedFp = reinterpret_cast<void *>(sizeFp);
      OILibrary *newLib = new OILibrary(typedFp, opts);

      if (int initCode = newLib->init(); initCode != Response::OIL_SUCCESS) {
        delete newLib;
        getBoxedLib()->store(nullptr);  // allow next attempt to initialise
        return initCode;
      }

      getLib()->store(newLib);
    }

    OILibrary *curLib = curBoxedLib->load();
    if (curLib == nullptr) {
      return Response::OIL_INITIALISING;
    }

    if (checkOptions && opts != curLib->opts) {
      return Response::OIL_CHANGED_OPTIONS;
    }

    result = curLib;
    return Response::OIL_SUCCESS;
  }
};

/*
 * Call this from anywhere in your program. It blocks on the first call for
 * each type when seen for the first time. Usage patterns are given at the
 * top of this file. This method should not be called when utilising
 * Ahead-Of-Time (AOT) compilation.
 */
template <class T>
int getObjectSize(T *ObjectAddr, size_t *ObjectSize, const options &opts,
                  bool checkOptions = true) {
  return CodegenHandler<T>::getObjectSize(ObjectAddr, ObjectSize, opts,
                                          checkOptions);
}

#endif

/*
 * You may only call this after a call to the previous signature, or a
 * call to CodegenHandler<T>::init(...) for the used type.
 *
 * As we can choose to compile the OIL blob Ahead-Of-Time (AOT) rather
 * than Just-In-Time (JIT), this default is provided as a weak symbol.
 * When in AOT mode this will no-op, removing the burden of JIT on a
 * production system.
 */
template <class T>
int __attribute__((weak)) getObjectSize(T *ObjectAddr, size_t *ObjectSize) {
#ifdef OIL_AOT_COMPILATION
  return Response::OIL_UNINITIALISED;
#else
  return CodegenHandler<T>::getObjectSize(ObjectAddr, ObjectSize);
#endif
}

}  // namespace ObjectIntrospection
