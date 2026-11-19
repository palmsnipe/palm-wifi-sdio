#!/usr/bin/env python3
"""Package reversible patches and checksums for software already installed on an E2."""

import argparse
from pathlib import Path
import struct

from build_single_prc import append_resources, encode_payload
from patch_installer import (
    RESOURCE_LIMIT, SETUP_SHIM_START, SHIM_START, command_edit, data_path_edits, driver_info_edit, setup_edit,
)


# Fingerprints of the three stock E2 v1.2 resources. The patcher checks these
# on the device. The release build needs only these digests and the byte edits.
STOCK_AMDC = bytes.fromhex("ec0e6569ca01a73a6ce0202e8aa08a2b")
STOCK_AMDI = bytes.fromhex("d26b0c166acb586cd548f310c2ded7f3")
STOCK_SETUP = bytes.fromhex("b95840ec083a592b1d4a9eb7d0109432")


def recipe(kind, ident, size, stock_digest, edits, suffix=b""):
    patched_size = size + len(suffix)
    if patched_size > RESOURCE_LIMIT:
        raise ValueError("the compiled shim exceeds the resource size limit")
    masks = {}
    for offset, before, after in edits:
        if len(before) != len(after) or not 0 <= offset <= size - len(before):
            raise ValueError("the patch edits bytes outside the original resource")
        for index, (old, new) in enumerate(zip(before, after), offset):
            if index in masks:
                raise ValueError("patch edits overlap")
            masks[index] = old ^ new
    changed = [(offset, mask) for offset, mask in sorted(masks.items()) if mask]
    return (struct.pack(">4sHHHH16s", kind, ident, size, patched_size, len(changed), stock_digest) +
            b"".join(struct.pack(">HB", offset, mask) for offset, mask in changed) + suffix)


def patcher_prc(base, payload):
    hardware = (payload / "hardware.bin").read_bytes()
    setup = (payload / "setup.bin").read_bytes()
    if not hardware or len(hardware) % 4 or not setup:
        raise ValueError("invalid compiled shim")
    status = encode_payload((payload / "E2WPA2Test.prc").read_bytes(), b"E2 WPA2 Test", b"applE2WT")
    if len(status) > 30000:
        raise ValueError("the status app exceeds one installer resource")
    return append_resources(base, [
        (b"wPch", 1000, recipe(b"amdc", 0, SHIM_START, STOCK_AMDC, (command_edit(), *data_path_edits()), hardware)),
        (b"wPch", 1001, recipe(b"amdi", 0, 24, STOCK_AMDI, (driver_info_edit(),))),
        (b"wPch", 1010, recipe(b"code", 2, SETUP_SHIM_START, STOCK_SETUP, (setup_edit(),), setup)),
        (b"wDat", 1020, status),
    ])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("base", type=Path)
    parser.add_argument("payload", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        args.output.write_bytes(patcher_prc(args.base.read_bytes(), args.payload))
    except (OSError, ValueError, struct.error) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
