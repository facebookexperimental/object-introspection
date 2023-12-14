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
#include <gflags/gflags.h>
#include <glog/logging.h>

#include <boost/scope_exit.hpp>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

extern "C" {
#include <getopt.h>
#include <libgen.h>
}

#include "oi/Config.h"
#include "oi/Features.h"
#include "oi/Metrics.h"
#include "oi/OIDebugger.h"
#include "oi/OIOpts.h"
#include "oi/PaddingHunter.h"
#include "oi/Portability.h"
#include "oi/TimeUtils.h"
#include "oi/TreeBuilder.h"

#if OI_PORTABILITY_META_INTERNAL()
#include <folly/init/Init.h>
#endif

namespace oi::detail {

/* Global for signal handling */
std::weak_ptr<OIDebugger> weak_oid;

// Using an enum inside a namespace here instead of an `enum class` because
// enums defined via `enum class` aren't implicitly convertible to `int`, and
// having to cast the argument for each call to `exit` would be ugly.
namespace ExitStatus {
enum ExitStatus {
  Success = EXIT_SUCCESS,
  UsageError,
  FileNotFoundError,
  ConfigGenerationError,
  ScriptParsingError,
  StopTargetError,
  SegmentRemovalError,
  SegmentInitError,
  CompilationError,
  PatchingError,
  ProcessingTargetDataError,
  OidObjectError,
  CacheUploadError,
};
}

/*
 * This is the main driver code for the Object Introspection (OI) debugger.
 * The 'oid' debugger is the driver application which instruments a target
 * application to collect data and then reaps that data from the target.
 *
 * The flow of work in 'oid' can, roughly speaking, be split into several
 * phases:
 *
 *  Phase 1 - Object Discovery
 *    Using the 'drgn' debugger library, discover the container types in a
 *    given parent object and its descendent objects. With this information we
 *    can locate the addresses in memory of these container objects.
 *
 *  Phase 2 - Code Generation
 *    Auto generate C++ code to iterate over the data structures of interest,
 *    calculate the size of these objects and record the data.
 *
 *  Phase 3 - Object Code Generation
 *    JIT compile the C++ code into object code and relocate the resulting
 *    text into the traget processes address space. This is done using
 *    clang/llvm APIs.
 *
 *  Phase 4 - Target Process Instrumentation
 *    The generated object code is injected into the target process in a text
 *    segment created apriori. Threads are captured and controlled at probe
 *    sites using breakpoint traps and the ptrace(2) interfaces.
 *
 *  Phase 5 - Data processing
 *    The results are retrieved from the target processes data buffer and
 *    processed. The data buffer is a data segment that we mapped into the
 *    target process.
 *
 *    In addition to the above phases we have process control which is
 *    currently based around ptrace(2).
 */

constexpr static OIOpts opts{
    OIOpt{'h', "help", no_argument, nullptr, "Print this message and exit"},
    OIOpt{
        'p', "pid", required_argument, "<pid>", "Target process to attach to"},
    OIOpt{
        'c', "config-file", required_argument, nullptr, "</path/to/oid.toml>"},
    OIOpt{'x',
          "data-buf-size",
          required_argument,
          "<bytes>",
          "Size of data segment (default:1MB)\n"
          "Accepts multiplicative suffix: K, M, G, T, P, E"},
    OIOpt{'d',
          "debug-level",
          required_argument,
          "<level>",
          "Verbose level for logging"},
    OIOpt{'r',
          "remove-mappings",
          no_argument,
          nullptr,
          "Remove oid mappings from target process"},
    OIOpt{'s', "script", required_argument, nullptr, "</path/to/script.oid>"},
    OIOpt{'S', "script-source", required_argument, nullptr, "type:symbol:arg"},
    OIOpt{'t',
          "timeout",
          required_argument,
          "<seconds>",
          "How long to probe the target process for"},
    OIOpt{'k',
          "custom-code-file",
          required_argument,
          nullptr,
          "</path/to/code.cpp>\n"
          "Use your own CPP file instead of CodeGen"},
    OIOpt{'e',
          "compile-and-exit",
          no_argument,
          nullptr,
          "Compile only then exit"},
    OIOpt{'o',
          "cache-path",
          required_argument,
          "<path>",
          "Enable caching using the provided directory"},
    OIOpt{'u',
          "cache-remote",
          required_argument,
          nullptr,
          "Enable upload/download of cache files\n"
          "Pick from {both,upload,download}"},
    OIOpt{'i',
          "debug-path",
          required_argument,
          nullptr,
          "</path/to/binary>\n"
          "Run oid on a executable with debug infos instead of a running "
          "process"},
    // Optional arguments are pretty nasty - it will only work as
    // "--dump-json=PATH" and not "--dump-json PATH". Try and make this take a
    // required argument at a later point
    OIOpt{'J',
          "dump-json",
          optional_argument,
          "[oid_out.json]",
          "File to dump the results to, as JSON\n"
          "(in addition to the default RocksDB output)"},
    OIOpt{
        'B',
        "dump-data-segment",
        no_argument,
        nullptr,
        "Dump the data segment's content, before TreeBuilder processes it\n"
        "Each argument gets its own dump file: 'dataseg.<oid-pid>.<arg>.dump'"},
    OIOpt{'a', "log-all-structs", no_argument, nullptr, "Log all structures"},
    OIOpt{'m',
          "mode",
          required_argument,
          "MODE",
          "Allows to specify a mode of operation/group of settings"},
    OIOpt{
        'f', "enable-feature", required_argument, "FEATURE", "Enable feature"},
    OIOpt{'F',
          "disable-feature",
          required_argument,
          "FEATURE",
          "Disable feature"},
};

void usage() {
  std::cerr << "usage: oid ...\n";
  std::cerr << opts << std::endl;
  featuresHelp(std::cerr);

  std::cerr << std::endl << "MODES SUMMARY" << std::endl;
  std::cerr << "  prod    Disable drgn, enable remote caching, and chase raw "
               "pointers."
            << std::endl;
  std::cerr << "  strict  Enable additional fatal error conditions such as "
               "TreeBuilder reading too little data."
            << std::endl;

  std::cerr << "\n\tFor problem reporting, questions and general comments "
               "please pop along"
               "\n\tto the Object Introspection Workplace group at "
               "https://fburl.com/oid.\n"
            << std::endl;
}

/*
 * This handler currently isn't completely async-signal-safe. It's mostly
 * all in the segment removal code and is commented in appropriate places.
 * The error messages are obviously not safe either.
 */
void sigIntHandler(int sigNum) {
  VLOG(1) << "Received SIGNAL " << sigNum;

  if (auto oid = weak_oid.lock()) {
    oid->stopAll();
  } else {
    /*
     * A small window exists between install a handler and creating the main
     * debugger object.
     */
    LOG(ERROR) << "Failed to find oid object when handling signal";
    exit(ExitStatus::OidObjectError);
  }
}

void installSigHandlers(void) {
  struct sigaction nact {};
  struct sigaction oact {};

  nact.sa_handler = sigIntHandler;
  sigemptyset(&nact.sa_mask);
  nact.sa_flags = SA_SIGINFO;

  sigaction(SIGINT, nullptr, &oact);
  if (oact.sa_handler != SIG_IGN) {
    sigaction(SIGINT, &nact, nullptr);
  }

  /* Also stop on SIGALRM, for handling timeout */
  sigaction(SIGALRM, &nact, nullptr);
}

std::optional<long> strunittol(const char* str) {
  errno = 0;
  char* strend = nullptr;
  long retval = strtol(str, &strend, 10);
  if (errno != 0) {
    return std::nullopt;
  }
  switch (*strend) {
    case 'E':
      retval *= 1024;
      [[fallthrough]];
    case 'P':
      retval *= 1024;
      [[fallthrough]];
    case 'T':
      retval *= 1024;
      [[fallthrough]];
    case 'G':
      retval *= 1024;
      [[fallthrough]];
    case 'M':
      retval *= 1024;
      [[fallthrough]];
    case 'K':
      retval *= 1024;
      if (*(strend + 1) != '\0') {
        return std::nullopt;
      }
      [[fallthrough]];
    case '\0':
      break;
    default:
      return std::nullopt;
  }
  return retval;
}

namespace Oid {

struct Config {
  pid_t pid;
  std::string debugInfoFile;
  std::vector<fs::path> configFiles;
  fs::path cacheBasePath;
  fs::path customCodeFile;
  size_t dataSegSize;
  int timeout_s;
  bool cacheRemoteUpload;
  bool cacheRemoteDownload;
  bool removeMappings;
  bool compAndExit;
  bool genPaddingStats = true;
  bool attachToProcess = true;
  bool hardDisableDrgn = false;
  bool strict = false;
};

}  // namespace Oid

static ExitStatus::ExitStatus runScript(
    const std::string& fileName,
    std::istream& script,
    const Oid::Config& oidConfig,
    const OICodeGen::Config& codeGenConfig,
    const OICompiler::Config& compilerConfig,
    const TreeBuilder::Config& tbConfig) {
  if (!fileName.empty()) {
    VLOG(1) << "SCR FILE: " << fileName;
  }

  auto progStart = time_hr::now();

  std::shared_ptr<OIDebugger> oid;  // share oid with the global signal handler
  if (oidConfig.pid != 0) {
    oid = std::make_shared<OIDebugger>(
        oidConfig.pid, codeGenConfig, compilerConfig, tbConfig);
  } else {
    oid = std::make_shared<OIDebugger>(
        oidConfig.debugInfoFile, codeGenConfig, compilerConfig, tbConfig);
  }
  weak_oid = oid;  // set the weak_ptr for signal handlers

  if (!oidConfig.cacheBasePath.empty()) {
    oid->setCacheBasePath(oidConfig.cacheBasePath);
  }

  oid->setCacheRemoteEnabled(oidConfig.cacheRemoteUpload,
                             oidConfig.cacheRemoteDownload);
  if (!oid->validateCache()) {
    return ExitStatus::UsageError;
  }
  oid->setCustomCodeFile(oidConfig.customCodeFile);
  oid->setHardDisableDrgn(oidConfig.hardDisableDrgn);
  oid->setStrict(oidConfig.strict);

  VLOG(1) << "OIDebugger constructor took " << std::dec
          << time_ns(time_hr::now() - progStart) << " nsecs";

  LOG(INFO) << "Script file: " << fileName;

  if (!oid->parseScript(script)) {
    LOG(ERROR) << "Error parsing input file '" << fileName << "'";
    return ExitStatus::ScriptParsingError;
  }

  if (oidConfig.attachToProcess && !oid->stopTarget()) {
    LOG(ERROR) << "Couldn't stop target process with PID " << oidConfig.pid;
    return ExitStatus::StopTargetError;
  }
  auto initStart = time_hr::now();

  /*
   * Remove any existing mappings if the '-r' flag is used or if any of the
   * segments have been explicitly changed on the command line. It's a bit of
   * a heavy hammer to remove both text and data if only one of the relevant
   * parameters have been set but that can always be modified in the future
   * if necessary.
   */
  if (oidConfig.attachToProcess) {
    if (oidConfig.removeMappings) {
      ExitStatus::ExitStatus ret = ExitStatus::Success;

      if (!oid->segConfigExists()) {
        LOG(INFO) << "No config exists for pid " << oidConfig.pid
                  << " : cannot remove mappings";
        ret = ExitStatus::UsageError;
      } else if (!oid->unmapSegments(true)) {
        LOG(ERROR) << "Failed to remove segments in target process with PID "
                   << oidConfig.pid;
        ret = ExitStatus::SegmentRemovalError;
      }

      oid->contTargetThread();
      return ret;
    }

    if (oidConfig.dataSegSize > 0) {
      oid->setDataSegmentSize(oidConfig.dataSegSize);
    }

    if (!oid->segmentInit()) {
      oid->contTargetThread();
      LOG(ERROR) << "Failed to initialise segments in target process with PID "
                 << oidConfig.pid;
      return ExitStatus::SegmentInitError;
    }

    // continue and detach main thread
    oid->contTargetThread();
  }

  VLOG(1) << "init took " << std::dec << time_ns(time_hr::now() - initStart)
          << " nsecs\n"
          << "Compilation Started";

  auto compileStart = time_hr::now();

  if (!oid->compileCode()) {
    LOG(ERROR) << "Compilation failed";
    return ExitStatus::CompilationError;
  }

  VLOG(1) << "Compilation Finished (" << std::dec
          << time_ns(time_hr::now() - compileStart) << " nsecs)";

  if (oidConfig.compAndExit) {
    // Ensure the .th cache file also gets created
    oid->getTreeBuilderTyping();

    if (oidConfig.genPaddingStats) {
      PaddingHunter paddingHunter;
      paddingHunter.localPaddedStructs = oid->getPaddingInfo();
      paddingHunter.processLocalPaddingInfo();
      paddingHunter.outputPaddingInfo();
    }
  } else {
    installSigHandlers();

    /*
     * Sigh. This is nonsense really and is tied to a single probe enabling.
     * This will need re-architecting when we move to multiple enablings.
     */
    if (!oid->isGlobalDataProbeEnabled()) {
      oid->setMode(OIDebugger::OID_MODE_FUNC);
    }

    /*
     * I think we might be able to just fit the global variable work entirely
     * under patchFunctions and therefore leave the shape of the code at
     * this level pretty much unaltered.
     */
    if (!oid->stopTarget()) {
      LOG(ERROR) << "Couldn't stop target process with PID " << oidConfig.pid;
      return ExitStatus::StopTargetError;
    }

    if (!oid->patchFunctions()) {
      oid->contTargetThread();
      LOG(ERROR) << "Error patching functions";
      return ExitStatus::PatchingError;
    }

    oid->contTargetThread(false);

    if (oidConfig.timeout_s > 0) {
      alarm(oidConfig.timeout_s);
    }

    while (!oid->isInterrupted()) {
      if (oid->processTrap(oidConfig.pid) == OIDebugger::OID_DONE) {
        break;
      }
    };

    // Disable timeout timer
    alarm(0);

    // Cleanup all the remaining traps that were injected
    if (!oid->removeTraps(0)) {
      LOG(ERROR) << "Failed to remove instrumentation...";
    }

    {  // Resume stopped thread before cleanup
      VLOG(1) << "Resuming stopped threads...";
      metrics::Tracing __("resume_threads");
      while (oid->processTrap(oidConfig.pid, false) == OIDebugger::OID_CONT) {
      }
    }

    oid->restoreState();

    if (!oid->isInterrupted() && !oid->processTargetData()) {
      LOG(ERROR) << "Problems processing target data";
      return ExitStatus::ProcessingTargetDataError;
    }
  }

  // Upload cache artifacts if present
  if (!oid->uploadCache()) {
    LOG(ERROR) << "cache upload requested and failed";
    return ExitStatus::CacheUploadError;
  }

  std::cout << "SUCCESS " << fileName << std::endl;
  VLOG(1) << "Entire process took " << time_ns(time_hr::now() - progStart)
          << " nsecs";
  return ExitStatus::Success;
}

}  // namespace oi::detail

