#!/usr/bin/env python3
"""Minimal AmigaDOS OFS/FFS ADF reader.

    python3 tools/adf.py list    disk.adf
    python3 tools/adf.py extract disk.adf outdir
"""
import os
import struct
import sys

BS = 512


class Adf:
    def __init__(self, path):
        self.img = open(path, "rb").read()
        dostype = self.img[0:4]
        if dostype[:3] != b"DOS":
            raise ValueError("not a DOS disk")
        self.ffs = bool(dostype[3] & 1)
        self.root = len(self.img) // BS // 2

    def blk(self, n):
        return self.img[n * BS:(n + 1) * BS]

    def l(self, b, i):
        return struct.unpack_from(">l", b, i * 4)[0]

    def name(self, b):
        n = b[BS - 80]
        return b[BS - 79:BS - 79 + n].decode("latin-1")

    def entries(self, dir_block):
        b = self.blk(dir_block)
        for i in range(6, 6 + 72):
            h = self.l(b, i)
            while h:
                e = self.blk(h)
                yield h, e
                h = self.l(e, BS // 4 - 4)  # hash chain

    def read_file(self, hdr):
        b = self.blk(hdr)
        size = self.l(b, BS // 4 - 47)
        out = bytearray()
        if self.ffs:
            # data block list from header + extension blocks
            blocks = []
            cur = b
            while True:
                n = self.l(cur, 2)
                ptrs = [self.l(cur, 6 + 71 - k) for k in range(n)]
                blocks += ptrs
                ext = self.l(cur, BS // 4 - 2)
                if not ext:
                    break
                cur = self.blk(ext)
            for p in blocks:
                out += self.blk(p)
        else:
            d = self.l(b, 4)  # first data block
            while d:
                db = self.blk(d)
                n = self.l(db, 3)
                out += db[24:24 + n]
                d = self.l(db, 4)
        return bytes(out[:size])

    def walk(self, dir_block=None, prefix=""):
        if dir_block is None:
            dir_block = self.root
        for h, e in self.entries(dir_block):
            sec = self.l(e, BS // 4 - 1)
            path = prefix + self.name(e)
            if sec == 2:  # ST_USERDIR
                yield path + "/", h, None
                yield from self.walk(h, path + "/")
            else:
                yield path, h, self.l(e, BS // 4 - 47)


def main():
    cmd, path = sys.argv[1], sys.argv[2]
    adf = Adf(path)
    if cmd == "list":
        for p, h, size in adf.walk():
            print(f"{h:5} {size if size is not None else '':>8} {p}")
    elif cmd == "extract":
        outdir = sys.argv[3]
        for p, h, size in adf.walk():
            dst = os.path.join(outdir, p)
            if size is None:
                os.makedirs(dst, exist_ok=True)
            else:
                os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
                with open(dst, "wb") as f:
                    f.write(adf.read_file(h))


if __name__ == "__main__":
    main()
