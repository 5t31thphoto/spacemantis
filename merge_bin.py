Import("env")
import os
from os.path import join, isfile

def after_build(source, target, env):
    try:
        build_dir = env.subst("$BUILD_DIR")
        project_dir = env.subst("$PROJECT_DIR")
        out_bin = join(project_dir, "firmware_merged.bin")
        bootloader = join(build_dir, "bootloader.bin")
        partitions = join(build_dir, "partitions.bin")
        firmware = join(build_dir, "firmware.bin")
        boot_app0 = join(env.subst("$PROJECT_PACKAGES_DIR"),
                         "framework-arduinoespressif32", "tools", "partitions", "boot_app0.bin")
        if not isfile(boot_app0):
            for c in [
                join(env.subst("$PROJECT_PACKAGES_DIR"), "framework-arduinoespressif32",
                     "tools", "partitions", "boot_app0.bin"),
                join(build_dir, "boot_app0.bin"),
            ]:
                if isfile(c):
                    boot_app0 = c
                    break
        if not (isfile(bootloader) and isfile(partitions) and isfile(firmware)):
            print("[merge] skip — bins missing")
            return
        parts = ["0x1000", bootloader, "0x8000", partitions]
        if isfile(boot_app0):
            parts += ["0xe000", boot_app0]
        parts += ["0x10000", firmware]
        cmd = [
            env.subst("$PYTHONEXE"), "-m", "esptool",
            "--chip", "esp32", "merge_bin",
            "-o", out_bin,
            "--flash_mode", "qio",
            "--flash_freq", "80m",
            "--flash_size", "16MB",
        ] + parts
        print("[merge]", " ".join(cmd))
        env.Execute(" ".join('"%s"' % c if " " in str(c) else str(c) for c in cmd))
        if isfile(out_bin):
            print("[merge] OK", out_bin, os.path.getsize(out_bin))
    except Exception as e:
        print("[merge] non-fatal:", e)

env.AddPostAction("$BUILD_DIR/firmware.bin", after_build)
