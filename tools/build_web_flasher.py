#!/usr/bin/env python3
"""Assembles the web flasher site from a finished `pio run`.

    pio run && python tools/build_web_flasher.py [--out DIR]

Copies web/ plus the merged image (.pio/build/default/inkdeck-full.bin) into
DIR (default .pio/web-flasher) and writes manifest.json for ESP Web Tools,
versioned like the firmware so the flasher can tell an update from a reinstall.
Serve DIR over HTTPS (or http://localhost) in Chrome or Edge to flash.

Refuses to package a build with flash-config.txt settings baked in: anyone who
can open the page could download your token and Wi-Fi password.
"""

import argparse
import json
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / ".pio" / "build"


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--env", default="default", help="PlatformIO environment (default: default)")
    parser.add_argument("--out", default=str(ROOT / ".pio" / "web-flasher"), help="output directory")
    parser.add_argument("--allow-baked-config", action="store_true",
                        help="package even with flash-config.txt settings baked in (private use only)")
    args = parser.parse_args()

    image = BUILD / args.env / "inkdeck-full.bin"
    header = BUILD / "flash_config" / "FlashConfig.h"
    if not image.is_file() or not header.is_file():
        sys.exit(f"Missing {image.relative_to(ROOT)}: run `pio run -e {args.env}` first.")

    text = header.read_text(encoding="utf-8")
    baked = re.search(r'kFlashConfig\[\] = "(.*)";', text)
    if baked and baked.group(1) and not args.allow_baked_config:
        sys.exit("This build has flash-config.txt settings baked in (token, Wi-Fi password).\n"
                 "Move flash-config.txt aside and rebuild before publishing a web flasher,\n"
                 "or pass --allow-baked-config for a flasher only you can reach.")
    version = re.search(r'kInkDeckVersion\[\] = "(.*)";', text)
    version = version.group(1) if version else "dev"

    out = Path(args.out)
    if out.exists():
        shutil.rmtree(out)
    shutil.copytree(ROOT / "web", out)
    shutil.copy2(image, out / "inkdeck-full.bin")
    manifest = {
        "name": "InkDeck",  # must match the firmware's Improv device info
        "version": version,
        "new_install_prompt_erase": True,
        "new_install_improv_wait_time": 20,  # the X4 boots its display before answering
        "builds": [
            {"chipFamily": "ESP32-C3", "parts": [{"path": "inkdeck-full.bin", "offset": 0}]},
        ],
    }
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Web flasher for InkDeck {version} written to {out}")


if __name__ == "__main__":
    main()
