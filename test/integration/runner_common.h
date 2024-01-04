#pragma once

#include <gtest/gtest.h>

#include <boost/asio.hpp>
#include <boost/process.hpp>
#include <boost/property_tree/ptree.hpp>
#include <filesystem>
#include <future>
#include <iostream>
#include <string>
#include <string_view>

struct OidOpts {
  boost::asio::io_context& ctx;
  std::string targetArgs;
  std::string scriptSource;
};

struct OilOpts {
  boost::asio::io_context& ctx;
  std::string targetArgs;
};

struct OilgenOpts {
  boost::asio::io_context& ctx;
  std::string_view targetSrc;
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
  std::optional<std::filesystem::path> writeCustomConfig(
      std::string_view filePrefix, std::string_view content);

  std::filesystem::path workingDir;

  std::string stdout_;
  std::string stderr_;

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

class OidIntegration : public IntegrationBase {
 protected:
  std::string TmpDirStr() override;

  OidProc runOidOnProcess(OidOpts opts,
                          std::vector<std::string> extra_args,
                          std::string configPrefix,
                          std::string configSuffix);
};

class OilIntegration : public IntegrationBase {
 protected:
  std::string TmpDirStr() override;

  Proc runOilTarget(OilOpts opts,
                    std::string configPrefix,
                    std::string configSuffix);
};

class OilgenIntegration : public IntegrationBase {
 protected:
  std::string TmpDirStr() override;

  Proc runOilgenTarget(OilgenOpts opts,
                       std::string configPrefix,
                       std::string configSuffix);
};
