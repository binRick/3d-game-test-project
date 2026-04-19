#!/usr/bin/env python3
# Pack directories into a single bundle file. Format (little-endian):
#   magic   : 4 bytes "IFB1"
#   nfiles  : u32
#   entries : [ path_len:u16 | path:bytes | size:u32 | data:bytes ] * nfiles
#
# Invoke: pack_bundle.py <out> <root1> [<root2> ...]

import os, struct, sys

out_path, *roots = sys.argv[1:]
files = []
for root in roots:
    for dirpath, _, filenames in os.walk(root):
        for f in filenames:
            files.append(os.path.join(dirpath, f).replace("\\", "/"))
files.sort()

with open(out_path, "wb") as out:
    out.write(b"IFB1")
    out.write(struct.pack("<I", len(files)))
    total = 0
    for fp in files:
        path_b = fp.encode("utf-8")
        assert len(path_b) <= 65535, f"path too long: {fp}"
        data = open(fp, "rb").read()
        out.write(struct.pack("<H", len(path_b)))
        out.write(path_b)
        out.write(struct.pack("<I", len(data)))
        out.write(data)
        total += len(data)
print(f"Packed {len(files)} files, {total:,} bytes -> {out_path}")
