#!/usr/bin/env python3
"""Build installation and rollback PRCs from a local palmOne Wi-Fi installer."""

import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--single", action="store_true", help="create one on-device installer PRC")
    parser.add_argument("installer", type=Path)
    parser.add_argument("output_dir", type=Path)
    args = parser.parse_args()
    base = Path(__file__).resolve().parent
    payload = base / "payload"
    if payload.is_dir():
        installer_base = payload / "E2WPA2InstallerBase.prc"
    else:
        payload = base / "build"
        installer_base = payload / "install/base.prc"
    packager = base / "tools/patch_installer.py"
    output = args.output_dir.resolve()
    if output.exists():
        parser.error("choose a new output directory")
    if not output.parent.is_dir():
        parser.error("the parent of the output directory must already exist")
    if any(not (payload / name).is_file() for name in ("hardware.bin", "setup.bin", "E2WPA2Test.prc")):
        parser.error("patch payload is missing. Extract the release ZIP or build the driver with "
                     "make from the repository root")
    if args.single and not installer_base.is_file():
        parser.error("single-PRC installer base is missing. Use a release kit with single-PRC support "
                     "or run make preview from the repository root")

    try:
        with tempfile.TemporaryDirectory(dir=output.parent) as temporary:
            work = Path(temporary)
            prepared = work / "prcs"
            prepared.mkdir()
            subprocess.run([sys.executable, packager, "package", args.installer.resolve(),
                            payload / "hardware.bin", payload / "setup.bin", prepared], check=True)
            shutil.copyfile(payload / "E2WPA2Test.prc", prepared / "E2WPA2Test.prc")
            if args.single:
                single = work / "single"
                single.mkdir()
                subprocess.run([sys.executable, base / "tools/build_single_prc.py", installer_base,
                                prepared, single / "E2WPA2Installer.prc"], check=True)
                prepared = single
            prepared.rename(output)
    except subprocess.CalledProcessError as error:
        return error.returncode
    except OSError as error:
        parser.error(str(error))
    if args.single:
        print(f"Created {output / 'E2WPA2Installer.prc'}")
    else:
        print(f"Created installation and rollback PRCs in {output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
