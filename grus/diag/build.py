#!/usr/bin/env python3
"""Build the grus diag shim and wrap it in a boot.img v2 cloned from the phone's own.

  grus/diag/build.py [--stock ../grus-dump/boot.img] [--out artifacts/grus-diag.img]

The ramdisk is an empty newc cpio; see grus/tools/shimbuild.py.
"""
import argparse
import hashlib
import os
import sys

SHIM = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(SHIM))
sys.path.insert(0, os.path.join(REPO, 'grus', 'tools'))
import shimbuild  # noqa: E402

OUT = os.path.join(REPO, 'build', 'grus-diag')
SOURCES = ('start.S', 'diag.c', 'console.c', 'spmi.c')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--stock', default=os.path.join(REPO, '..', 'grus-dump', 'boot.img'))
    ap.add_argument('--out', default=os.path.join(REPO, 'artifacts', 'grus-diag.img'))
    a = ap.parse_args()
    os.makedirs(os.path.dirname(a.out), exist_ok=True)

    kernel = shimbuild.build_image(SHIM, SOURCES, OUT)
    img = shimbuild.pack(a.stock, kernel)
    open(a.out, 'wb').write(img)
    print(f'Image   {len(kernel)} B')
    print(f'boot    {a.out} {len(img)} B sha256 {hashlib.sha256(img).hexdigest()}')


if __name__ == '__main__':
    main()
