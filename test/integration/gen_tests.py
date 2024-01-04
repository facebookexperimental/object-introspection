import json
import os
import pathlib
import sys

import toml


def is_thrift_test(config):
    return "thrift_definitions" in config


def get_case_name(test_suite, test_case):
    return f"{test_suite}_{test_case}"


def get_target_oid_func_name(test_suite, test_case):
    case_name = get_case_name(test_suite, test_case)
    return f"oid_test_case_{case_name}"


def get_target_oil_func_name(test_suite, test_case):
    case_name = get_case_name(test_suite, test_case)
    return f"oil_test_case_{case_name}"


def get_namespace(test_suite):
    return f"ns_{test_suite}"


def add_headers(f, custom_headers, thrift_headers):
    f.write(
        """
#include <boost/current_function.hpp>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
#include <tuple>

#include <oi/oi.h>
#include <oi/exporters/Json.h>
#include <oi/result/SizedResult.h>

"""
    )
    for header in custom_headers:
        f.write(f"#include <{header}>\n")

    for header in thrift_headers:
        f.write(f'#include "{header}"\n')


def add_test_getters(f, case_name, case):
    param_types = ", ".join(
        f"std::remove_cvref_t<{param}>" for param in case["param_types"]
    )
    if "arg_types" in case:
        arg_types = ", ".join(case["arg_types"])
    else:
        arg_types = param_types

    f.write(
        f"\n"
        f"  std::tuple<{arg_types}> get_{case_name}() {{\n"
        f'{case["setup"]}\n'
        f"  }}\n"
    )


def get_param_str(param, i):
    if "]" in param:
        # Array param

        if ")" in param:
            # "int(&)[5]" ->  "int (&a0)[5]"
            start, end = param.split(")")
            return f"{start}a{i}){end}"

        # "int[5]" -> "int a0[5]"
        # "int[5][10]" -> "int a0[5][10]"
        type_name, array_size = param.split("[", 1)
        return f"{type_name} a{i}[{array_size}"

    # Non-array param, e.g. "int&" -> "int& a0"
    return f"{param} a{i}"


def add_test_setup(f, config):
    ns = get_namespace(config["suite"])
    # fmt: off
    f.write(
        f"\n"
        f'{config.get("raw_definitions", "")}\n'
        f"namespace {ns} {{\n"
        f"#pragma clang diagnostic push\n"
        f"#pragma clang diagnostic ignored \"-Wunused-private-field\"\n"
        f'{config.get("definitions", "")}\n'
        f"#pragma clang diagnostic pop\n"
    )
    # fmt: on

    def define_traceable_func(name, params, body):
        return (
            f"\n"
            # f'  extern "C" {{\n'
            f'  extern "C" void __attribute__((noinline)) {name}({params}) {{\n'
            f"{body}"
            f"  }}\n"
            # f"  }}\n"
        )

    cases = config["cases"]
    for case_name, case in cases.items():
        if "target_function" in case:
            # This test case is using a custom function - don't generate a
            # target func for it
            continue

        add_test_getters(f, case_name, case)

        # generate oid and oil targets
        params_str = ", ".join(
            get_param_str(param, i) for i, param in enumerate(case["param_types"])
        )

        oid_func_body = (
            '    std::cout << "(" << getpid() << ") " << BOOST_CURRENT_FUNCTION << " ["'
        )
        for i in range(len(case["param_types"])):
            oid_func_body += f' << " " << (uintptr_t)&a{i}'
        oid_func_body += ' << "]" << std::endl;\n'

        f.write(
            define_traceable_func(
                get_target_oid_func_name(config["suite"], case_name),
                params_str,
                oid_func_body,
            )
        )

        oil_func_body = (
            f"    oi::GeneratorOptions opts{{\n"
            f"      .configFilePaths = configFiles,\n"
            f'      .sourceFileDumpPath = "oil_jit_code.cpp",\n'
            f"      .debugLevel = 3,\n"
            f"    }};\n\n"
        )

        oil_func_body += "    auto pr = oi::exporters::Json(std::cout);\n"
        oil_func_body += "    pr.setPretty(true);\n"
        for i in range(len(case["param_types"])):
            oil_func_body += f"    auto ret{i} = oi::result::SizedResult(*oi::setupAndIntrospect(a{i}, opts));\n"
            oil_func_body += f"    pr.print(ret{i});\n"

        f.write(
            define_traceable_func(
                get_target_oil_func_name(config["suite"], case_name),
                params_str,
                oil_func_body,
            )
        )

    f.write(f"}} // namespace {ns}\n")


