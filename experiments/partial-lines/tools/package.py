"""Package this experiment into its own dist; never write Voice artifacts."""
import hashlib
from pathlib import Path
import shutil
import subprocess

Import("env")


def package(source, target, env):
    output = Path(env.subst("$PROJECT_DIR")) / "dist"
    output.mkdir(exist_ok=True)
    app = Path(env.subst("$BUILD_DIR/${PROGNAME}.bin"))
    segments = [(int(str(offset), 0), Path(env.subst(str(path))))
                for offset, path in env["FLASH_EXTRA_IMAGES"]]
    segments.append((int(str(env.get("ESP32_APP_OFFSET", "0x10000")), 0), app))
    segments.sort()
    if [offset for offset, _ in segments] != [0, 0x8000, 0xE000, 0x10000]:
        raise RuntimeError("Unexpected partition layout")
    esptool = Path(env.PioPlatform().get_package_dir("tool-esptoolpy")) / "esptool.py"
    merged = output / "note4c-partial-lines-merged.bin"
    command = [env.subst("$PYTHONEXE"), str(esptool), "--chip", "esp32s3",
               "merge_bin", "--output", str(merged), "--flash_mode", "dio",
               "--flash_freq", "80m", "--flash_size", "16MB"]
    for offset, path in segments:
        command.extend([hex(offset), str(path)])
    subprocess.run(command, check=True)
    artifacts = [merged]
    for _, path in segments:
        destination = output / path.name
        shutil.copyfile(path, destination)
        artifacts.append(destination)
    (output / "SHA256SUMS").write_text("".join(
        hashlib.sha256(path.read_bytes()).hexdigest() + "  " + path.name + "\n"
        for path in sorted(artifacts)), encoding="utf-8")
    print("Partial-lines: dist/firmware.bin at 0x10000 preserves existing NVS")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", package)
