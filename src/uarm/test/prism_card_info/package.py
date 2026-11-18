#!/usr/bin/env python3
"""Build the E2 diagnostic library and an unmodified stock rollback library."""

import argparse
from pathlib import Path
import struct
import sys
import zlib

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "wpa2_stock"))
from patch_driver import (
    COMMAND_ENTRY, DRIVER_SHA256, RESOURCE_LIMIT, SHIM_START,
    e2_package, replace_code, resources, unpack_database,
)


def build_libraries(installer, shim):
    driver = bytearray(unpack_database(e2_package(installer), b"i027", DRIVER_SHA256))
    # The installer stores the driver under a temporary name and type.
    driver[:32] = b"SP3WLAN".ljust(32, b"\0")
    driver[60:64] = b"libr"
    table = resources(driver)
    start, end = next((start, end) for kind, ident, start, end in table
                      if kind == b"amdc" and ident == 0)
    code = driver[start:end]
    if len(code) != SHIM_START or code[COMMAND_ENTRY:COMMAND_ENTRY + 4] != bytes.fromhex("00c099e5"):
        raise ValueError("unexpected SP3WLAN command dispatch")
    if not shim or len(shim) % 4 or len(code) + len(shim) > RESOURCE_LIMIT:
        raise ValueError("invalid diagnostic code resource size")
    displacement = (SHIM_START - COMMAND_ENTRY - 8) // 4
    struct.pack_into("<I", code, COMMAND_ENTRY, 0xEA000000 | displacement)
    return driver, replace_code(driver, table, start, end, code + shim)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("installer", type=Path)
    parser.add_argument("shim", type=Path)
    parser.add_argument("output_dir", type=Path)
    args = parser.parse_args()
    stock_path = args.output_dir / "SP3WLAN-Stock.prc"
    diagnostic_path = args.output_dir / "SP3WLAN-Diagnostic.prc"
    if {stock_path.resolve(), diagnostic_path.resolve()} & {args.installer.resolve(), args.shim.resolve()}:
        parser.error("choose separate inputs and outputs")
    try:
        stock, diagnostic = build_libraries(args.installer.read_bytes(), args.shim.read_bytes())
        stock_path.write_bytes(stock)
        diagnostic_path.write_bytes(diagnostic)
    except (ValueError, OSError, StopIteration, zlib.error, struct.error) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
