"""Rewrite equal-length ELF dynamic symbols and rebuild the SysV hash table."""

import struct
from pathlib import Path


def elf_hash(name):
    value = 0
    for byte in name:
        value = (value << 4) + byte
        high = value & 0xF0000000
        if high:
            value ^= high >> 24
        value &= ~high
    return value


def rewrite_exports(path, rename):
    path = Path(path)
    data = bytearray(path.read_bytes())
    if data[:6] != b"\x7fELF\x02\x01":
        raise SystemExit("expected a little-endian ELF64 file")

    section_offset = struct.unpack_from("<Q", data, 0x28)[0]
    section_size, section_count, names_index = struct.unpack_from("<HHH", data, 0x3A)
    sections = [struct.unpack_from("<IIQQQQIIQQ", data, section_offset + i * section_size)
                for i in range(section_count)]
    names = sections[names_index]
    names_data = data[names[4]:names[4] + names[5]]

    def section(name):
        for item in sections:
            end = names_data.find(b"\0", item[0])
            if names_data[item[0]:end] == name:
                return item
        raise SystemExit(f"missing ELF section {name.decode()}")

    dynstr = section(b".dynstr")
    dynsym = section(b".dynsym")
    hash_section = section(b".hash")
    start, end = dynstr[4], dynstr[4] + dynstr[5]
    renamed = 0
    symbol_count = dynsym[5] // dynsym[9]
    for index in range(1, symbol_count):
        name_offset = struct.unpack_from("<I", data, dynsym[4] + index * dynsym[9])[0]
        name_start = start + name_offset
        name_end = data.find(0, name_start, end)
        old = bytes(data[name_start:name_end])
        new = rename(old)
        if new is None:
            continue
        if len(new) != len(old):
            raise SystemExit("ELF export rewrites must preserve symbol length")
        data[name_start:name_end] = new
        renamed += 1

    bucket_count, chain_count = struct.unpack_from("<II", data, hash_section[4])
    buckets = [0] * bucket_count
    chains = [0] * chain_count
    for index in range(1, chain_count):
        name_offset = struct.unpack_from("<I", data, dynsym[4] + index * dynsym[9])[0]
        name_start = start + name_offset
        name_end = data.find(0, name_start, end)
        bucket = elf_hash(data[name_start:name_end]) % bucket_count
        if buckets[bucket] == 0:
            buckets[bucket] = index
        else:
            current = buckets[bucket]
            while chains[current]:
                current = chains[current]
            chains[current] = index

    struct.pack_into(f"<{bucket_count + chain_count}I", data, hash_section[4] + 8,
                     *(buckets + chains))
    path.write_bytes(data)
    return renamed
