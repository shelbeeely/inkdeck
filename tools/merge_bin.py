# PlatformIO post-build script: merges bootloader, partition table, boot_app0
# and the app into one image flashed at 0x0 (what the web flasher installs):
# .pio/build/<env>/inkdeck-full.bin

Import("env")  # noqa: F821  (provided by PlatformIO)


def merge_bin(source, target, env):
    board = env.BoardConfig()
    out = env.subst("$BUILD_DIR/inkdeck-full.bin")
    images = []
    for offset, path in env.get("FLASH_EXTRA_IMAGES", []):
        images += [offset, env.subst(path)]
    images += [board.get("upload.offset_address", "0x10000"), str(target[0])]  # the .bin
    cmd = [
        '"$OBJCOPY"',
        "--chip", board.get("build.mcu"),
        "merge-bin",
        "-o", f'"{out}"',
        "--flash-mode", board.get("build.flash_mode", "dio"),
        "--flash-size", board.get("upload.flash_size", "16MB"),
    ] + [f'"{i}"' for i in images]
    env.Execute(" ".join(cmd))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_bin)  # noqa: F821
