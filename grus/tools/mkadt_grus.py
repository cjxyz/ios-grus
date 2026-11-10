#!/usr/bin/env python3
"""Build the Apple device tree for grus from sweet's tools/mkadt.py.

  mkadt_grus.py OUT.adt [--dump]

sweet's builder is reused as is; only what differs on the Mi 9 SE changes
(docs/grus/HARDWARE.md):
  - splash framebuffer 1080x2340 (sweet 1080x2400), same base and stride;
  - CPU nodes: Kryo 360, Silver 1.7 GHz (cores 0-5) / Gold 2.3 GHz (6-7);
  - root compatible gains xiaomi,grus and qcom,sdm712. xiaomi,sweet stays:
    SweetPlatformExpert matches on it.
RAM window (0xb0000000 + 0x4d000000), GICv3, timer INTID 19 and UFS are the
same as sweet; every fixed reserved-memory region of grus ends at or below
0xb0000000 (live DTB).
"""
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO, 'tools'))
import adt  # noqa: E402
import mkadt  # noqa: E402

FB_H = 2340
SILVER_HZ, GOLD_HZ = 1708800000, 2304000000


def find(node, name):
    for c in node.children:
        if c.name == name:
            return c
    raise SystemExit(f'ADT: no node {name}')


def set_prop(node, key, value):
    for i, (k, f, v) in enumerate(node.props):
        if k == key:
            node.props[i] = (k, f, value)
            return
    raise SystemExit(f'ADT: {node.name} has no {key}')


def build():
    mkadt.FB_H = FB_H	# read by build() for /sweet-fbcon
    root = mkadt.build()
    for cpu in find(root, 'cpus').children:
        core = int(cpu.name[3:])
        set_prop(cpu, 'compatible', mkadt.cstr('qcom,kryo360') + mkadt.cstr('ARM,v8'))
        set_prop(cpu, 'clock-frequency', mkadt.u32(GOLD_HZ if core >= 6 else SILVER_HZ))
    set_prop(root, 'compatible', b''.join(mkadt.cstr(s) for s in (
        'D22AP', 'iPhone10,3', 'xiaomi,sweet', 'xiaomi,grus', 'qcom,sdm712', 'AppleARM')))
    assert find(root, 'sweet-fbcon').prop('height') == mkadt.u32(FB_H)
    return root


if __name__ == '__main__':
    blob = adt.serialize(build())
    open(sys.argv[1], 'wb').write(blob)
    if '--dump' in sys.argv:
        adt.dump(adt.parse(blob)[0])
    print(f'{sys.argv[1]}: {len(blob)} bytes', file=sys.stderr)
