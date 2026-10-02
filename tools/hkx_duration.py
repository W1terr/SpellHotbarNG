"""Reads the duration of a Skyrim SE (hk_2010, 64 bit) animation .hkx packfile."""

import struct
import sys


def duration(data: bytes) -> float | None:
    # packfile header is 0x40 bytes, followed by section headers of 0x30 bytes each (hk_2010)
    num_sections = struct.unpack_from("<i", data, 0x14)[0]
    sections = {}
    for i in range(num_sections):
        base = 0x40 + i * 0x30
        name = data[base:base + 20].split(b"\0")[0].decode()
        start, local_fix, global_fix, virtual_fix, exports, imports, end = struct.unpack_from("<7i", data, base + 0x14)
        sections[name] = (start, local_fix, global_fix, virtual_fix, exports, imports, end)

    cls_start = sections["__classnames__"][0]
    d_start, _, _, v_fix, exports, _, _ = sections["__data__"]
    # virtual fixups: (object offset, class section index, class name offset) until -1
    pos = d_start + v_fix
    while pos + 12 <= d_start + exports:
        obj, sec, name_off = struct.unpack_from("<3i", data, pos)
        pos += 12
        if obj == -1:
            continue
        cls = data[cls_start + name_off:].split(b"\0")[0].decode(errors="replace")
        if cls.endswith("Animation") and cls.startswith("hka") and "Binding" not in cls:
            # hkReferencedObject (0x10) + type (int32) -> duration float at +0x14
            return struct.unpack_from("<f", data, d_start + obj + 0x14)[0]
    return None


if __name__ == "__main__":
    for path in sys.argv[1:]:
        print(path, duration(open(path, "rb").read()))
