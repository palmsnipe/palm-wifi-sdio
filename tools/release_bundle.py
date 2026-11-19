#!/usr/bin/env python3
"""Assemble the public preview tools without palmOne firmware or database resources."""

import argparse
from pathlib import Path
import zipfile

SOURCES = {
    "driver": ("hardware.c", "hardware.h", "link_entry.S", "profile.h", "stock.ld"),
    "panel": ("credential.h", "setup.c", "setup.ld", "setup_entry.S"),
    "app": ("STARTER-LICENSE", "app.rcp", "linker.lkr", "main.c", "startup.c"),
    "wpa2": ("arm_memory.c", "ccmp_station.c", "ccmp_station.h", "palmone_ccmp_wire.h",
             "palmone_rsn_wire.h", "probe_result.h", "rsn_supplicant.c", "rsn_supplicant.h", "string.h"),
    "tools": ("patch_installer.py", "release_bundle.py"),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("bearssl_dir", type=Path)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    guide = repo / "doc/palmone-e2-wpa2.md"
    build = repo / "build"
    files = {
        "README.md": guide,
        "LICENSE": repo / "LICENSE",
        "Makefile": repo / "Makefile",
        "apply.py": repo / "apply.py",
        "doc/palmone-e2-wpa2.md": guide,
        "doc/maintenance.md": repo / "doc/maintenance.md",
        "tests/test_release.py": repo / "tests/test_release.py",
        "build/nonce_seed.h": build / "nonce_seed.h",
    }
    for folder, names in SOURCES.items():
        for name in names:
            relative = f"{folder}/{name}"
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
