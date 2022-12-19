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

extern "C" {
#include <drgn.h>
}

#include <filesystem>

#include "OICodeGen.h"
#include "OICompiler.h"
#include "OILibraryImpl.h"
#include "OIUtils.h"
#include "ObjectIntrospection.h"

namespace fs = std::filesystem;

static void assert_drgn_succeeded(struct drgn_error *err, const char *message) {
  if (err) {
    LOG(ERROR) << message << ": " << err->message;
    exit(EXIT_FAILURE);
  }
}

static bool compile_type(
    struct drgn_qualified_type *type, uintptr_t function_addr,
    OICompiler::Config &compilerConfig, OICodeGen::Config &generatorConfig,
    std::vector<std::pair<std::string, std::string>> &results) {
  std::string code =
#include "OITraceCode.cpp"
      ;

  auto codegen = OICodeGen::buildFromConfig(generatorConfig);
  if (!codegen) {
    return false;
  }

  codegen->setRootType(*type);
  if (!codegen->generate(code)) {
    return false;
  }

  const auto identifier =
      ObjectIntrospection::function_identifier(function_addr);
  if (drgn_type_has_name(type->type)) {
    VLOG(1) << "Identifier for type '" << drgn_type_name(type->type) << "' is "
            << identifier;
  }
  OICompiler compiler{{}, compilerConfig};
  auto source_path =
      identifier +
      OICache::extensions[static_cast<size_t>(OICache::Entity::Source)];
  auto object_path =
      identifier +
      OICache::extensions[static_cast<size_t>(OICache::Entity::Object)];
  if (!compiler.compile(code, source_path, object_path)) {
    return false;
  }
  results.emplace_back(identifier, object_path);
  return true;
}

int main(int argc, char *argv[]) {
  google::LogToStderr();
  google::InitGoogleLogging("oi_compile");
  // Rudimentary argument parsing:
  // argv[1] is the executable we want to perform codegen for
  // argv[2] is an optional configuration file
  if (argc <= 1) {
    LOG(ERROR)
        << "Please provide the file to perform codegen for as an argument";
    return EXIT_FAILURE;
  }
  if (argc > 3) {
    LOG(ERROR) << "Unrecognized command line arguments, oi_compile accepts at "
                  "most two arguments (the file to perform codegen for, and "
                  "optionally a config file)";
    return EXIT_FAILURE;
  }
  const char *target = argv[1];
  if (!fs::exists(target)) {
    LOG(ERROR) << target << ": file does not exist";
    return EXIT_FAILURE;
  }
  if (!fs::is_regular_file(target)) {
    LOG(ERROR) << target << ": not a regular file";
    return EXIT_FAILURE;
  }
  const char *config_file =
      argc == 3 ? argv[2] : "/usr/local/share/oi/base.oid.toml";
  if (!fs::exists(config_file)) {
    LOG(ERROR) << config_file << ": file does not exist";
    return EXIT_FAILURE;
  }

  // OI initialization
  OICompiler::Config compilerConfig{};
  OICodeGen::Config generatorConfig{};
  if (!OIUtils::processConfigFile(config_file, compilerConfig,
                                  generatorConfig)) {
    LOG(ERROR) << "Failed to process config file";
    return EXIT_FAILURE;
  }
  generatorConfig.useDataSegment = false;

  // drgn initialization
  char envVar[] = "RGN_ENABLE_TYPE_ITERATOR=1";
  if (putenv(envVar) == -1) {
    PLOG(ERROR)
        << "Failed to set environment variable DRGN_ENABLE_TYPE_ITERATOR";
    return EXIT_FAILURE;
  }
  struct drgn_program *prog;
  assert_drgn_succeeded(drgn_program_create(NULL, &prog),
                        "Error while initializing drgn program");
  assert_drgn_succeeded(
      drgn_program_load_debug_info(prog, &target, 1, false, false),
      "Error while loading debug info");
  struct drgn_type_iterator *types;
  assert_drgn_succeeded(drgn_oil_type_iterator_create(prog, &types),
                        "Error while creating type iterator");

  // Perform codegen for each type
  std::vector<std::pair<std::string, std::string>> results{};
  struct drgn_qualified_type *type;
  uintptr_t *function_addrs;
  std::size_t function_addrs_len;
  assert_drgn_succeeded(drgn_oil_type_iterator_next(
                            types, &type, &function_addrs, &function_addrs_len),
                        "Error while advancing type iterator");
  while (type) {
    for (std::size_t i = 0; i < function_addrs_len; i++) {
      auto function_addr = function_addrs[i];

      if (!compile_type(type, function_addr, compilerConfig, generatorConfig,
                        results)) {
        if (drgn_type_has_name(type->type)) {
          LOG(ERROR) << "Compilation failed for type '"
                     << drgn_type_name(type->type) << "'";
        } else {
          LOG(ERROR)
              << "Compilation failed for type with template function address 0x"
              << std::hex << function_addr;
        }
        return EXIT_FAILURE;
      }
    }
    free(function_addrs);
    assert_drgn_succeeded(
        drgn_oil_type_iterator_next(types, &type, &function_addrs,
                                    &function_addrs_len),
        "Error while advancing type iterator");
  }
  drgn_type_iterator_destroy(types);

  // Finally, create a new executable by copying the given executable
  // and appending the object files created by codegen to the new
  // executable as ELF sections, with one section for each type.
  std::string output = target + std::string("_oil");
  if (fs::exists(output)) {
    LOG(WARNING) << output << " already exists, overwriting it";
    fs::remove(output);
  }
  fs::copy(target, output);
  std::string command = "objcopy";
  for (auto &[identifier, object_path] : results) {
    command += " --add-section ";
    command += ObjectIntrospection::OI_SECTION_PREFIX;
    command += identifier;
    command += "=";
    command += object_path;
  }
  command += " ";
  command += output;
  int status = system(command.c_str());

  // Delete temporary files now that we're done with them
  for (auto &[_, object_path] : results) {
    fs::remove(object_path);
  }

  if (status == -1) {
    PLOG(ERROR) << "Failed to launch subprocess";
    return EXIT_FAILURE;
  } else if (status != 0) {
    LOG(ERROR) << "objcopy exited with non-zero exit code " << status;
    return EXIT_FAILURE;
  }
}