def add_common_code(f):
    f.write(
        """
void usage(const std::string &argv0) {
  std::cerr << "usage: " << argv0 << " oid CASE ITERATIONS" << std::endl;
  std::cerr << "       " << argv0 << " oil CASE CONFIG_FILE..." << std::endl;
}

int main(int argc, char *argv[]) {
  if (argc < 4) {
    usage(argv[0]);
    return -1;
  }

  std::string mode = argv[1];
  std::string test_case = argv[2];

  int iterations = 1;

  if (mode == "oid") {
    if (argc != 4) {
      usage(argv[0]);
      return -1;
    }
    std::istringstream iss(argv[3]);
    iss >> iterations;
    if (iss.fail()) {
      usage(argv[0]);
      return -1;
    }
  }
  else if (mode == "oil") {
    for (int i = 3; i < argc; i++) {
      configFiles.emplace_back(argv[i]);
    }
  }
  else {
    usage(argv[0]);
    return -1;
  }

"""
    )


def add_dispatch_code(f, config):
    ns = get_namespace(config["suite"])
    for case_name in config["cases"]:
        case_str = get_case_name(config["suite"], case_name)
        oil_func_name = get_target_oil_func_name(config["suite"], case_name)
        oid_func_name = get_target_oid_func_name(config["suite"], case_name)
        f.write(
            f'  if (test_case == "{case_str}") {{\n'
            f"    auto val = {ns}::get_{case_name}();\n"
            f"    for (int i=0; i<iterations; i++) {{\n"
            f'      if (mode == "oil") {{\n'
            f"        std::apply({ns}::{oil_func_name}, val);\n"
            f"      }} else {{\n"
            f"        std::apply({ns}::{oid_func_name}, val);\n"
            f"      }}\n"
            f"      std::this_thread::sleep_for(std::chrono::milliseconds(100));\n"
            f"    }}\n"
            f"    return 0;\n"
            f"  }}\n"
        )


def add_footer(f):
    f.write(
        """
  std::cerr << "Unknown test case: " << argv[1] << " " << argv[2] << std::endl;
  return -1;
}
"""
    )


def gen_target(output_target_name, test_configs):
    with open(output_target_name, "w") as f:
        headers = set()
        thrift_headers = []
        for config in test_configs:
            headers.update(config.get("includes", []))
            if is_thrift_test(config):
                thrift_headers += [
                    f"thrift/annotation/gen-cpp2/{config['suite']}_types.h"
                ]
        add_headers(f, sorted(headers), thrift_headers)
        f.write("std::vector<std::filesystem::path> configFiles;")

        for config in test_configs:
            add_test_setup(f, config)
        add_common_code(f)
        for config in test_configs:
            add_dispatch_code(f, config)
        add_footer(f)


def get_probe_name(probe_type, func_name, args):
    return probe_type + ":" + func_name + ":" + args


def add_tests(f, config):
    for case_name, case in config["cases"].items():
        add_oid_integration_test(f, config, case_name, case)
        add_oil_integration_test(f, config, case_name, case)
        add_oilgen_integration_test(f, config, case_name, case)


