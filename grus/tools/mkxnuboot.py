#!/usr/bin/env python3
"""Build a grus fastboot image that boots XNU: sweet shim + payload (kernel + grus ADT).

  grus/tools/mkxnuboot.py [--kernel out/kernel/kernel.development.sweet]
                          [--cmdline "..."] [--ramdisk ROOT.img] [--out artifacts/grus-xnu.img]

Steps: grus/tools/mkadt_grus.py -> build/grus.adt; tools/mkpayload.py packs the
kernel (chained fixups resolved) and the ADT into the SWEETXNU payload; the sweet
shim (shim/, built with clang) goes in as the boot.img kernel and the payload as
its ramdisk, where shim/loader.c finds it through /chosen linux,initrd-*.
"""
import argparse
import hashlib
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO, 'grus', 'tools'))
sys.path.insert(0, os.path.join(REPO, 'tools'))
import adt  # noqa: E402
import mkadt_grus  # noqa: E402
import mkpayload  # noqa: E402
import shimbuild  # noqa: E402

SHIM_SOURCES = ('start.S', 'shim.c', 'console.c', 'mmu.c', 'fdt.c', 'loader.c', 'ufs.c', 'clang_rt.c')
# First XNU boot: one core, no root (the kernel stops at "Waiting on <dict>..."), and the
# sweet drivers whose hardware differs on grus switched off (touch: GT9886 Normandy and
# RPMh RSC; keys: PM660L GPIO 7; USB: GCC clocks not checked yet).
CMDLINE = ('debug=0xc serial=3 cpus=1 keepsyms=1 -enable_kprintf_spam -noprogress wdt=-1 '
           'wdt_test=no_user_checkin sweettouch=0 sweetkeys=0 sweetusb=0')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--kernel', default=os.path.join(REPO, 'out', 'kernel', 'kernel.development.sweet'))
    ap.add_argument('--cmdline', default=CMDLINE)
    ap.add_argument('--ramdisk', help='root image for md0 (boot with rd=md0)')
    ap.add_argument('--stock', default=os.path.join(REPO, '..', 'grus-dump', 'boot.img'))
    ap.add_argument('--out', default=os.path.join(REPO, 'artifacts', 'grus-xnu.img'))
    a = ap.parse_args()
    build = os.path.join(REPO, 'build')
    os.makedirs(build, exist_ok=True)
    os.makedirs(os.path.dirname(a.out), exist_ok=True)

    adt_path = os.path.join(build, 'grus.adt')
    open(adt_path, 'wb').write(adt.serialize(mkadt_grus.build()))
    payload, info = mkpayload.build(a.kernel, adt_path, a.cmdline, a.ramdisk)
    open(os.path.join(build, 'grus.payload'), 'wb').write(payload)
    shim = shimbuild.build_image(os.path.join(REPO, 'shim'), SHIM_SOURCES,
                                 os.path.join(build, 'sweet-shim'), werror=False)
    img = shimbuild.pack(a.stock, shim, ramdisk=payload)
    open(a.out, 'wb').write(img)
    print(f'adt     {os.path.getsize(adt_path)} B')
    print(f'payload {len(payload)} B  {info}')
    print(f'shim    {len(shim)} B')
    print(f'cmdline {a.cmdline}')
    print(f'boot    {a.out} {len(img)} B sha256 {hashlib.sha256(img).hexdigest()}')


if __name__ == '__main__':
    main()
