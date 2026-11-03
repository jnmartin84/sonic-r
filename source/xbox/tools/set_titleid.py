#!/usr/bin/env python3
"""Give default.xbe its own title ID (cxbe always writes 0xFFFF0002, shared by
every nxdk program). The ID names the save folder E:\\UDATA\\<ID>.

usage: set_titleid.py default.xbe [HEX_ID]      (default FFFF5352)
"""
import struct, sys
path = sys.argv[1]
tid = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0xFFFF5352
d = bytearray(open(path, "rb").read())
base, = struct.unpack_from("<I", d, 0x104)
cert, = struct.unpack_from("<I", d, 0x118)
off = cert - base + 8
old, = struct.unpack_from("<I", d, off)
struct.pack_into("<I", d, off, tid)
open(path, "wb").write(d)
print("title id %08X -> %08X" % (old, tid))
