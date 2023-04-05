import glob
import json
import os
import os.path
import shutil
import subprocess
import tempfile
import time
import unittest
from contextlib import contextmanager
from enum import Enum

ExitStatus = Enum(
    "ExitStatus",
    (
        "SUCCESS USAGE_ERROR FILE_NOT_FOUND_ERROR "
        "CONFIG_GENERATION_ERROR SCRIPT_PARSING_ERROR "
        "STOP_TARGET_ERROR SEGMENT_REMOVAL_ERROR "
        "SEGMENT_INIT_ERROR COMPILATION_ERROR PATCHING_ERROR "
        "PROCESSING_TARGET_DATA_ERROR OID_OBJECT_ERROR"
    ),
    start=0,
)

OUTPUT_PATH = "oid_out.json"


def copy_file(from_path, to_path):
    """
    Copies a file from `from_path` to `to_path`, preserving its permissions.
    """
    shutil.copy2(from_path, to_path)
    shutil.copymode(from_path, to_path)


class OIDebuggerTestCase(unittest.TestCase):
    def setUp(self):
        # Store PWD before moving out of it
        self.original_working_directory = os.getcwd()
        # Store OI's source directory before moving out of it
        self.oid_source_dir = os.path.dirname(
            os.path.dirname(os.path.abspath(__file__))
        )
        # Assume we started in the CMake build directory
        self.build_dir = self.original_working_directory

        # This needs to be a class variable, otherwise it won't be referenced
        # by any object alive by the end of this class method's execution and
        # and the directory will be automatically removed before executing the
        # tests themselves.
        self.temp = tempfile.TemporaryDirectory(dir=self.build_dir)
        os.chdir(self.temp.name)

        self.oid = os.getenv("OID")
        self.oid_conf = os.path.join(self.build_dir, "..", "testing.oid.toml")

        self.binary_path = os.path.join(self.build_dir, "integration_mttest")
        self.sleepy_binary_path = os.path.join(self.build_dir, "integration_sleepy")

        self.custom_generated_code_file = os.path.join(
            self.temp.name, "custom_oid_output.cpp"
        )

        self.custom_generated_code_file = f"{self.temp.name}/custom_oid_output.cpp"
        self.default_script = "integration_entry_doStuff_arg0.oid"

    def tearDown(self):
        os.chdir(self.original_working_directory)
        self.temp.cleanup()

    @contextmanager
    def spawn_oid(self, script_path, test_cmd=None, oid_opt=""):
        """
        Spawns a test binary and oid with the specified oid binary path, script
        and test command.

        Will take care of cleaning up the process properly.
        """

        if test_cmd is None:
            test_cmd = self.binary_path + " 100"

        debuggee_proc = subprocess.Popen(
            test_cmd,
            shell=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        debuggee_pid = debuggee_proc.pid

        cmd = f"OID_METRICS_TRACE=time {self.oid} --dump-json --config-file {self.oid_conf} --script {script_path} -t60 --pid {debuggee_pid} {oid_opt}"
        proc = subprocess.run(
            cmd,
            shell=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        try:
            yield proc
        finally:
            debuggee_proc.terminate()
            outs, _errs = debuggee_proc.communicate()

    def script(self, script_name=None):
        return os.path.join(
            self.oid_source_dir, "test", script_name or self.default_script
        )

    def expectReturncode(self, proc, returncode):
        if proc.returncode != returncode.value:
            print()
            print(proc.stdout.decode("utf-8"))
            print(proc.stderr.decode("utf-8"))

        self.assertEqual(proc.returncode, returncode.value)

    def test_help_works(self):
        proc = subprocess.run(f"{self.oid} --help", shell=True, stdout=subprocess.PIPE)
        self.expectReturncode(proc, ExitStatus.SUCCESS)
        self.assertIn(b"usage: ", proc.stdout)

    def test_attach_more_than_once_works(self):
        with subprocess.Popen(
            f"{self.binary_path} 100",
            shell=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        ) as debuggee_proc:
            debuggee_pid = debuggee_proc.pid

            for i in range(2):
                proc = subprocess.run(
                    f"{self.oid} --dump-json --config-file {self.oid_conf} --script {self.script()} -t60 --pid {debuggee_pid}",
                    shell=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                )
                self.expectReturncode(proc, ExitStatus.SUCCESS)

            debuggee_proc.terminate()

        with open(OUTPUT_PATH, "r") as f:
            output = json.loads(f.read())
            self.assertEqual(output[0]["typeName"], "Foo")
            self.assertEqual(output[0]["staticSize"], 2176)
            self.assertEqual(output[0]["dynamicSize"], 76)
            self.assertEqual(len(output[0]["members"]), 25)

    @unittest.skip(
        "https://github.com/facebookexperimental/object-introspection/issues/53"
    )
    def test_data_segment_size_change(self):
        with subprocess.Popen(
            f"{self.binary_path} 1000",
            shell=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        ) as debuggee_proc:
            debuggee_pid = debuggee_proc.pid

            for data_segment_size in ("1M", "2M", "1M"):
                proc = subprocess.run(
                    f"{self.oid} --dump-json --config-file {self.oid_conf} --script {self.script()} -t60 --pid {debuggee_pid} -x {data_segment_size}",
                    shell=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                )
                self.expectReturncode(proc, ExitStatus.SUCCESS)

            debuggee_proc.terminate()

        with open(OUTPUT_PATH, "r") as f:
            output = json.loads(f.read())
            self.assertEqual(output[0]["typeName"], "Foo")
            self.assertEqual(output[0]["staticSize"], 2176)
            self.assertEqual(output[0]["dynamicSize"], 76)
            self.assertEqual(len(output[0]["members"]), 25)

    def test_custom_generated_file(self):
        with subprocess.Popen(
            f"{self.binary_path} 100",
            shell=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        ) as debuggee_proc:
            debuggee_pid = debuggee_proc.pid

            proc = subprocess.run(
                f"{self.oid} --script {self.script()} --config-file {self.oid_conf} -t60 --pid {debuggee_pid}",
                shell=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            self.expectReturncode(proc, ExitStatus.SUCCESS)

            copy_file(
                "/tmp/tmp_oid_output_2.cpp",
                self.custom_generated_code_file,
            )
            proc = subprocess.run(
                f"{self.oid} --script {self.script()} --config-file {self.oid_conf} --pid {debuggee_pid} -t60 --generated-code-file {self.custom_generated_code_file}",
                shell=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            self.expectReturncode(proc, ExitStatus.SUCCESS)

            debuggee_proc.terminate()

        # with open("oid_out.json", "r") as f:
        #     output = json.loads(f.read())
        #     self.assertEqual(output["typeName"], "Foo")
        #     self.assertEqual(output["staticSize"] + output["dynamicSize"], 2062)
        #     self.assertEqual(len(output["members"]), 9)

        # debuggee_proc.terminate()
        # outs, _errs = debuggee_proc.communicate()

    def test_symbol_not_found_in_binary_fails(self):
        with self.spawn_oid(self.script(), test_cmd="/bin/sleep 100") as proc:
            self.expectReturncode(proc, ExitStatus.COMPILATION_ERROR)
            self.assertIn(
                b"Failed to create FuncDesc for",
                proc.stderr,
            )

    def test_non_existant_script_fails(self):
        with self.spawn_oid("not_there.oid") as proc:
            self.expectReturncode(proc, ExitStatus.FILE_NOT_FOUND_ERROR)

    def test_no_args_shows_help(self):
        proc = subprocess.run(
            self.oid,
            shell=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self.expectReturncode(proc, ExitStatus.USAGE_ERROR)
        self.assertIn(b"usage: ", proc.stdout)

    def test_remove_mappings(self):
        with subprocess.Popen(
            f"{self.binary_path} 100",
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            shell=True,
        ) as target_proc:
            pid = target_proc.pid

            # The sleep is unfortunate but it allows the segments to get
            # established in the target process.
            time.sleep(2)

            numsegs = subprocess.run(
                f"cat /proc/{pid}/maps | wc -l",
                shell=True,
                capture_output=True,
                check=True,
            )
            beforeSegs = int(numsegs.stdout.decode("ascii"))

            proc = subprocess.run(
                f"{self.oid} --config-file {self.oid_conf} --script {self.script()} -t 1 --pid {pid} -d 3",
                shell=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            self.expectReturncode(proc, ExitStatus.SUCCESS)

            numsegs = subprocess.run(
                f"cat /proc/{pid}/maps | wc -l",
                shell=True,
                capture_output=True,
                check=True,
            )
            afterSegs = int(numsegs.stdout.decode("ascii"))
            self.assertEqual(beforeSegs, afterSegs - 2)

            # remove both the text and data segments
            proc = subprocess.run(
                f"{self.oid} --config-file {self.oid_conf} -r --pid {pid}",
                shell=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            self.expectReturncode(proc, ExitStatus.SUCCESS)

            numsegs = subprocess.run(
                f"cat /proc/{pid}/maps | wc -l",
                shell=True,
                capture_output=True,
                check=True,
            )
            afterRemSegs = int(numsegs.stdout.decode("ascii"))
            self.assertEqual(beforeSegs, afterRemSegs)

    def test_metrics_data_is_generated(self):
        with self.spawn_oid(self.script()) as proc:
            self.expectReturncode(proc, ExitStatus.SUCCESS)

        # Just checking that the file exists and it's valid JSON
        with open("oid_metrics.json", "r") as f:
            json.loads(f.read())

    # Ensure removeTrap properly repatch small functions
    def test_probe_return_arg0_small_fun(self):
        # Check function incN is smaller than the POKETEXT window (8 bytes)
        readIncNSize = subprocess.run(
            f"readelf -s {self.binary_path} | grep _ZN3Foo4incNEi | awk '{{print $3}}'",
            capture_output=True,
            shell=True,
            check=True,
        )
        incNSize = int(readIncNSize.stdout.decode("ascii"))
        self.assertLessEqual(incNSize, 8)

        with self.spawn_oid(self.script("integration_return_incN_arg0.oid")) as proc:
            self.expectReturncode(proc, ExitStatus.SUCCESS)

        with open(OUTPUT_PATH, "r") as f:
            output = json.loads(f.read())
            self.assertEqual(output[0]["typeName"], "int")
            self.assertEqual(output[0]["staticSize"] + output[0]["dynamicSize"], 4)
            self.assertNotIn("members", output[0])

    def test_error_function_no_this(self):
        with self.spawn_oid(self.script("integration_entry_doStuff_this.oid")) as proc:
            self.expectReturncode(proc, ExitStatus.COMPILATION_ERROR)
            self.assertIn(
                b"has no 'this' parameter",
                proc.stderr,
            )

    def test_error_method_no_arg0(self):
        with self.spawn_oid(self.script("integration_entry_inc_arg0.oid")) as proc:
            self.expectReturncode(proc, ExitStatus.COMPILATION_ERROR)
            self.assertIn(
                b"Argument index 0 too large. Args count: 0",
                proc.stderr,
            )

    def test_error_timeout(self):
        with self.spawn_oid(
            self.script(),
            test_cmd=f"{self.sleepy_binary_path} 100",
            oid_opt="-d2 --timeout=1",
        ) as proc:
            self.expectReturncode(proc, ExitStatus.SUCCESS)
            self.assertIn(b"Received SIGNAL 14", proc.stderr)
            self.assertIn(b"processTrap: Error in waitpid", proc.stderr)
            self.assertIn(b"Interrupted system call", proc.stderr)


if __name__ == "__main__":
    print("[debug] Running OI's integration tests")
    unittest.main(verbosity=2)
