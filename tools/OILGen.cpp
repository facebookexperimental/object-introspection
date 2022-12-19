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

#include <glog/logging.h>

#include <filesystem>
#include <iostream>

#include "OICodeGen.h"
#include "OIGenerator.h"
#include "OIOpts.h"

namespace fs = std::filesystem;
using namespace ObjectIntrospection;

constexpr static OIOpts opts{
    OIOpt{'h', "help", no_argument, nullptr, "Print this message and exit."},
    OIOpt{'o', "output", required_argument, "<file>", "Write output to file."},
    OIOpt{'c', "config-file", required_argument, "<oid.toml>",
          "Path to OI configuration file."},
    OIOpt{'d', "dump-jit", optional_argument, "<jit.cpp>",
          "Write generated code to a file (for debugging)."},
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
  fs::path configFilePath = "/usr/local/share/oi/base.oid.toml";
  fs::path sourceFileDumpPath = "";

  int c;
  while ((c = getopt_long(argc, argv, opts.shortOpts(), opts.longOpts(),
                          nullptr)) != -1) {
    switch (c) {
      case 'o':
        outputPath = optarg;
        break;
      case 'c':
        configFilePath = optarg;
        break;
      case 'd':
        sourceFileDumpPath = optarg != nullptr ? optarg : "jit.cpp";
        break;
    }
  }

  if (optind >= argc) {
    usage();
    return EXIT_FAILURE;
  }
  fs::path primaryObject = argv[optind];

  OIGenerator oigen;

  oigen.setOutputPath(std::move(outputPath));
  oigen.setConfigFilePath(std::move(configFilePath));
  oigen.setSourceFileDumpPath(sourceFileDumpPath);

  return oigen.generate(primaryObject);
}