def add_oid_integration_test(f, config, case_name, case):
    probe_type = case.get("type", "entry")
    args = case.get("args", "arg0")

    func_name = get_target_oid_func_name(config["suite"], case_name)
    if "target_function" in case:
        func_name = case["target_function"]

    probe_str = get_probe_name(probe_type, func_name, args)
    case_str = get_case_name(config["suite"], case_name)
    exit_code = case.get("expect_oid_exit_code", 0)
    cli_options = (
        "{" + ", ".join(f'"{option}"' for option in case.get("cli_options", ())) + "}"
    )

    config_prefix = case.get("config_prefix", "")
    config_suffix = case.get("config_suffix", "")

    f.write(
        f"\n"
        f"TEST_F(OidIntegration, {case_str}) {{\n"
        f"{generate_skip(case, 'oid')}"
        f'  std::string configPrefix = R"--({config_prefix})--";\n'
        f'  std::string configSuffix = R"--({config_suffix})--";\n'
        f"  ba::io_context ctx;\n"
        f"  auto [target, oid] = runOidOnProcess(\n"
        f"        {{\n"
        f"          .ctx = ctx,\n"
        f'          .targetArgs = "oid {case_str}",\n'
        f'          .scriptSource = "{probe_str}",\n'
        f"        }},\n"
        f"        {cli_options},\n"
        f"        std::move(configPrefix), std::move(configSuffix));\n"
        f"  ASSERT_EQ(exit_code(oid), {exit_code});\n"
        f"  EXPECT_EQ(target.proc.running(), true);\n"
    )

    if "expect_json" in case:
        try:
            json.loads(case["expect_json"])
        except json.decoder.JSONDecodeError as error:
            print(
                f"\x1b[31m`expect_json` value for test case {config['suite']}.{case_name} was invalid JSON: {error}\x1b[0m",
                file=sys.stderr,
            )
            sys.exit(1)

        f.write(
            f"\n"
            f"  std::stringstream expected_json_ss;\n"
            f'  expected_json_ss << R"--({case["expect_json"]})--";\n'
            f"  bpt::ptree expected_json, actual_json;\n"
            f"  bpt::read_json(expected_json_ss, expected_json);\n"
            f'  bpt::read_json("oid_out.json", actual_json);\n'
            f"  compare_json(expected_json, actual_json);\n"
        )

    if "expect_stdout" in case:
        f.write(
            f'  std::string stdout_regex = R"--({case["expect_stdout"]})--";\n'
            f"  EXPECT_THAT(stdout_, MatchesRegex(stdout_regex));\n"
        )
    if "expect_stderr" in case:
        f.write(
            f'  std::string stderr_regex = R"--({case["expect_stderr"]})--";\n'
            f"  EXPECT_THAT(stderr_, MatchesRegex(stderr_regex));\n"
        )
    if "expect_not_stdout" in case:
        f.write(
            f'  std::string not_stdout_regex = R"--({case["expect_not_stdout"]})--";\n'
            f"  EXPECT_THAT(stdout_, Not(MatchesRegex(not_stdout_regex)));\n"
        )
    if "expect_not_stderr" in case:
        f.write(
            f'  std::string not_stderr_regex = R"--({case["expect_not_stderr"]})--";\n'
            f"  EXPECT_THAT(stderr_, Not(MatchesRegex(not_stderr_regex)));\n"
        )

    f.write(f"}}\n")


