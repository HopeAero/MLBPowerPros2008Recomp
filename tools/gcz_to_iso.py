# SPDX-License-Identifier: GPL-3.0-or-later
"""Expand a Dolphin GCZ image to a plain ISO.

nodtool rejects GCZ files whose block size is smaller than a Wii sector (Dolphin's old default of
16 KiB, for example), so tools/linux/build.sh expands those first. Every block is checked against
its Adler-32 from the GCZ header.
Usage: gcz_to_iso.py IN.gcz OUT.iso
"""
import os
import struct
import sys
import zlib

MAGIC = 0xB10BC001


def main(source, destination):
    with open(source, 'rb') as f:
        magic, _sub_type, compressed_size, data_size, block_size, blocks = struct.unpack('<IIQQII', f.read(32))
        if magic != MAGIC:
            sys.exit(f'{source}: not a GCZ image')
        pointers = struct.unpack(f'<{blocks}Q', f.read(8 * blocks))
        hashes = struct.unpack(f'<{blocks}I', f.read(4 * blocks))
        data_start = f.tell()
        temporary = destination + '.part'
        with open(temporary, 'wb') as out:
            for i in range(blocks):
                start = pointers[i] & ~(1 << 63)
                end = (pointers[i + 1] & ~(1 << 63)) if i + 1 < blocks else compressed_size
                f.seek(data_start + start)
                raw = f.read(end - start)
                if zlib.adler32(raw) != hashes[i]:
                    sys.exit(f'{source}: block {i} fails its checksum')
                block = raw if pointers[i] >> 63 else zlib.decompress(raw)
                out.write(block[:min(block_size, data_size - i * block_size)])
                if i % 4096 == 0:
                    print(f'{100 * i // blocks}%', end='\r', flush=True)
        os.replace(temporary, destination)
    print(f'{destination}: {data_size:,} bytes')


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
