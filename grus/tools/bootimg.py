#!/usr/bin/env python3
"""Android boot image header v2: unpack and pack (the subset grus ABL needs).

  bootimg.py unpack boot.img outdir     -> kernel, ramdisk, dtb, header.txt
  bootimg.py selftest boot.img          -> repack the parts, compare with the input
"""
import hashlib
import os
import struct
import sys

HDR = struct.Struct('<8s10I16s512s32s1024sIQIIQ')
FIELDS = ('magic kernel_size kernel_addr ramdisk_size ramdisk_addr second_size second_addr '
          'tags_addr page_size header_version os_version name cmdline id extra_cmdline '
          'recovery_dtbo_size recovery_dtbo_offset header_size dtb_size dtb_addr').split()


def pad(n, page):
    return (n + page - 1) // page * page


def unpack(data):
    h = dict(zip(FIELDS, HDR.unpack_from(data)))
    if h['magic'] != b'ANDROID!' or h['header_version'] != 2:
        raise SystemExit('not a boot.img v2')
    page, off, parts = h['page_size'], h['page_size'], {}
    for name in ('kernel', 'ramdisk', 'second', 'recovery_dtbo', 'dtb'):
        size = h[name + '_size']
        parts[name] = data[off:off + size]
        off += pad(size, page)
    return h, parts


def pack(h, kernel, ramdisk, dtb, cmdline=None):
    """Header fields (addresses, page size, os_version) come from h; second and dtbo stay empty."""
    page = h['page_size']
    sha = hashlib.sha1()
    for blob in (kernel, ramdisk, b'', b'', dtb):	# kernel, ramdisk, second, recovery_dtbo, dtb
        sha.update(blob)
        sha.update(struct.pack('<I', len(blob)))
    cmd = h['cmdline'] if cmdline is None else cmdline.encode()
    hdr = HDR.pack(b'ANDROID!', len(kernel), h['kernel_addr'], len(ramdisk), h['ramdisk_addr'],
                   0, h['second_addr'], h['tags_addr'], page, 2, h['os_version'], h['name'],
                   cmd.ljust(512, b'\0'), sha.digest().ljust(32, b'\0'), h['extra_cmdline'],
                   0, 0, HDR.size, len(dtb), h['dtb_addr'])
    out = bytearray(hdr.ljust(page, b'\0'))
    for blob in (kernel, ramdisk, dtb):
        out += blob.ljust(pad(len(blob), page), b'\0')
    return bytes(out)


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    data = open(sys.argv[2], 'rb').read()
    h, parts = unpack(data)
    if sys.argv[1] == 'unpack':
        os.makedirs(sys.argv[3], exist_ok=True)
        for name in ('kernel', 'ramdisk', 'dtb'):
            open(os.path.join(sys.argv[3], name), 'wb').write(parts[name])
        with open(os.path.join(sys.argv[3], 'header.txt'), 'w') as f:
            for k in FIELDS:
                v = h[k]
                f.write(f'{k} {v.rstrip(bytes(1)).decode(errors="replace") if isinstance(v, bytes) else hex(v)}\n')
    elif sys.argv[1] == 'selftest':
        if parts['second'] or parts['recovery_dtbo']:
            raise SystemExit('selftest needs empty second/recovery_dtbo')
        out = pack(h, parts['kernel'], parts['ramdisk'], parts['dtb'])
        same = data[:len(out)] == out and not data[len(out):].strip(b'\0')
        print('selftest', 'OK' if same else 'MISMATCH', len(out), 'bytes')
        sys.exit(0 if same else 1)
    else:
        raise SystemExit(__doc__)


if __name__ == '__main__':
    main()
