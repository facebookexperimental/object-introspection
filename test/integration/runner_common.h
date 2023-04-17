#pragma once

#include <gtest/gtest.h>

#include <boost/asio.hpp>
#include <boost/process.hpp>
#include <boost/property_tree/ptree.hpp>
#include <filesystem>
#include <future>
#include <iostream>
#include <string>

struct OidOpts {
  boost::asio::io_context& ctx;
  std::string targetArgs;
  std::string script;
  std::string scriptSource;
};

struct Proc {
  boost::asio::io_context& ctx;
  boost::process::child proc;
  boost::process::child tee_stdout;
  boost::process::child tee_stderr;
};

struct OidProc {
  Proc target;
  Proc oid;
};

class IntegrationBase : public ::testing::Test {
 protected:
  virtual ~IntegrationBase() = default;

  virtual std::string TmpDirStr() = 0;

  void TearDown() override;
  void SetUp() override;
  int exit_code(Proc& proc);
  std::filesystem::path createCustomConfig(const std::string& prefix,
                                           const std::string& suffix);

  std::filesystem::path workingDir;

  std::string stdout_;
  std::string stderr_;
};

class OidIntegration : public IntegrationBase {
 protected:
  std::string TmpDirStr() override;

  OidProc runOidOnProcess(OidOpts opts, std::vector<std::string> extra_args,
                          std::string configPrefix, std::string configSuffix);

  /*
   * compare_json
   *
   * Compares two JSON objects for equality if "expect_eq" is true, or for
   * inequality if "expect_eq" is false.
   */
  void compare_json(const boost::property_tree::ptree& expected_json,
                    const boost::property_tree::ptree& actual_json,
                    const std::string& full_key = "root",
                    bool expect_eq = true);
};

class OilIntegration : public IntegrationBase {
 protected:
  std::string TmpDirStr() override;

  Proc runOilTarget(OidOpts opts, std::string configPrefix,
                    std::string configSuffix);
};
