"""Run the actual probe driver against a recording transport with ASan/UBSan."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="note4c-partial-check-") as work:
    binary = str(Path(work) / "probe-test")
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(root / "include"), str(root / "src/probe.cpp"),
        str(root / "test/probe_test.cpp"), "-o", binary,
    ], check=True)
    subprocess.run([binary], check=True)
