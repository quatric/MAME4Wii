#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders:MAME4Wii contributors
"""Pack a MEM2-linked DOL into the compressed payload format read by bootstub.c.

usage: pack.py input.dol output.z
"""

import struct
import sys
import zlib

MAGIC = 0x4D345750  # 'M4WP'
MAX_SECTIONS = 18


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__.strip())
    with open(sys.argv[1], 'rb') as f:
        dol = f.read()
    offsets = struct.unpack_from('>18I', dol, 0x00)
    addresses = struct.unpack_from('>18I', dol, 0x48)
    sizes = struct.unpack_from('>18I', dol, 0x90)
    bss_addr, bss_size, entry = struct.unpack_from('>3I', dol, 0xD8)

    sections = [(addresses[i], offsets[i], sizes[i]) for i in range(18) if sizes[i]]
    if not sections:
        sys.exit('no sections in %s' % sys.argv[1])

    header = struct.pack('>5I', MAGIC, entry, bss_addr, bss_size, len(sections))
    for addr, _, size in sections:
        header += struct.pack('>2I', addr, size)
    header += b'\0' * 8 * (MAX_SECTIONS - len(sections))

    raw = header + b''.join(dol[off:off + size] for _, off, size in sections)
    with open(sys.argv[2], 'wb') as f:
        f.write(zlib.compress(raw, 9))


if __name__ == '__main__':
    main()