int main(int argc, char* argv[]) {
  using namespace oi::detail;

  int debugLevel = 1;
  Oid::Config oidConfig = {};
  std::string scriptFile;
  std::string scriptSource;
  std::string configGenOption;
  std::optional<fs::path> jsonPath{std::nullopt};

  std::map<Feature, bool> features = {
      {Feature::PackStructs, true},
      {Feature::GenPaddingStats, true},
      {Feature::TypeGraph, true},
      {Feature::PruneTypeGraph, true},
  };

  bool logAllStructs = true;
  bool dumpDataSegment = false;

  metrics::Tracing _("main");

#if OI_PORTABILITY_META_INTERNAL()
  folly::InitOptions init;
  init.useGFlags(false);
  init.removeFlags(false);
  folly::init(&argc, &argv, init);
#else
  google::InitGoogleLogging(argv[0]);
#endif
  google::SetStderrLogging(google::WARNING);

  int c = 0;
  while ((c = getopt_long(
              argc, argv, opts.shortOpts(), opts.longOpts(), nullptr)) != -1) {
    switch (c) {
      case 'F':
        [[fallthrough]];
      case 'f':
        if (auto f = featureFromStr(optarg); f != Feature::UnknownFeature) {
          features[f] = c == 'f';  // '-f' enables, '-F' disables
        } else {
          LOG(ERROR) << "Invalid feature: " << optarg << " specified!";
          usage();
          return ExitStatus::UsageError;
        }
        break;

      case 'm': {
        if (strcmp("prod", optarg) == 0) {
          // change default settings for prod
          oidConfig.hardDisableDrgn = true;
          oidConfig.cacheRemoteDownload = true;
          oidConfig.cacheBasePath = "/tmp/oid-cache";
          features[Feature::ChaseRawPointers] = true;
        } else if (strcmp("strict", optarg) == 0) {
          oidConfig.strict = true;
        } else {
          LOG(ERROR) << "Invalid mode: " << optarg << " specified!";
          usage();
          return ExitStatus::UsageError;
        }
        break;
      }
      case 'x': {
        auto dataSegSizeArg = strunittol(optarg);
        if (!dataSegSizeArg.has_value() || dataSegSizeArg.value() <= 0) {
          LOG(ERROR) << "Invalid value specified for data buffer size";
          usage();
          return ExitStatus::UsageError;
        }
        oidConfig.dataSegSize = static_cast<size_t>(dataSegSizeArg.value());
        break;
      }
      case 'p':
        oidConfig.pid = atoi(optarg);
        break;
      case 'd':
        debugLevel = atoi(optarg);
        google::LogToStderr();
        google::SetStderrLogging(google::INFO);
        google::SetVLOGLevel("*", debugLevel);
        // Upstream glog defines `GLOG_INFO` as 0 https://fburl.com/ydjajhz0,
        // but internally it's defined as 1 https://fburl.com/code/9fwams75
        gflags::SetCommandLineOption("minloglevel", "0");
        break;
      case 'k':
        oidConfig.customCodeFile = optarg;

        if (!fs::exists(oidConfig.customCodeFile)) {
          LOG(ERROR) << "Non existent generated code file: "
                     << oidConfig.customCodeFile;
          usage();
          return ExitStatus::FileNotFoundError;
        }

        if (oidConfig.customCodeFile == "/tmp/tmp_oid_output_2.cpp") {
          LOG(ERROR) << "Cannot use generatedCodePath:"
                     << oidConfig.customCodeFile;
          return ExitStatus::UsageError;
        }

        break;
      case 'e':
        oidConfig.compAndExit = true;
        break;
      case 'c':
        oidConfig.configFiles.emplace_back(optarg);

        if (!fs::exists(oidConfig.configFiles.back())) {
          LOG(ERROR) << "Non existent config file: "
                     << oidConfig.configFiles.back();
          usage();
          return ExitStatus::FileNotFoundError;
        }

        break;
      case 'i':
        oidConfig.debugInfoFile = std::string(optarg);
        oidConfig.attachToProcess = false;
        oidConfig.compAndExit = true;

        if (!fs::exists(oidConfig.debugInfoFile)) {
          LOG(ERROR) << "Non existent debuginfo file: "
                     << oidConfig.debugInfoFile;
          usage();
          return ExitStatus::FileNotFoundError;
        }

        break;
      case 'o':
        oidConfig.cacheBasePath = optarg;
        break;
      case 'u':
        if (strcmp(optarg, "both") == 0) {
          oidConfig.cacheRemoteUpload = true;
          oidConfig.cacheRemoteDownload = true;
        } else if (strcmp(optarg, "upload") == 0) {
          oidConfig.cacheRemoteUpload = true;
        } else if (strcmp(optarg, "download") == 0) {
          oidConfig.cacheRemoteDownload = true;
        } else {
          LOG(ERROR) << "Invalid download option: " << optarg << " specified!";
          usage();
          return ExitStatus::UsageError;
        }
        break;
      case 'r':
        oidConfig.removeMappings = true;
        break;
      case 'a':
        logAllStructs = true;
        break;
      case 'B':
        dumpDataSegment = true;
        break;
      case 's':
        scriptFile = std::string(optarg);
        break;
      case 'S':
        scriptSource = std::string(optarg);
        break;
      case 't':
        oidConfig.timeout_s = atoi(optarg);
        break;
      case 'J':
        jsonPath = optarg != nullptr ? optarg : "oid_out.json";
        break;
      case 'h':
      default:
        usage();
        return ExitStatus::Success;
    }
  }

  if (oidConfig.pid != 0 && !oidConfig.debugInfoFile.empty()) {
    LOG(INFO) << "'-p' and '-b' are mutually exclusive";
    usage();
    return ExitStatus::UsageError;
  }

  if ((oidConfig.pid == 0 && oidConfig.debugInfoFile.empty())) {
    usage();
    return ExitStatus::UsageError;
  }

  if (!oidConfig.removeMappings && scriptFile.empty() && scriptSource.empty()) {
    LOG(INFO) << "One of '-s', '-r' or '-S' must be specified";
    usage();
    return ExitStatus::UsageError;
  }

  /*
   * This is unfortunately necessary to stop users having to specify a script
   * just to remove mappings (which doesn't make sense).
   */
  if (oidConfig.removeMappings && scriptFile.empty() && scriptSource.empty()) {
    scriptSource = "entry:unknown_function:arg0";
  }

  OICompiler::Config compilerConfig{};

  OICodeGen::Config codeGenConfig;
  codeGenConfig.features = {};  // fill in after processing the config file

  TreeBuilder::Config tbConfig{
      .features = {},  // fill in after processing the config file
      .logAllStructs = logAllStructs,
      .dumpDataSegment = dumpDataSegment,
      .jsonPath = jsonPath,
  };

  auto featureSet = config::processConfigFiles(
      oidConfig.configFiles, features, compilerConfig, codeGenConfig);
  if (!featureSet) {
    return ExitStatus::UsageError;
  }
  compilerConfig.features = *featureSet;
  codeGenConfig.features = *featureSet;
  tbConfig.features = *featureSet;

  if (!scriptFile.empty()) {
    if (!std::filesystem::exists(scriptFile)) {
      LOG(ERROR) << "Non-existent script file: " << scriptFile;
      return ExitStatus::FileNotFoundError;
    }
    std::ifstream script(scriptFile);
    auto status = runScript(
        scriptFile, script, oidConfig, codeGenConfig, compilerConfig, tbConfig);
    if (status != ExitStatus::Success) {
      return status;
    }
  } else if (!scriptSource.empty()) {
    std::istringstream script(scriptSource);
    auto status = runScript(
        scriptFile, script, oidConfig, codeGenConfig, compilerConfig, tbConfig);
    if (status != ExitStatus::Success) {
      return status;
    }
  }

  if (metrics::Tracing::isEnabled()) {
    LOG(INFO) << "Will write metrics (" << metrics::Tracing::isEnabled()
              << ") in " << metrics::Tracing::outputPath();
  } else {
    LOG(INFO) << "Will not write any metric: " << metrics::Tracing::isEnabled();
  }

  return ExitStatus::Success;
}
