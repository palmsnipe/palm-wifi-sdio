#!/usr/bin/env python3
"""Build the E2 hardware test and its stock rollback driver."""

import argparse
from pathlib import Path
import re
import secrets
import struct
import sys
import zlib

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "prism_card_info"))
from package import build_libraries, resources, SHIM_START
from patch_driver import e2_package, PANEL_SHA256, patch_setup, unpack_database


def patch_data_path(driver):
    table = {(kind, ident): start for kind, ident, start, _ in resources(driver)}
    code = table[b"amdc", 0]
    info = table[b"amdi", 0]
    for call, original, entry in ((0x9680, "01f026ef", 4), (0x96f0, "01f0eeee", 8),
                                  (0xad88, "00f0cceb", 12), (0x99b6, "01f08ced", 16)):
        position = code + call
        if driver[position:position + 4] != bytes.fromhex(original):
            raise ValueError("unexpected stock data path call")
        offset = SHIM_START + entry - ((call + 4) & ~3)
        struct.pack_into("<HH", driver, position,
                         0xF000 | (offset >> 12), 0xE800 | ((offset >> 1) & 0x7FF))
    old_size, = struct.unpack_from(">I", driver, info)
    if old_size != 0x28:
        raise ValueError("unexpected stock global data size")
    header = Path(__file__).with_name("hardware.h").read_text()
    extra = int(re.search(r"#define HARDWARE_LINK_BYTES (\d+)", header)[1])
    struct.pack_into(">I", driver, info, old_size + extra)


def provision(output):
    seed = ", ".join(hex(byte) for byte in secrets.token_bytes(32))
    output.write_text(f"static const unsigned char nonceSeed[32] = {{{seed}}};\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    seed = commands.add_parser("seed")
    seed.add_argument("output", type=Path)
    package = commands.add_parser("package")
    package.add_argument("installer", type=Path)
    package.add_argument("shim", type=Path)
    package.add_argument("setup", type=Path)
    package.add_argument("output_dir", type=Path)
    args = parser.parse_args()
    try:
        if args.command == "seed":
            provision(args.output)
        else:
            names = ("SP3WLAN-Stock.prc", "SP3WLAN-E2-WPA2-Preview.prc",
                     "WiFiPanel-Stock.prc", "WiFiPanel-E2-WPA2-Preview.prc")
            paths = [args.output_dir / name for name in names]
            if {path.resolve() for path in paths} & {args.installer.resolve(), args.shim.resolve(), args.setup.resolve()}:
                parser.error("choose separate inputs and outputs")
            installer = args.installer.read_bytes()
            stock, driver = build_libraries(installer, args.shim.read_bytes())
            patch_data_path(driver)
            panel = unpack_database(e2_package(installer), b"i058", PANEL_SHA256)
            patched_panel = patch_setup(panel, args.setup.read_bytes())
            original_panel = bytearray(panel)
            original_panel[0] = ord("W")
            original_panel[60:64] = b"panl"
            for path, data in zip(paths, (stock, driver, original_panel, patched_panel)):
                path.write_bytes(data)
    except (ValueError, OSError, StopIteration, zlib.error) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