def add_oil_integration_test(f, config, case_name, case):
    case_str = get_case_name(config["suite"], case_name)
    exit_code = case.get("expect_oil_exit_code", 0)

    if "oil_disable" in case or "target_function" in case:
        return

    config_prefix = case.get("config_prefix", "")
    config_suffix = case.get("config_suffix", "")

    f.write(
        f"\n"
        f"TEST_F(OilIntegration, {case_str}) {{\n"
        f"{generate_skip(case, 'oil')}"
        f'  std::string configPrefix = R"--({config_prefix})--";\n'
        f'  std::string configSuffix = R"--({config_suffix})--";\n'
        f"  ba::io_context ctx;\n"
        f"  auto target = runOilTarget({{\n"
        f"    .ctx = ctx,\n"
        f'    .targetArgs = "oil {case_str}",\n'
        f"  }}, std::move(configPrefix), std::move(configSuffix));\n\n"
        f"  ASSERT_EQ(exit_code(target), {exit_code});\n"
    )

    key = "expect_json"
    if "expect_json_v2" in case:
        key = "expect_json_v2"
    if key in case:
        try:
            json.loads(case[key])
        except json.decoder.JSONDecodeError as error:
            print(
                f"\x1b[31m`expect_json` value for test case {config['suite']}.{case_name} was invalid JSON: {error}\x1b[0m",
                file=sys.stderr,
            )
            sys.exit(1)

        f.write(
            f"\n"
            f"  std::stringstream expected_json_ss;\n"
            f'  expected_json_ss << R"--({case[key]})--";\n'
            f"  auto result_json_ss = std::stringstream(stdout_);\n"
            f"  bpt::ptree expected_json, actual_json;\n"
            f"  bpt::read_json(expected_json_ss, expected_json);\n"
            f"  bpt::read_json(result_json_ss, actual_json);\n"
            f"  compare_json(expected_json, actual_json);\n"
        )

    f.write(f"}}\n")


def add_oilgen_integration_test(f, config, case_name, case):
    case_str = get_case_name(config["suite"], case_name)
    exit_code = case.get("expect_oil_exit_code", 0)

    if "oil_disable" in case or "target_function" in case:
        return

    config_prefix = case.get("config_prefix", "")
    config_suffix = case.get("config_suffix", "")

    f.write(
        f"\n"
        f"TEST_F(OilgenIntegration, {case_str}) {{\n"
        f"{generate_skip(case, 'oil')}"
    )

    f.write('  constexpr std::string_view targetSrc = R"--(')
    headers = set(config.get("includes", []))
    thrift_headers = [f"thrift/annotation/gen-cpp2/{config['suite']}_types.h"] if is_thrift_test(config) else []
    add_headers(f, sorted(headers), thrift_headers)

    f.write(
        f"\n"
        f'{config.get("raw_definitions", "")}\n'
        f"#pragma clang diagnostic push\n"
        f'#pragma clang diagnostic ignored "-Wunused-private-field"\n'
        f'{config.get("definitions", "")}\n'
        f"#pragma clang diagnostic pop\n"
    )
    add_test_getters(f, case_name, case)

    main = "int main() {\n"
    main += "  auto pr = oi::exporters::Json(std::cout);\n"
    main += "  pr.setPretty(true);\n"
    main += f"  auto val = get_{case_name}();\n"
    for i in range(len(case["param_types"])):
        main += f"  auto ret{i} = oi::result::SizedResult(oi::introspect"
        if "arg_types" in case:
            main += f"<std::decay_t<{case['param_types'][i]}>>"
        main += f"(std::get<{i}>(val)));\n"
        main += f"  pr.print(ret{i});\n"
    main += "}\n"

    f.write(main)
    f.write(')--";\n')

    f.write(
        f'  std::string configPrefix = R"--({config_prefix})--";\n'
        f'  std::string configSuffix = R"--({config_suffix})--";\n'
        f"  ba::io_context ctx;\n"
        f"  auto target = runOilgenTarget({{\n"
        f"    .ctx = ctx,\n"
        f"    .targetSrc = targetSrc,\n"
        f"  }}, std::move(configPrefix), std::move(configSuffix));\n\n"
        f"  ASSERT_EQ(exit_code(target), {exit_code});\n"
    )

    key = "expect_json"
    if "expect_json_v2" in case:
        key = "expect_json_v2"
    if key in case:
        try:
            json.loads(case[key])
        except json.decoder.JSONDecodeError as error:
            print(
                f"\x1b[31m`expect_json` value for test case {config['suite']}.{case_name} was invalid JSON: {error}\x1b[0m",
                file=sys.stderr,
            )
            sys.exit(1)

        f.write(
            f"\n"
            f"  std::stringstream expected_json_ss;\n"
            f'  expected_json_ss << R"--({case[key]})--";\n'
            f"  auto result_json_ss = std::stringstream(stdout_);\n"
            f"  bpt::ptree expected_json, actual_json;\n"
            f"  bpt::read_json(expected_json_ss, expected_json);\n"
            f"  bpt::read_json(result_json_ss, actual_json);\n"
            f"  compare_json(expected_json, actual_json);\n"
        )

    f.write(f"}}\n")


