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

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <vector>

#include "oi/OICodeGen.h"
#include "oi/OIGenerator.h"
#include "oi/OIOpts.h"

namespace fs = std::filesystem;
using namespace oi::detail;

constexpr static OIOpts opts{
    OIOpt{'h', "help", no_argument, nullptr, "Print this message and exit."},
    OIOpt{'o', "output", required_argument, "<file>",
          "Write output(s) to file(s) with this prefix."},
    OIOpt{'c', "config-file", required_argument, "<oid.toml>",
          "Path to OI configuration file."},
    OIOpt{'d', "debug-level", required_argument, "<level>",
          "Verbose level for logging"},
    OIOpt{'j', "dump-jit", optional_argument, "<jit.cpp>",
          "Write generated code to a file (for debugging)."},
    OIOpt{'e', "exit-code", no_argument, nullptr,
          "Return a bad exit code if nothing is generated."},
    OIOpt{'p', "pic", no_argument, nullptr,
          "Generate position independent code."},
};

void usage() {
  std::cerr << "usage: oilgen ARGS INPUT_OBJECT" << std::endl;
  std::cerr << opts;

  std::cerr << std::endl
            << "You probably shouldn't be calling this application directly. "
               "It's meant to be"
            << std::endl
            << "called by a clang plugin automatically with BUCK." << std::endl;
}

int main(int argc, char* argv[]) {
  google::InitGoogleLogging(argv[0]);
  FLAGS_minloglevel = 0;
  FLAGS_stderrthreshold = 0;

  fs::path outputPath = "a.o";
  std::vector<fs::path> configFilePaths;
  fs::path sourceFileDumpPath = "";
  bool exitCode = false;
  bool pic = false;

  int c;
  while ((c = getopt_long(argc, argv, opts.shortOpts(), opts.longOpts(),
                          nullptr)) != -1) {
    switch (c) {
      case 'h':
        usage();
        return EXIT_SUCCESS;
      case 'o':
        outputPath = optarg;
        break;
      case 'c':
        configFilePaths.emplace_back(optarg);
        break;
      case 'd':
        google::LogToStderr();
        google::SetStderrLogging(google::INFO);
        google::SetVLOGLevel("*", atoi(optarg));
        // Upstream glog defines `GLOG_INFO` as 0 https://fburl.com/ydjajhz0,
        // but internally it's defined as 1 https://fburl.com/code/9fwams75
        gflags::SetCommandLineOption("minloglevel", "0");
        break;
      case 'j':
        sourceFileDumpPath = optarg != nullptr ? optarg : "jit.cpp";
        break;
      case 'e':
        exitCode = true;
        break;
      case 'p':
        pic = true;
        break;
    }
  }

  if (optind >= argc) {
    usage();
    return EXIT_FAILURE;
  }
  fs::path primaryObject = argv[optind];

  if ((setenv("DRGN_ENABLE_TYPE_ITERATOR", "1", 1)) < 0) {
    LOG(ERROR) << "Failed to set environment variable\
       DRGN_ENABLE_TYPE_ITERATOR\n";
    exit(EXIT_FAILURE);
  }

  OIGenerator oigen;

  oigen.setOutputPath(std::move(outputPath));
  oigen.setConfigFilePaths(std::move(configFilePaths));
  oigen.setSourceFileDumpPath(sourceFileDumpPath);
  oigen.setFailIfNothingGenerated(exitCode);
  oigen.setUsePIC(pic);

  SymbolService symbols(primaryObject);

  return oigen.generate(primaryObject, symbols);
}
