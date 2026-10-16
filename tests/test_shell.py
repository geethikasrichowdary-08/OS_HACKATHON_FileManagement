import pathlib
import resource
import subprocess
import tempfile
import time
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SHELL = ROOT / "build" / "minish"


class MiniShellTests(unittest.TestCase):
    def run_shell(self, commands, cwd=None, timeout=6):
        return subprocess.run(
            [str(SHELL)], input=commands, text=True, capture_output=True,
            cwd=cwd, timeout=timeout, check=False
        )

    def test_builtins_and_parent_directory_change(self):
        with tempfile.TemporaryDirectory() as directory:
            child = pathlib.Path(directory, "child")
            child.mkdir()
            result = self.run_shell(f"cd {child}\npwd\nhelp\nexit 0\n", cwd=directory)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(str(child), result.stdout)
            self.assertIn("Built-ins:", result.stdout)

    def test_quotes_and_pipeline(self):
        result = self.run_shell("printf '%s\\n' 'hello world' | tr a-z A-Z\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), "HELLO WORLD")

    def test_input_output_and_append_redirection(self):
        with tempfile.TemporaryDirectory() as directory:
            result = self.run_shell(
                "printf 'first\\n' > output.txt\n"
                "printf 'second\\n' >> output.txt\n"
                "cat < output.txt\n", cwd=directory
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, "first\nsecond\n")
            self.assertEqual(pathlib.Path(directory, "output.txt").read_text(),
                             "first\nsecond\n")

    def test_background_jobs_are_reaped_and_reported(self):
        result = self.run_shell(
            "/usr/bin/sleep 0.05 &\n/usr/bin/sleep 0.15\njobs\nexit\n"
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("[1]", result.stdout)
        self.assertIn("Done", result.stdout)
        self.assertIn("/usr/bin/sleep 0.05 &", result.stdout)

    def test_kill_only_addresses_a_shell_job(self):
        result = self.run_shell("/usr/bin/sleep 30 &\nkill %1\njobs\nexit\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Terminated", result.stdout)
        self.assertIn("Done", result.stdout)

    def test_kill_escalates_for_job_ignoring_sigterm(self):
        started = time.monotonic()
        result = self.run_shell(
            "/bin/sh -c 'trap \"\" TERM; exec /usr/bin/sleep 30' &\n"
            "/usr/bin/sleep 0.1\nkill %1\njobs\n",
            timeout=4,
        )
        elapsed = time.monotonic() - started
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertGreaterEqual(elapsed, 0.9)
        self.assertLess(elapsed, 2.5)
        self.assertIn("Terminated", result.stdout)
        self.assertIn("Done", result.stdout)

    def test_pipe_creation_failure_preserves_shell_input(self):
        def limit_descriptors():
            resource.setrlimit(resource.RLIMIT_NOFILE, (4, 4))

        result = subprocess.run(
            [str(SHELL)], input="printf x | cat\n/bin/echo recovered\n",
            text=True, capture_output=True, cwd=ROOT, timeout=4,
            preexec_fn=limit_descriptors,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "recovered\n")
        self.assertIn("minish: pipe:", result.stderr)

    def test_exit_cleans_up_background_children(self):
        started = time.monotonic()
        result = self.run_shell("/usr/bin/sleep 30 &\nexit\n", timeout=4)
        elapsed = time.monotonic() - started
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertLess(elapsed, 2.5)

    def test_exit_escalates_for_job_ignoring_sigterm(self):
        started = time.monotonic()
        result = self.run_shell(
            "/bin/sh -c 'trap \"\" TERM; exec /usr/bin/sleep 30' &\n"
            "/usr/bin/sleep 0.1\nexit\n",
            timeout=4,
        )
        elapsed = time.monotonic() - started
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertGreaterEqual(elapsed, 0.9)
        self.assertLess(elapsed, 2.5)

    def test_invalid_syntax_and_missing_program_are_reported(self):
        result = self.run_shell(
            "echo hi |\nminish-command-that-does-not-exist\nexit 0\n"
        )
        self.assertEqual(result.returncode, 0)
        self.assertIn("syntax:", result.stderr)
        self.assertIn("minish-command-that-does-not-exist", result.stderr)


if __name__ == "__main__":
    unittest.main()