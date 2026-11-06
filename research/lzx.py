"""Minimal LZX decoder (scratch reference, written from the public LZX algorithm description).

Bitstream: 16-bit little-endian words, read MSB first. Frames of 32768 output bytes.
"""

MIN_MATCH = 2
NUM_CHARS = 256
PRETREE_N = 20
LENGTH_N = 249
ALIGNED_N = 8

EXTRA = []
j = 0
for i in range(0, 52, 2):
    EXTRA += [j, j]
    if i != 0 and j < 17:
        j += 1
BASE = [0]
for i in range(51):
    BASE.append(BASE[-1] + (1 << EXTRA[i]))
SLOTS = {15: 30, 16: 32, 17: 34, 18: 36, 19: 38, 20: 42, 21: 50}


class LzxError(Exception):
    pass


class Bits:
    def __init__(self, data):
        self.d = data
        self.p = 0
        self.buf = 0
        self.n = 0

    def _fill(self):
        while self.n <= 16:
            if self.p + 1 < len(self.d):
                w = self.d[self.p] | (self.d[self.p + 1] << 8)
            elif self.p < len(self.d):
                w = self.d[self.p]
            else:
                w = 0
            self.p += 2
            self.buf = (self.buf << 16) | w
            self.n += 16

    def peek(self, k):
        self._fill()
        return (self.buf >> (self.n - k)) & ((1 << k) - 1)

    def read(self, k):
        if k == 0:
            return 0
        if k > 16:
            hi = self.read(k - 16)
            return (hi << 16) | self.read(16)
        v = self.peek(k)
        self.n -= k
        self.buf &= (1 << self.n) - 1
        return v

    def align16(self):
        self.n -= self.n % 16

    def over(self):
        return self.p - (self.n // 16) * 2 > len(self.d) + 2


class Huff:
    def __init__(self, lens):
        self.table = {}
        code = 0
        maxlen = max(lens) if lens else 0
        if maxlen == 0:
            self.maxlen = 0
            return
        for L in range(1, maxlen + 1):
            for sym, l in enumerate(lens):
                if l == L:
                    self.table[(L, code)] = sym
                    code += 1
            code <<= 1
        self.maxlen = maxlen

    def decode(self, b):
        if self.maxlen == 0:
            raise LzxError("empty tree")
        code = 0
        for L in range(1, self.maxlen + 1):
            code = (code << 1) | b.read(1)
            s = self.table.get((L, code))
            if s is not None:
                return s
        raise LzxError("bad huffman code")


def read_lengths(b, lens, first, last):
    pre = Huff([b.read(4) for _ in range(PRETREE_N)])
    x = first
    while x < last:
        z = pre.decode(b)
        if z == 17:
            for _ in range(4 + b.read(4)):
                lens[x] = 0; x += 1
        elif z == 18:
            for _ in range(20 + b.read(5)):
                lens[x] = 0; x += 1
        elif z == 19:
            run = 4 + b.read(1)
            z = pre.decode(b)
            v = (lens[x] - z + 17) % 17
            for _ in range(run):
                lens[x] = v; x += 1
        else:
            lens[x] = (lens[x] - z + 17) % 17
            x += 1


class Lzx:
    def __init__(self, window_bits):
        self.wsize = 1 << window_bits
        self.slots = SLOTS[window_bits]
        self.main_lens = [0] * (NUM_CHARS + self.slots * 8)
        self.len_lens = [0] * LENGTH_N
        self.R = [1, 1, 1]
        self.out = bytearray()
        self.header_read = False
        self.block_remaining = 0
        self.block_type = 0

    def decompress(self, data, out_len, read_header_each_frame=False):
        """Decode one chunk (one or more 32K frames) into out_len bytes."""
        b = Bits(data)
        start = len(self.out)
        if read_header_each_frame or not self.header_read:
            if b.read(1):
                b.read(32)  # E8 file size; translation not applied here
            self.header_read = True
        target = start + out_len
        while len(self.out) < target:
            if self.block_remaining == 0:
                self.block_type = b.read(3)
                self.block_remaining = (b.read(16) << 8) | b.read(8)
                if self.block_type == 2:
                    self.aligned = Huff([b.read(3) for _ in range(ALIGNED_N)])
                if self.block_type in (1, 2):
                    read_lengths(b, self.main_lens, 0, NUM_CHARS)
                    read_lengths(b, self.main_lens, NUM_CHARS, len(self.main_lens))
                    self.main = Huff(self.main_lens)
                    read_lengths(b, self.len_lens, 0, LENGTH_N)
                    self.length = Huff(self.len_lens) if any(self.len_lens) else None
                elif self.block_type == 3:
                    b.align16()
                    # remaining buffered whole words are part of the raw stream; rewind to them
                    b.p -= (b.n // 16) * 2
                    b.n = 0; b.buf = 0
                    for k in range(3):
                        self.R[k] = int.from_bytes(data[b.p:b.p + 4], 'little'); b.p += 4
                else:
                    raise LzxError(f"bad block type {self.block_type}")
            n = min(self.block_remaining, target - len(self.out))
            if self.block_type == 3:
                self.out += data[b.p:b.p + n]; b.p += n
                self.block_remaining -= n
                if self.block_remaining == 0 and (n & 1):
                    b.p += 1
                continue
            end = len(self.out) + n
            while len(self.out) < end:
                m = self.main.decode(b)
                if m < NUM_CHARS:
                    self.out.append(m)
                    continue
                m -= NUM_CHARS
                ml = m & 7
                if ml == 7:
                    if self.length is None:
                        raise LzxError("no length tree")
                    ml += self.length.decode(b)
                ml += MIN_MATCH
                slot = m >> 3
                if slot == 0:
                    off = self.R[0]
                elif slot == 1:
                    off = self.R[1]; self.R[1] = self.R[0]; self.R[0] = off
                elif slot == 2:
                    off = self.R[2]; self.R[2] = self.R[0]; self.R[0] = off
                else:
                    extra = EXTRA[slot]
                    off = BASE[slot] - 2
                    if self.block_type == 2:
                        if extra > 3:
                            off += b.read(extra - 3) << 3
                            off += self.aligned.decode(b)
                        elif extra == 3:
                            off += self.aligned.decode(b)
                        elif extra > 0:
                            off += b.read(extra)
                        else:
                            off = 1
                    else:
                        off += b.read(extra)
                    self.R[2] = self.R[1]; self.R[1] = self.R[0]; self.R[0] = off
                if off > len(self.out):
                    raise LzxError(f"offset {off} beyond output {len(self.out)}")
                for _ in range(ml):
                    self.out.append(self.out[-off])
                if b.over():
                    raise LzxError("input overrun")
            self.block_remaining -= n
            # LZX frames are 32K; realign at each frame boundary
            if (len(self.out) - 0) % 32768 == 0:
                b.align16()
        return bytes(self.out[start:target]), b
