"""Check runner reports the build status, even after a bootloader size line."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class MtkCheckTest(unittest.TestCase):
    def test_build_failure_is_not_hidden_by_summary_filter(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "export.sh").write_text(":\n")
            programs = {
                "git": "case \"$*\" in *rev-parse*) echo e92874d50d3a2968566f9628018f4c096c01f3d9;; *) echo v6.1-dev-2300-g17a74c925c;; esac\n",
                "xtensa-esp-elf-gcc": "echo 'gcc (crosstool-NG esp-15.2.0_20251204)'\n",
                "idf.py": "echo 'Bootloader binary size 0x5220 bytes.'; echo 'simulated build failure' >&2; exit 7\n",
            }
            for name, body in programs.items():
                path = root / name
                path.write_text("#!/bin/sh\n" + body)
                path.chmod(0o755)
            env = dict(os.environ, IDF_DIR=tmp, PATH=tmp + os.pathsep + os.environ["PATH"])
            result = subprocess.run(["sh", "tools/mtk.sh", "check"], env=env,
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("simulated build failure", result.stdout + result.stderr)
            (root / "idf.py").write_text("#!/bin/sh\necho 'Project build complete'\n")
            result = subprocess.run(["sh", "tools/mtk.sh", "check"], env=env,
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Project build complete", result.stdout)


if __name__ == "__main__":
    unittest.main()
