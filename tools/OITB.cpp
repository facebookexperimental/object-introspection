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

#include <boost/archive/text_iarchive.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <span>

#include "glog/vlog_is_on.h"
#include "oi/OIOpts.h"
#include "oi/PaddingHunter.h"
#include "oi/Serialize.h"
#include "oi/TreeBuilder.h"
#include "oi/TypeHierarchy.h"

namespace fs = std::filesystem;

using namespace oi::detail;

constexpr static OIOpts opts{
    OIOpt{'h', "help", no_argument, nullptr, "Print this message and exit"},
    OIOpt{'a',
          "log-all-structs",
          no_argument,
          nullptr,
          "Enable TreeBuilder::Config::logAllStructs (=true)\n"
          "Note: this option is already enabled, this is a no-op"},
    OIOpt{'J',
          "dump-json",
          optional_argument,
          "[oid_out.json]",
          "File to dump the results to, as JSON\n"
          "(in addition to the default RocksDB output)"},
    OIOpt{
        'f', "enable-feature", required_argument, "FEATURE", "Enable feature"},
    OIOpt{'F',
          "disable-feature",
          required_argument,
          "FEATURE",
          "Disable feature"},
};

static void usage(std::ostream& out) {
  out << "Run TreeBuilder on the given data-segment dump.\n";
  out << "oitb aims to facilitate debugging issues from TreeBuilder, by "
         "allowing local iterations and debugging.\n";
  out << "The options -a, -n, -w must match the settings used when collecting "
         "the data-segment dump.\n";

  out << "\nusage: oitb [opts...] [--] <path-th> <path-pd> "
         "<path-dataseg-dump>\n";
  out << opts << std::endl;
  featuresHelp(out);

  out << std::endl;
}

template <typename... Args>
[[noreturn]] static void fatal_error(Args&&... args) {
  std::cerr << "error: ";
  (std::cerr << ... << args);
  std::cerr << "\n\n";

  usage(std::cerr);
  exit(EXIT_FAILURE);
}

static auto loadTypeInfo(fs::path thPath, fs::path pdPath) {
  std::string cacheBuildId;  // We don't check the build-id
  std::pair<RootInfo, TypeHierarchy> th;
  std::map<std::string, PaddingInfo> pd;

  { /* Load TypeHierarchy */
    std::ifstream ifs(thPath);
    boost::archive::text_iarchive ia(ifs);

    ia >> cacheBuildId;
    ia >> th;
  }

  { /* Load PaddingInfo */
    std::ifstream ifs(pdPath);
    boost::archive::text_iarchive ia(ifs);

    ia >> cacheBuildId;
    ia >> pd;
  }

  return std::make_tuple(th.first, th.second, pd);
}

static auto loadDatasegDump(fs::path datasegDumpPath) {
  std::vector<uint64_t> dataseg;
  std::ifstream dump(datasegDumpPath);

  { /* Allocate enough memory to hold the dump */
    dump.seekg(0, std::ios_base::end);
    auto dumpSize = dump.tellg();
    dump.seekg(0, std::ios_base::beg);

    assert(dumpSize % sizeof(decltype(dataseg)::value_type) == 0);
    dataseg.resize(dumpSize / sizeof(decltype(dataseg)::value_type));
  }

  { /* Read the dump into the dataseg vector */
    auto datasegBytes = std::as_writable_bytes(std::span{dataseg});
    dump.read((char*)datasegBytes.data(), datasegBytes.size_bytes());
  }

  return dataseg;
}

[[maybe_unused]] /* For debugging... */
static std::ostream&
operator<<(std::ostream& out, TreeBuilder::Config tbc) {
  out << "TreeBuilde::Config = [";
  out << "\n  logAllStructs = " << tbc.logAllStructs;
  out << "\n  chaseRawPointers = " << tbc.features[Feature::ChaseRawPointers];
  out << "\n  genPaddingStats = " << tbc.features[Feature::GenPaddingStats];
  out << "\n  dumpDataSegment = " << tbc.dumpDataSegment;
  out << "\n  jsonPath = " << (tbc.jsonPath ? *tbc.jsonPath : "NONE");
  out << "\n]\n";
  return out;
}

int main(int argc, char* argv[]) {
  google::InitGoogleLogging(*argv);
  google::LogToStderr();
  google::SetStderrLogging(google::INFO);
  google::SetVLOGLevel("TreeBuilder", 3);
  gflags::SetCommandLineOption("minloglevel", "0");

  /* Reflects `oid`'s defaults for TreeBuilder::Config */
  TreeBuilder::Config tbConfig{
      .features = {Feature::PackStructs, Feature::GenPaddingStats},
      .logAllStructs = true,
      .dumpDataSegment = false,
      .jsonPath = std::nullopt,
  };

  int c = '\0';
  while ((c = getopt_long(
              argc, argv, opts.shortOpts(), opts.longOpts(), nullptr)) != -1) {
    switch (c) {
      case 'h':
        usage(std::cout);
        exit(EXIT_SUCCESS);

      case 'F':
        [[fallthrough]];
      case 'f':
        if (auto f = featureFromStr(optarg); f != Feature::UnknownFeature)
          tbConfig.features[f] = c == 'f';  // '-f' enables, '-F' disables
        else
          fatal_error("Invalid feature specified: ", optarg);
        break;

      case 'a':
        tbConfig.logAllStructs =
            true;  // Weird that we're setting it to true, again...
        break;
      case 'J':
        tbConfig.jsonPath = optarg ? optarg : "oid_out.json";
        break;

      case ':':
        fatal_error("missing option argument");
      case '?':
        fatal_error("invalid option");
      default:
        fatal_error("invalid option");
    }
  }

  if (argc - optind < 3)
    fatal_error("missing arguments");
  else if (argc - optind > 3)
    fatal_error("too many arguments");

  LOG(INFO) << "Loading type infos...";
  auto [rootType, typeHierarchy, paddingInfos] =
      loadTypeInfo(argv[optind + 0], argv[optind + 1]);

  LOG(INFO) << "Loading data segment dump...";
  auto dataseg = loadDatasegDump(argv[optind + 2]);

  TreeBuilder typeTree(tbConfig);

  if (tbConfig.features[Feature::GenPaddingStats]) {
    LOG(INFO) << "Setting-up PaddingHunter...";
    typeTree.setPaddedStructs(&paddingInfos);
  }

  LOG(INFO) << "Running TreeBuilder...";
  typeTree.build(dataseg, rootType.varName, rootType.type.type, typeHierarchy);

  if (tbConfig.jsonPath) {
    LOG(INFO) << "Writting JSON results to " << *tbConfig.jsonPath << "...";
    typeTree.dumpJson();
  }

  return EXIT_SUCCESS;
}
