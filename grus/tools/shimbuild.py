"""Build a bare-metal shim Image with clang/lld and wrap it in a grus boot.img v2.

Shared by grus/diag/build.py (diagnostic shim) and grus/tools/mkxnuboot.py (the
sweet shim + XNU payload). Header fields (load addresses, page size, os_version)
and the DTB section come from the phone's own boot.img, so ABL sees the same
layout as for Android and applies the same dtbo overlay.
"""
import gzip
import os
import subprocess

import bootimg

CFLAGS = ['--target=aarch64-none-elf', '-O2', '-ffreestanding', '-nostdlib', '-fno-pic',
          '-mgeneral-regs-only', '-mstrict-align', '-fno-stack-protector', '-Wall', '-Wextra',
          # clang turns switches over string literals into pointer tables (ABS64)
          '-fno-jump-tables']


def run(*cmd):
    subprocess.run(cmd, check=True)


def empty_cpio():
    """Gzipped newc archive with only the trailer (some ABL builds reject a zero-size ramdisk)."""
    name = b'TRAILER!!!\0'
    hdr = b'070701' + b''.join(b'%08X' % v for v in (0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, len(name), 0))
    body = hdr + name
    body += b'\0' * (-len(body) % 4)
    body += b'\0' * (-len(body) % 512)
    return gzip.compress(body, 9, mtime=0)


def build_image(src_dir, sources, out_dir, werror=True):
    """Compile and link sources (start.S first) with src_dir/shim.ld; return the Image padded to _end."""
    os.makedirs(out_dir, exist_ok=True)
    flags = CFLAGS + (['-Werror'] if werror else [])
    objs = []
    for src in sources:
        obj = os.path.join(out_dir, os.path.splitext(src)[0] + '.o')
        run('clang', *flags, '-c', os.path.join(src_dir, src), '-o', obj)
        objs.append(obj)
    # ABL runs the image wherever it loads it (0x80080000 on grus), with no
    # relocation pass: only the header's image_size (start.o, linked at 0) may be absolute.
    relocs = subprocess.run(['llvm-readelf', '-r', *objs[1:]], check=True,
                            capture_output=True, text=True).stdout
    if 'R_AARCH64_ABS64' in relocs:
        bad = [l for l in relocs.splitlines() if 'ABS64' in l or l.startswith('File:') or 'section' in l]
        raise SystemExit('error: absolute relocations in C objects:\n' + '\n'.join(bad))
    elf = os.path.join(out_dir, 'shim.elf')
    run('ld.lld', '-T', os.path.join(src_dir, 'shim.ld'), '--build-id=none', '-N', *objs, '-o', elf)
    image = os.path.join(out_dir, 'Image')
    run('llvm-objcopy', '-O', 'binary', elf, image)
    nm = subprocess.run(['llvm-nm', elf], check=True, capture_output=True, text=True).stdout
    end = next(int(l.split()[0], 16) for l in nm.splitlines() if l.endswith(' _end'))
    data = open(image, 'rb').read().ljust(end, b'\0')	# .bss and stacks as zeroed bytes
    assert data[0x38:0x3c] == b'ARM\x64', 'missing arm64 Image header'
    return data


def pack(stock_path, kernel, ramdisk=None, cmdline='console=null'):
    h, parts = bootimg.unpack(open(stock_path, 'rb').read())
    return bootimg.pack(h, kernel, ramdisk if ramdisk is not None else empty_cpio(), parts['dtb'],
                        cmdline=cmdline)
