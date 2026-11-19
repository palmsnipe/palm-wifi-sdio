#!/usr/bin/env python3
"""Generate a local installer PRC from the patched and stock databases."""

import argparse
from pathlib import Path
import struct

from patch_installer import resources

PAYLOADS = (
    (1000, "SP3WLAN-E2-WPA2-Preview.prc", b"SP3WLAN", b"librP3wl"),
    (1010, "WiFiPanel-E2-WPA2-Preview.prc", b"WiFiPanel", b"panlWpnl"),
    (1020, "E2WPA2Test.prc", b"E2 WPA2 Test", b"applE2WT"),
    (1030, "SP3WLAN-Stock.prc", b"SP3WLAN", b"librP3wl"),
    (1040, "WiFiPanel-Stock.prc", b"WiFiPanel", b"panlWpnl"),
)
CHUNK_SIZE = 30_000


def encode_payload(image, name, identity):
    if len(image) < 78 or image[:32].split(b"\0", 1)[0] != name or image[60:68] != identity:
        raise ValueError(f"unexpected database image for {name.decode()}")
    table = resources(image)
    if not table or len(image) < table[0][2] or any(start >= end for _, _, start, end in table):
        raise ValueError(f"invalid resource table for {name.decode()}")
    if any(left[3] != right[2] for left, right in zip(table, table[1:])):
        raise ValueError(f"resource offsets are out of order for {name.decode()}")
    return struct.pack(">I", len(image)) + image


def installer_prc(base, images):
    entries = []
    for first, filename, name, identity in PAYLOADS:
        payload = encode_payload((images / filename).read_bytes(), name, identity)
        if len(payload) > 10 * CHUNK_SIZE:
            raise ValueError(f"{filename} exceeds the installer resource range")
        for index, offset in enumerate(range(0, len(payload), CHUNK_SIZE)):
            entries.append((b"wDat", first + index, payload[offset:offset + CHUNK_SIZE]))
    return append_resources(base, entries)


def append_resources(base, extra):
    entries = [(kind, ident, base[start:end]) for kind, ident, start, end in resources(base)] + extra
    header = bytearray(base[:76] + struct.pack(">H", len(entries)))
    offset = 78 + 10 * len(entries) + 2
    for kind, ident, data in entries:
        header.extend(struct.pack(">4sHI", kind, ident, offset))
        offset += len(data)
    return bytes(header) + b"\0\0" + b"".join(data for _, _, data in entries)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("base", type=Path)
    parser.add_argument("images", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        result = installer_prc(args.base.read_bytes(), args.images)
        args.output.write_bytes(result)
    except (OSError, ValueError, struct.error) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
