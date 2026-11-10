#!/usr/bin/env python3
"""Build the grus diag shim and wrap it in a boot.img v2 cloned from the phone's own.

  grus/diag/build.py [--stock ../grus-dump/boot.img] [--out artifacts/grus-diag.img]

Header fields (load addresses, page size, os_version) and the DTB section come from the
stock boot.img, so ABL sees the same layout as for Android and applies the same dtbo overlay.
The ramdisk is an empty newc cpio (some ABL builds reject a zero-size ramdisk).
"""
import argparse
import gzip
import hashlib
import os
import subprocess
import sys

SHIM = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(SHIM))
sys.path.insert(0, os.path.join(REPO, 'grus', 'tools'))
import bootimg  # noqa: E402

OUT = os.path.join(REPO, 'build', 'grus-diag')
SOURCES = ('start.S', 'diag.c', 'console.c', 'spmi.c')
CFLAGS = ['--target=aarch64-none-elf', '-O2', '-ffreestanding', '-nostdlib', '-fno-pic',
          '-mgeneral-regs-only', '-mstrict-align', '-fno-stack-protector', '-Wall', '-Wextra', '-Werror']


def run(*cmd):
    subprocess.run(cmd, check=True)


def empty_cpio():
    # newc trailer entry only
    name = b'TRAILER!!!\0'
    hdr = b'070701' + b''.join(b'%08X' % v for v in (0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, len(name), 0))
    body = hdr + name
    body += b'\0' * (-len(body) % 4)
    body += b'\0' * (-len(body) % 512)
    return gzip.compress(body, 9, mtime=0)


def build_image():
    os.makedirs(OUT, exist_ok=True)
    objs = []
    for src in SOURCES:
        obj = os.path.join(OUT, os.path.splitext(src)[0] + '.o')
        run('clang', *CFLAGS, '-c', os.path.join(SHIM, src), '-o', obj)
        objs.append(obj)
    # ABL runs the image wherever it loads it, with no relocation pass: only
    # the header's image_size (start.o, linked at 0) may be absolute.
    relocs = subprocess.run(['llvm-readelf', '-r', *objs[1:]], check=True,
                            capture_output=True, text=True).stdout
    if 'R_AARCH64_ABS64' in relocs:
        raise SystemExit('error: absolute relocations in C objects:\n' + relocs)
    elf = os.path.join(OUT, 'shim.elf')
    run('ld.lld', '-T', os.path.join(SHIM, 'shim.ld'), '--build-id=none', '-N', *objs, '-o', elf)
    image = os.path.join(OUT, 'Image')
    run('llvm-objcopy', '-O', 'binary', elf, image)
    nm = subprocess.run(['llvm-nm', elf], check=True, capture_output=True, text=True).stdout
    end = next(int(l.split()[0], 16) for l in nm.splitlines() if l.endswith(' _end'))
    data = open(image, 'rb').read()
    # pad to image_size so .bss and the stack exist as zeroed bytes
    return data.ljust(end, b'\0')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--stock', default=os.path.join(REPO, '..', 'grus-dump', 'boot.img'))
    ap.add_argument('--out', default=os.path.join(REPO, 'artifacts', 'grus-diag.img'))
    a = ap.parse_args()
    os.makedirs(os.path.dirname(a.out), exist_ok=True)

    kernel = build_image()
    assert kernel[0x38:0x3c] == b'ARM\x64'
    h, parts = bootimg.unpack(open(a.stock, 'rb').read())
    img = bootimg.pack(h, kernel, empty_cpio(), parts['dtb'], cmdline='console=null')
    open(a.out, 'wb').write(img)
    print(f'Image   {len(kernel)} B')
    print(f'boot    {a.out} {len(img)} B sha256 {hashlib.sha256(img).hexdigest()}')


if __name__ == '__main__':
    main()
