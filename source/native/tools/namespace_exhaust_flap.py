#!/usr/bin/env python3
"""Rename the PR #29 Exhaust Flap class exported as MhdMaps."""

import argparse
from hashlib import sha256
from io import BytesIO
from pathlib import Path
from struct import pack_into, unpack_from

from elftools.elf.elffile import ELFFile

ORIGINAL_SHA256 = "5b61fc8913efdc4a756b5ffac10dada9eab8e4595fbb13812c29f01e1ab75af3"
OLD_NAME = b"7MhdMaps"
NEW_NAME = b"7ExhFlap"


def elf_hash(name):
    value = 0
    for char in name:
        value = (value << 4) + char
        high_bits = value & 0xF0000000
        if high_bits:
            value ^= high_bits >> 24
        value &= ~high_bits
    return value


def rename(original):
    if sha256(original).hexdigest() != ORIGINAL_SHA256:
        raise ValueError("Expected the original PR #29 libExhaustFlap.so")

    elf = ELFFile(BytesIO(original))
    output = bytearray(original)
    for section_name in (".dynstr", ".strtab", ".rodata"):
        section = elf.get_section_by_name(section_name)
        offset, size = section["sh_offset"], section["sh_size"]
        output[offset:offset + size] = section.data().replace(OLD_NAME, NEW_NAME)

    # The shipped library uses the SysV ELF symbol hash. Rebuild the buckets
    # without reordering dynamic symbols, which are referenced by relocations.
    updated = ELFFile(BytesIO(output))
    symbols = updated.get_section_by_name(".dynsym")
    hashes = elf.get_section_by_name(".hash")
    offset = hashes["sh_offset"]
    bucket_count, chain_count = unpack_from("<II", original, offset)
    assert bucket_count > 0 and chain_count == symbols.num_symbols()
    assert hashes["sh_size"] == 4 * (2 + bucket_count + chain_count)
    buckets = [0] * bucket_count
    chains = [0] * chain_count
    for index, symbol in enumerate(symbols.iter_symbols()):
        if index:
            slot = elf_hash(symbol.name.encode("ascii")) % bucket_count
            chains[index] = buckets[slot]
            buckets[slot] = index
    pack_into(f"<{bucket_count + chain_count}I", output, offset + 8, *buckets, *chains)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.write_bytes(rename(args.input.read_bytes()))


if __name__ == "__main__":
    main()
