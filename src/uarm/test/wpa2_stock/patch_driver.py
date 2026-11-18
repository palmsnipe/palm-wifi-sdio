#!/usr/bin/env python3
"""Patch local E2 v1.2 Wi-Fi driver and Setup databases for the virtual WPA2 AP."""

import argparse
import hashlib
from pathlib import Path
import struct
import zlib

INSTALLER_SHA256 = "adb714f4e8a9f979340991cbd9dc71d03dcfa1e30b4d96236cb938c4bedde43c"
DRIVER_SHA256 = "3d00cf82d89ba7647d903f85c76e7878e127f9132565414b36dfa53e20763340"
PANEL_SHA256 = "ed4d62e3e6eb6877f93a0edba91874f7918fd95d6076eff6bad737521483ed4c"
COMMAND_ENTRY = 0xB540
RX_PAYLOAD_CALL = 0x96F0
SHIM_START = 0xC22C
RX_SHIM = SHIM_START + 4
ORIGINAL_GLOBALS_ABOVE_R9 = 0x28
REKEY_STATE_BYTES = 0x40
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


def patch(driver, shim):
    table = resources(driver)
    amdc_start, amdc_end = next((start, end) for typ, ident, start, end in table
                                if typ == b"amdc" and ident == 0)
    amdi_start = next(start for typ, ident, start, _ in table if typ == b"amdi" and ident == 0)
    code = bytearray(driver[amdc_start:amdc_end])
    if len(code) != SHIM_START or code[COMMAND_ENTRY:COMMAND_ENTRY + 4] != bytes.fromhex("00c099e5"):
        raise ValueError("unexpected P3WLAN command dispatch")
    if not shim or len(code) + len(shim) > RESOURCE_LIMIT:
        raise ValueError("the shim does not fit in the amdc resource")
    if code[RX_PAYLOAD_CALL:RX_PAYLOAD_CALL + 4] != bytes.fromhex("01f0eeee") or \
            struct.unpack_from(">I", driver, amdi_start)[0] != ORIGINAL_GLOBALS_ABOVE_R9:
        raise ValueError("unexpected P3WLAN receive call or global data size")
    offset = (SHIM_START - COMMAND_ENTRY - 8) // 4
    struct.pack_into("<I", code, COMMAND_ENTRY, 0xEA000000 | offset)
    offset = RX_SHIM - ((RX_PAYLOAD_CALL + 4) & ~3)
    struct.pack_into("<HH", code, RX_PAYLOAD_CALL, 0xF000 | (offset >> 12), 0xE800 | ((offset >> 1) & 0x7FF))
    code += shim
    result = replace_code(driver, table, amdc_start, amdc_end, code)
    # The installer stores this library under a temporary name and type.
    result[:32] = b"SP3WLAN".ljust(32, b"\0")
    result[60:64] = b"libr"
    struct.pack_into(">I", result, amdi_start + len(shim), ORIGINAL_GLOBALS_ABOVE_R9 + REKEY_STATE_BYTES)
    return result


def patch_setup(panel, shim):
    table = resources(panel)
    start, end = next((start, end) for typ, ident, start, end in table
                      if typ == b"code" and ident == 2)
    code = bytearray(panel[start:end])
    if len(code) != SETUP_SHIM_START or code[SETUP_SELECTION:SETUP_SELECTION + 6] != bytes.fromhex("0c2a0003002c"):
        raise ValueError("unexpected Wi-Fi Setup network-selection handler")
    if not shim or len(code) + len(shim) > RESOURCE_LIMIT:
        raise ValueError("the Setup shim does not fit in the code resource")
    offset = SETUP_SHIM_START - (SETUP_SELECTION + 2)
    struct.pack_into(">HhH", code, SETUP_SELECTION, 0x6100, offset, 0x4E71)  # bsr.w and nop
    result = replace_code(panel, table, start, end, code + shim)
    result[0] = ord("W")
    result[60:64] = b"panl"
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("installer", type=Path)
    parser.add_argument("shim", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--setup-shim", type=Path)
    parser.add_argument("--setup-output", type=Path)
    args = parser.parse_args()
    if bool(args.setup_shim) != bool(args.setup_output):
        parser.error("--setup-shim and --setup-output must be used together")
    paths = [args.installer, args.shim, args.output]
    if args.setup_shim:
        paths += [args.setup_shim, args.setup_output]
    if len({path.resolve() for path in paths}) != len(paths):
        parser.error("choose separate inputs and outputs")
    try:
        package = e2_package(args.installer.read_bytes())
        driver = patch(unpack_database(package, b"i027", DRIVER_SHA256), args.shim.read_bytes())
        if args.setup_shim:
            panel = patch_setup(unpack_database(package, b"i058", PANEL_SHA256), args.setup_shim.read_bytes())
            args.setup_output.write_bytes(panel)
        args.output.write_bytes(driver)
    except (ValueError, OSError, StopIteration, zlib.error, struct.error) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
