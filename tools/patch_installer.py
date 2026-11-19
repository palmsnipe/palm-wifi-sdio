#!/usr/bin/env python3
"""Build the Tungsten E2 WPA2 driver, Wi-Fi panel and stock rollback PRCs."""

import argparse
import hashlib
from pathlib import Path
import re
import secrets
import struct
import zlib

INSTALLER_SHA256 = "adb714f4e8a9f979340991cbd9dc71d03dcfa1e30b4d96236cb938c4bedde43c"
DRIVER_SHA256 = "3d00cf82d89ba7647d903f85c76e7878e127f9132565414b36dfa53e20763340"
PANEL_SHA256 = "ed4d62e3e6eb6877f93a0edba91874f7918fd95d6076eff6bad737521483ed4c"
COMMAND_ENTRY = 0xB540
SHIM_START = 0xC22C
SETUP_SELECTION = 0x48B6
SETUP_SHIM_START = 0x57DE
RESOURCE_LIMIT = 65530


def resources(database):
    count, = struct.unpack_from(">H", database, 76)
    entries = [struct.unpack_from(">4sHI", database, 78 + 10 * i) for i in range(count)]
    end_by_start = dict(zip(sorted(offset for _, _, offset in entries),
                            sorted(offset for _, _, offset in entries)[1:] + [len(database)]))
    return [(kind, ident, offset, end_by_start[offset]) for kind, ident, offset in entries]


def chunks(database, kind):
    selected = [(ident, start, end) for typ, ident, start, end in resources(database) if typ == kind]
    return [database[start:end] for _, start, end in sorted(selected)]


def e2_package(installer):
    if hashlib.sha256(installer).hexdigest() != INSTALLER_SHA256:
        raise ValueError("expected the PalmDB WiFi_Installer1.2.prc for the Tungsten E2")
    return b"".join(chunks(installer, b"i011"))


def unpack_database(package, kind, digest):
    database = b"".join(zlib.decompress(part) for part in chunks(package, kind))
    if hashlib.sha256(database).hexdigest() != digest:
        raise ValueError(f"unexpected E2 database in {kind.decode()}")
    return database


def replace_code(database, table, start, end, code):
    result = bytearray(database[:start] + code + database[end:])
    growth = len(code) - (end - start)
    for i, (_, _, offset, _) in enumerate(table):
        if offset >= end:
            struct.pack_into(">I", result, 84 + i * 10, offset + growth)
    return result


def setup_edit():
    displacement = SETUP_SHIM_START - (SETUP_SELECTION + 2)
    return SETUP_SELECTION, bytes.fromhex("0c2a0003002c"), struct.pack(">HhH", 0x6100, displacement, 0x4E71)


def command_edit():
    displacement = (SHIM_START - COMMAND_ENTRY - 8) // 4
    return COMMAND_ENTRY, bytes.fromhex("00c099e5"), struct.pack("<I", 0xEA000000 | displacement)


def data_path_edits():
    for call, original, entry in ((0x9680, "01f026ef", 4), (0x96f0, "01f0eeee", 8),
                                  (0xad88, "00f0cceb", 12), (0x99b6, "01f08ced", 16)):
        offset = SHIM_START + entry - ((call + 4) & ~3)
        yield call, bytes.fromhex(original), struct.pack("<HH", 0xF000 | (offset >> 12),
                                                         0xE800 | ((offset >> 1) & 0x7FF))


def driver_info_edit():
    header = (Path(__file__).resolve().parents[1] / "driver/hardware.h").read_text()
    extra = int(re.search(r"#define HARDWARE_LINK_BYTES (\d+)", header)[1])
    return 0, struct.pack(">I", 0x28), struct.pack(">I", 0x28 + extra)


def apply_edit(data, start, edit, message):
    offset, original, replacement = edit
    start += offset
    end = start + len(original)
    if data[start:end] != original:
        raise ValueError(message)
    data[start:end] = replacement


def patch_setup(panel, shim):
    table = resources(panel)
    start, end = next((start, end) for typ, ident, start, end in table
                      if typ == b"code" and ident == 2)
    code = bytearray(panel[start:end])
    if len(code) != SETUP_SHIM_START:
        raise ValueError("unexpected Wi-Fi Setup network-selection handler")
    if not shim or len(code) + len(shim) > RESOURCE_LIMIT:
        raise ValueError("the Setup shim does not fit in the code resource")
    apply_edit(code, 0, setup_edit(), "unexpected Wi-Fi Setup network-selection handler")
    result = replace_code(panel, table, start, end, code + shim)
    result[0] = ord("W")
    result[60:64] = b"panl"
    return result


def build_libraries(installer, shim):
    driver = bytearray(unpack_database(e2_package(installer), b"i027", DRIVER_SHA256))
    # The installer stores the driver under a temporary name and type.
    driver[:32] = b"SP3WLAN".ljust(32, b"\0")
    driver[60:64] = b"libr"
    table = resources(driver)
    start, end = next((start, end) for kind, ident, start, end in table
                      if kind == b"amdc" and ident == 0)
    code = driver[start:end]
    if len(code) != SHIM_START:
        raise ValueError("unexpected SP3WLAN command dispatch")
    if not shim or len(shim) % 4 or len(code) + len(shim) > RESOURCE_LIMIT:
        raise ValueError("invalid diagnostic code resource size")
    apply_edit(code, 0, command_edit(), "unexpected SP3WLAN command dispatch")
    return driver, replace_code(driver, table, start, end, code + shim)


def patch_data_path(driver):
    table = {(kind, ident): start for kind, ident, start, _ in resources(driver)}
    code = table[b"amdc", 0]
    info = table[b"amdi", 0]
    for edit in data_path_edits():
        apply_edit(driver, code, edit, "unexpected stock data path call")
    apply_edit(driver, info, driver_info_edit(), "unexpected stock global data size")


def provision(output):
    seed = ", ".join(hex(byte) for byte in secrets.token_bytes(32))
    output.write_text(f"static const unsigned char nonceSeed[32] = {{{seed}}};\n")


def make_images(installer, hardware, setup):
    stock, driver = build_libraries(installer, hardware)
    patch_data_path(driver)
    panel = unpack_database(e2_package(installer), b"i058", PANEL_SHA256)
    patched_panel = patch_setup(panel, setup)
    original_panel = bytearray(panel)
    original_panel[0] = ord("W")
    original_panel[60:64] = b"panl"
    return stock, driver, original_panel, patched_panel


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
            images = make_images(args.installer.read_bytes(), args.shim.read_bytes(), args.setup.read_bytes())
            for path, data in zip(paths, images):
                path.write_bytes(data)
    except (ValueError, OSError, StopIteration, zlib.error, struct.error) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
