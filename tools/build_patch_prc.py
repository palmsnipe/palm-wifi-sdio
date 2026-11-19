#!/usr/bin/env python3
"""Package reversible patches and checksums for software already installed on an E2."""

import argparse
import hashlib
from pathlib import Path
import struct
import zlib

from build_single_prc import append_resources, encode_payload
from patch_installer import make_images, resources


def resource_map(image):
    return {(kind, ident): image[start:end] for kind, ident, start, end in resources(image)}


def recipe(kind, ident, stock, patched, suffix):
    if len(stock) > len(patched) or len(patched) > 65530 or patched[len(stock):] != suffix:
        raise ValueError("the only allowed resource growth is the compiled shim")
    edits = [(i, old ^ new) for i, (old, new) in enumerate(zip(stock, patched)) if old != new]
    return (struct.pack(">4sHHHH16s16s", kind, ident, len(stock), len(patched), len(edits),
                        hashlib.md5(stock).digest(), hashlib.md5(patched).digest()) +
            b"".join(struct.pack(">HB", offset, mask) for offset, mask in edits) + suffix)


def patch_resources(stock, patched, ids):
    original = resource_map(stock)
    updated = resource_map(patched)
    if original.keys() != updated.keys():
        raise ValueError("patch changed the resource directory")
    changed = {key for key in original if original[key] != updated[key]}
    if changed != ids.keys():
        raise ValueError(f"unexpected changed resources: {changed}")
    return [(b"wPch", resource_id, recipe(*key, original[key], updated[key], suffix))
            for key, (resource_id, suffix) in ids.items()]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("base", type=Path)
    parser.add_argument("payload", type=Path)
    parser.add_argument("installer", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        hardware = (args.payload / "hardware.bin").read_bytes()
        setup = (args.payload / "setup.bin").read_bytes()
        stock, driver, stock_panel, panel = make_images(args.installer.read_bytes(), hardware, setup)
        extras = patch_resources(stock, driver, {(b"amdc", 0): (1000, hardware), (b"amdi", 0): (1001, b"")})
        extras += patch_resources(stock_panel, panel, {(b"code", 2): (1010, setup)})
        status = encode_payload((args.payload / "E2WPA2Test.prc").read_bytes(), b"E2 WPA2 Test", b"applE2WT")
        if len(status) > 30000:
            raise ValueError("the status app exceeds one installer resource")
        extras.append((b"wDat", 1020, status))
        args.output.write_bytes(append_resources(args.base.read_bytes(), extras))
    except (OSError, ValueError, StopIteration, struct.error, zlib.error) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
