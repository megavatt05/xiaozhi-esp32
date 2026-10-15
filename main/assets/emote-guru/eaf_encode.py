"""Encoder for the Espressif .eaf animation container.

Byte layout was reverse-engineered from espressif2022__esp_emote_gfx
src/lib/eaf/gfx_eaf_dec.{c,h} and cross-checked against real asset files
(mi_2_eye_8bit_huff.eaf and friends), not guessed.

Container
    0   uint8   0x89
    1   char[3] "EAF"
    4   uint32  frame count
    8   uint32  checksum = sum of every byte from offset 16 to end of file
    12  uint32  payload length = filesize - 16
    16  table   frame count * {uint32 frame_size, uint32 frame_offset}
        data    frames, concatenated; frame_offset is relative to the end of
                the table, frame_size covers the 2-byte frame magic too

Frame
    uint16 0x5A5A
    file_data:
        0   "_S"          (a "_C" entry is a flag frame, we never write those)
        2   uint8  0
        3   uint8[6] version, big-endian 1
        9   uint8  bit depth (4 / 8 / 24) -> palette has 1<<depth entries
        10  uint16 width
        12  uint16 height
        14  uint16 block count
        16  uint16 block height
        18  uint32[block count] encoded length of each block
        ..  palette: 4 bytes per entry, ordered B, G, R, A
        ..  block data

Block
    byte 0 = encoding type (0 = RLE)
    then (count, value) pairs, count 1..255, value = palette index.
    A block must decode to exactly width * block_height pixels; the last block
    may be short in height and the decoder only consumes
    (height - block*block_height) * width pixels of it.

Transparency is not an alpha blend: the decoder treats a palette entry whose
four bytes are all zero as "draw nothing". That means pure black (0,0,0) is not
representable, so callers should avoid it (this module maps it to 1,1,1).
"""

import struct

import numpy as np

ENCODING_RLE = 0
BIT_DEPTH_8 = 8
MAGIC_HEAD = 0x5A5A
ALPHA_THRESHOLD = 128


def build_palette(colors):
    """colors: list of (r, g, b) in draw order; index 0 is reserved transparent.

    Returns (palette_bytes, lookup) where lookup maps an rgb tuple to an index.
    """
    entries = [(0, 0, 0, 0)]  # index 0 == transparent, must stay all-zero
    lookup = {}
    for rgb in colors:
        orig = tuple(int(v) for v in rgb)
        stored = (1, 1, 1) if orig == (0, 0, 0) else orig  # pure black would read as transparent
        if orig in lookup:
            continue
        lookup[orig] = len(entries)
        entries.append((stored[2], stored[1], stored[0], 255))  # stored B, G, R, A
    while len(entries) < 256:
        entries.append((0, 0, 0, 0))
    if len(entries) > 256:
        raise ValueError('too many palette entries: %d' % len(entries))
    pal = np.array(entries, dtype=np.uint8).reshape(-1)
    return pal.tobytes(), lookup


def rle_block(flat):
    """(count, value) pairs for one block of palette indices."""
    out = bytearray()
    n = flat.size
    i = 0
    while i < n:
        v = flat[i]
        j = i + 1
        while j < n and flat[j] == v and j - i < 255:
            j += 1
        out.append(j - i)
        out.append(int(v))
        i = j
    return out


def encode_frame(idx, palette_bytes, block_height):
    h, w = idx.shape
    blocks = (h + block_height - 1) // block_height
    payload = []
    for b in range(blocks):
        rows = idx[b * block_height:(b + 1) * block_height, :]
        body = rle_block(np.ascontiguousarray(rows).ravel())
        payload.append(bytes([ENCODING_RLE]) + bytes(body))
    lens = struct.pack('<%dI' % blocks, *[len(p) for p in payload])
    header = b'_S' + bytes([0]) + struct.pack('>Q', 1)[2:]  # 1 flag byte + 6 version bytes
    header += bytes([BIT_DEPTH_8])
    header += struct.pack('<HHHH', w, h, blocks, block_height)
    return (struct.pack('<H', MAGIC_HEAD) + header + lens + palette_bytes
            + b''.join(payload))


def encode_eaf(frames, palette_bytes, block_height=16):
    """frames: iterable of 2-D uint8 numpy arrays holding palette indices."""
    frames = [np.ascontiguousarray(f, dtype=np.uint8) for f in frames]
    blobs = [encode_frame(f, palette_bytes, block_height) for f in frames]
    table_len = 8 * len(blobs)
    data = bytearray()
    table = bytearray()
    off = 0
    for blob in blobs:
        table += struct.pack('<II', len(blob), off)
        data += blob
        off += len(blob)
    body = bytes(table) + bytes(data)
    checksum = sum(body) & 0xFFFFFFFF
    return (bytes([0x89]) + b'EAF'
            + struct.pack('<III', len(blobs), checksum, len(body)) + body)


def indices_from_rgba(rgba, lookup):
    """RGBA uint8 array -> palette indices; alpha below threshold -> index 0."""
    rgb = rgba[..., :3].astype(np.int64)
    alpha = rgba[..., 3]
    key = (rgb[..., 0] << 16) | (rgb[..., 1] << 8) | rgb[..., 2]
    out = np.zeros(rgba.shape[:2], dtype=np.uint8)
    opaque = alpha >= ALPHA_THRESHOLD
    if not opaque.any():
        return out
    keys = np.array(sorted((r << 16) | (g << 8) | b for (r, g, b) in lookup), dtype=np.int64)
    vals = np.array([lookup[(int(k) >> 16 & 255, int(k) >> 8 & 255, int(k) & 255)] for k in keys],
                    dtype=np.uint8)
    pos = np.searchsorted(keys, key[opaque])
    clipped = np.clip(pos, 0, keys.size - 1)
    unknown = keys[clipped] != key[opaque]
    if unknown.any():
        bad = key[opaque][unknown]
        uniq = np.unique(bad)[:4]
        raise ValueError('colors not in palette: %d pixels, e.g. %s'
                         % (int(unknown.sum()),
                            [(int(c) >> 16 & 255, int(c) >> 8 & 255, int(c) & 255) for c in uniq]))
    out[opaque] = vals[np.minimum(pos, keys.size - 1)]
    return out
