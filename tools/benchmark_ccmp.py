#!/usr/bin/env python3
"""Verify the built ARM shim against AESCCM and count CCMP instructions."""

import argparse
import subprocess
from pathlib import Path

from cryptography.hazmat.primitives.ciphers.aead import AESCCM
from unicorn import Uc, UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_ARM
from unicorn import arm_const as regs


CODE, DATA, STACK, STOP = 0x100000, 0x200000, 0x300000, 0x400000
PAIRWISE, GROUP, MATERIAL, MAC, PAYLOAD = (DATA + offset for offset in
                                            (0x100, 0x400, 0x700, 0x800, 0x900))
TO_DS = bytes.fromhex("084100000239a1b2c311021122334455ffffffffffff1300")
FROM_DS = bytes.fromhex("08420000ffffffffffff0239a1b2c3110211223344551300")


class Shim:
    def __init__(self, elf, readelf):
        symbols = {}
        for line in subprocess.check_output([readelf, "-sW", elf], text=True).splitlines():
            fields = line.split()
            if len(fields) == 8 and fields[3] == "FUNC":
                symbols[fields[7]] = int(fields[1], 16)
        self.symbols = symbols
        self.origin = symbols["diagEntry"] & ~1
        self.machine = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        self.machine.ctl_set_cpu_model(regs.UC_CPU_ARM_PXA270)
        for address in (CODE, DATA, STACK, STOP):
            self.machine.mem_map(address, 0x10000)
        self.machine.mem_write(CODE, elf.with_suffix(".bin").read_bytes())
        self.machine.hook_add(UC_HOOK_CODE, self.count_instruction)

    def count_instruction(self, machine, address, size, data):
        self.instructions += 1

    def call(self, name, *args):
        machine = self.machine
        for register, value in zip((regs.UC_ARM_REG_R0, regs.UC_ARM_REG_R1,
                                    regs.UC_ARM_REG_R2, regs.UC_ARM_REG_R3), args):
            machine.reg_write(register, value)
        machine.reg_write(regs.UC_ARM_REG_SP, STACK + 0x10000)
        machine.reg_write(regs.UC_ARM_REG_LR, STOP | 1)
        self.instructions = 0
        machine.emu_start(CODE + self.symbols[name] - self.origin, STOP, timeout=5_000_000)
        if machine.reg_read(regs.UC_ARM_REG_PC) != STOP:
            raise RuntimeError(f"{name} did not return")
        return machine.reg_read(regs.UC_ARM_REG_R0), self.instructions

    def install(self, address, key, rx_pn=0):
        self.machine.mem_write(MATERIAL, key)
        return self.call("ccmpInstall", address, MATERIAL, rx_pn & 0xffffffff, rx_pn >> 32)[1]

    def open(self, address, frame, alignment):
        body = PAYLOAD + alignment
        self.machine.mem_write(body, frame)
        return self.call("ccmpOpen", address, MAC, body, len(frame))


def reference(key, mac, pn, data, key_id=0):
    packet_number = pn.to_bytes(6, "little")
    header = packet_number[:2] + bytes((0, 0x20 | key_id << 6)) + packet_number[2:]
    aad = bytes((mac[0] & 0x8f, mac[1] & 0xc7)) + mac[4:22] + bytes((mac[22] & 15, 0))
    nonce = b"\0" + mac[10:16] + pn.to_bytes(6, "big")
    return header + AESCCM(key, tag_length=8).encrypt(nonce, data, aad)


def exercise(shim):
    key = bytes(range(16))
    install = shim.install(PAIRWISE, key)
    rows = {}
    pn = 0
    for length in (1, 15, 16, 17, 32, 128, 512, 1508, 1510):
        # Vary both alignment and packet number under the same installed key.
        plaintext = bytes(index % 251 for index in range(length))
        for alignment in range(4):
            pn += 1
            body = PAYLOAD + alignment
            shim.machine.mem_write(MAC, TO_DS)
            shim.machine.mem_write(body, bytes(8) + plaintext + bytes(8))
            result, seal = shim.call("ccmpSeal", PAIRWISE, MAC, body, length)
            expected = reference(key, TO_DS, pn, plaintext)
            assert result == 1 and shim.machine.mem_read(body, len(expected)) == expected

            shim.machine.mem_write(MAC, FROM_DS)
            frame = reference(key, FROM_DS, pn, plaintext)
            result, opened = shim.open(PAIRWISE, frame, alignment)
            assert result == 1 and shim.machine.mem_read(body + 8, length) == plaintext
            assert shim.open(PAIRWISE, frame, alignment)[0] == 2
            rows[(length, alignment)] = (seal, opened)

    group_key = bytes(range(16, 32))
    next_key = bytes(range(32, 48))
    high_pn = 0x123456789abc
    shim.install(GROUP, group_key, high_pn)
    assert shim.open(GROUP, reference(group_key, FROM_DS, high_pn, b"group", 1), 0)[0] == 2
    assert shim.open(GROUP, reference(group_key, FROM_DS, high_pn + 1, b"group", 1), 0)[0] == 1
    shim.install(GROUP, next_key)
    old_frame = reference(group_key, FROM_DS, high_pn + 2, b"group", 1)
    assert shim.open(GROUP, old_frame, 0)[0] == 3
    assert shim.machine.mem_read(PAYLOAD + 8, 5) == bytes(5)
    new_frame = reference(next_key, FROM_DS, 1, b"group", 1)
    assert shim.open(GROUP, new_frame, 0)[0] == 1
    shim.call("ccmpClear", GROUP)
    assert shim.machine.mem_read(GROUP, MATERIAL - GROUP) == bytes(MATERIAL - GROUP)
    assert shim.open(GROUP, new_frame, 0)[0] == 4
    return install, rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path, help="baseline hardware.elf, adjacent to hardware.bin")
    parser.add_argument("candidate", type=Path, help="candidate hardware.elf, adjacent to hardware.bin")
    parser.add_argument("--readelf", default="arm-none-eabi-readelf")
    args = parser.parse_args()
    base_install, baseline = exercise(Shim(args.baseline, args.readelf))
    new_install, candidate = exercise(Shim(args.candidate, args.readelf))
    print("PXA270 passed AESCCM, alignment, replay, rekey and teardown checks")
    print(f"Key install instructions {base_install:,} -> {new_install:,}")
    print("Bytes           Seal instructions           Open instructions")
    for size in (32, 128, 512, 1508, 1510):
        b_seal, b_open = baseline[(size, 0)]
        c_seal, c_open = candidate[(size, 0)]
        print(f"{size:5}   {b_seal:9,} -> {c_seal:9,}     {b_open:9,} -> {c_open:9,}")


if __name__ == "__main__":
    main()
