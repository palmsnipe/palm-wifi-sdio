#!/usr/bin/env python3
"""Assemble the public preview tools without palmOne firmware or database resources."""

import argparse
from pathlib import Path
import zipfile

SOURCES = {
    "test/wpa2_hardware": ("Makefile", "apply.py", "release_bundle.py", "hardware.c", "hardware.h",
                           "profile.h", "link_entry.S", "main.c", "app.rcp", "package.py"),
    "test/wpa2_stock": ("stock.ld", "setup.c", "credential.h", "setup_entry.S", "setup.ld", "patch_driver.py"),
    "test/wpa2_probe": ("rsn_supplicant.c", "rsn_supplicant.h", "ccmp_station.c", "ccmp_station.h",
                         "arm_memory.c", "startup.c", "linker.lkr", "probe_result.h", "string.h",
                         "STARTER-LICENSE"),
    "test/prism_card_info": ("package.py",),
    "uarm": ("palmone_rsn_wire.h", "palmone_ccmp_wire.h"),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("bearssl_dir", type=Path)
    args = parser.parse_args()
    hardware = Path(__file__).resolve().parent
    repo = hardware.parents[3]
    guide = repo / "doc/palmone-e2-wpa2.md"
    build = hardware / "build"
    files = {
        "README.md": guide,
        "LICENSE": repo / "LICENSE",
        "apply.py": hardware / "apply.py",
        "doc/palmone-e2-wpa2.md": guide,
        "src/uarm/test/wpa2_hardware/build/nonce_seed.h": build / "nonce_seed.h",
    }
    for folder, names in SOURCES.items():
        for name in names:
            relative = f"src/uarm/{folder}/{name}"
            files[relative] = repo / relative
    for name in ("E2WPA2Test.prc", "hardware.bin", "setup.bin"):
        files[f"payload/{name}"] = build / name

    # Include the upstream source and license for the BearSSL code in the ARM payload.
    bearssl = args.bearssl_dir.resolve()
    for root in ("inc", "src"):
        for path in (bearssl / root).rglob("*"):
            if path.is_file() and path.suffix in (".c", ".h"):
                files[f"bearssl-0.6/{path.relative_to(bearssl)}"] = path
    files["bearssl-0.6/LICENSE.txt"] = bearssl / "LICENSE.txt"
    missing = [str(path) for path in files.values() if not path.is_file()]
    if missing:
        parser.error(f"missing release inputs: {', '.join(missing)}")
    with zipfile.ZipFile(args.output, "w", compression=zipfile.ZIP_DEFLATED) as output:
        for name, path in sorted(files.items()):
            output.write(path, name)


if __name__ == "__main__":
    main()