def generate_skip(case, specific):
    possibly_skip = ""
    skip_reason = case.get("skip", False)
    specific_skip_reason = case.get(f"{specific}_skip", False)
    if specific_skip_reason or skip_reason:
        possibly_skip += "  if (!run_skipped_tests) {\n"
        possibly_skip += "    GTEST_SKIP()"
        if type(specific_skip_reason) == str:
            possibly_skip += f' << "{specific_skip_reason}"'
        elif type(skip_reason) == str:
            possibly_skip += f' << "{skip_reason}"'
        possibly_skip += ";\n"
        possibly_skip += "  }\n"

    return possibly_skip


def gen_runner(output_runner_name, test_configs):
    with open(output_runner_name, "w") as f:
        f.write(
            "#include <boost/property_tree/json_parser.hpp>\n"
            "#include <boost/property_tree/ptree.hpp>\n"
            "#include <filesystem>\n"
            "#include <fstream>\n"
            "#include <gmock/gmock.h>\n"
            "#include <gtest/gtest.h>\n"
            "#include <sstream>\n"
            "#include <string>\n"
            "#include <vector>\n"
            '#include "runner_common.h"\n'
            "\n"
            "namespace ba = boost::asio;\n"
            "namespace bpt = boost::property_tree;\n"
            "\n"
            "using ::testing::MatchesRegex;\n"
            "\n"
            "extern bool run_skipped_tests;\n"
        )
        for config in test_configs:
            add_tests(f, config)


def gen_thrift(test_configs):
    for config in test_configs:
        if not is_thrift_test(config):
            continue
        output_thrift_name = f"{config['suite']}.thrift"
        with open(output_thrift_name, "w") as f:
            f.write(config["thrift_definitions"])
        print(f"Thrift out: {output_thrift_name}")


def main():
    if len(sys.argv) < 4:
        print("Usage: gen_tests.py OUTPUT_TARGET OUTPUT_RUNNER INPUT1 [INPUT2 ...]")
        exit(1)

    output_target = sys.argv[1]
    output_runner = sys.argv[2]
    inputs = sys.argv[3:]

    print(f"Output target: {output_target}")
    print(f"Output runner: {output_runner}")
    print(f"Input files: {inputs}")

    test_configs = []
    test_suites = set()
    while len(inputs) > 0:
        test_path = inputs.pop()
        if test_path.endswith(".toml"):
            test_suite = pathlib.Path(test_path).stem
            if test_suite in test_suites:
                raise Exception(f"Test suite {test_suite} is defined multiple times")
            test_suites.add(test_suite)
            config = toml.load(test_path)
            config["suite"] = test_suite
            test_configs += [config]
        elif os.path.isdir(test_path):
            for root, dirs, files in os.walk(test_path):
                for name in files:
                    if name.endswith(".toml"):
                        path = os.path.join(root, name)
                        print("Found definition file at {path}")
                        inputs.append(path)
        else:
            raise Exception(
                "Test definition inputs must have the '.toml' extension or be a directory"
            )

    gen_target(output_target, test_configs)
    gen_runner(output_runner, test_configs)
    gen_thrift(test_configs)


if __name__ == "__main__":
    main()
