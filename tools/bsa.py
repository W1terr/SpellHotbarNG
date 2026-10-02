"""Minimal reader for Skyrim SE (v105) BSA archives: list and extract files."""

import struct
import sys
from pathlib import Path

import lz4.frame


class BSA:
    def __init__(self, path: Path):
        self.path = Path(path)
        self.files: dict[str, tuple[int, int]] = {}  # lowercase path -> (offset, size field)
        with open(self.path, "rb") as f:
            magic, version, offset, flags, folder_count, file_count, _, _, _ = struct.unpack("<4sIIIIIIII", f.read(36))
            assert magic == b"BSA\0" and version == 105, "only SE (v105) archives are supported"
            self.flags = flags
            folders = [struct.unpack("<QIIQ", f.read(24)) for _ in range(folder_count)]
            records = []
            for _hash, count, _pad, _off in folders:
                name_len = f.read(1)[0]
                folder = f.read(name_len)[:-1].decode("cp1252")
                for _ in range(count):
                    _fhash, size, foff = struct.unpack("<QII", f.read(16))
                    records.append((folder, size, foff))
            names = f.read().split(b"\0")
            for (folder, size, foff), name in zip(records, names):
                self.files[f"{folder}\\{name.decode('cp1252')}".lower()] = (foff, size)

    def read(self, name: str) -> bytes:
        offset, size = self.files[name.lower().replace("/", "\\")]
        compressed = bool(self.flags & 0x4) != bool(size & 0x40000000)
        size &= 0x3FFFFFFF
        with open(self.path, "rb") as f:
            f.seek(offset)
            if self.flags & 0x100:  # embedded file name
                skip = f.read(1)[0]
                f.read(skip)
                size -= skip + 1
            data = f.read(size)
        if compressed:
            return lz4.frame.decompress(data[4:])
        return data


if __name__ == "__main__":
    bsa = BSA(Path(sys.argv[1]))
    pattern = sys.argv[2].lower() if len(sys.argv) > 2 else ""
    for name in sorted(bsa.files):
        if pattern in name:
            print(name)
