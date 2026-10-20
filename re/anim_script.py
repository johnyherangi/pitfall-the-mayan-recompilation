#!/usr/bin/env python3
"""Dump an animation script (FUN_00423134 format) from PITFALL.EXE's data section.

usage: anim_script.py <addr> [<addr> ...]
  byte < 0x80       frame
  f0                end (restart from the top, report 'looped')
  fe ev             fire event when scriptEvent == ev
  fc id pri         play sound           (ee id pri: only when Harry is near)
  fd id pri         play sound variant   (ef id pri: near only)
  fb id pri m a p   random sound: id + (rand & m) + a, (ed ...: near only)
  f7                toggle facing
  f9                obj+0x29 = 0
"""
import sys
import os
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
EXE_PATH = os.environ.get('PITFALL_EXE', os.path.join(ROOT, 'original', 'PITFALL.EXE'))
EXE = open(EXE_PATH, 'rb').read()
SECTIONS = [(0x401000, 0x400, 0x4e3c4), (0x455000, 0x4e800, 0x230), (0x456000, 0x4ec00, 0x1d1b4)]

def rd(addr, n):
    for va, raw, size in SECTIONS:
        if va <= addr < va + size:
            off = raw + addr - va
            return EXE[off:off + n]
    raise ValueError(f'{addr:#x} not in a section')

def decode(addr):
    out = []
    pos = 0
    data = rd(addr, 512)
    while pos < len(data):
        b = data[pos]
        if b < 0x80:
            out.append(str(b)); pos += 1
        elif b == 0xf0:
            out.append('END'); pos += 1; break
        elif b == 0xfe:
            out.append(f'EVENT({data[pos+1]})'); pos += 2
        elif b in (0xfc, 0xfd, 0xee, 0xef):
            out.append(f'{ {0xfc:"SND",0xfd:"SND2",0xee:"SNDNEAR",0xef:"SND2NEAR"}[b]}({data[pos+1]},{data[pos+2]})'); pos += 3
        elif b in (0xfb, 0xed):
            out.append(f'{"RSND" if b == 0xfb else "RSNDNEAR"}({data[pos+1]},{data[pos+2]},&{data[pos+3]:#x},{data[pos+4]},{data[pos+5]})'); pos += 6
        elif b == 0xf7:
            out.append('FLIP'); pos += 1
        elif b == 0xf9:
            out.append('CLR29'); pos += 1
        else:
            out.append(f'?{b:#x}'); pos += 1; break
    return pos, out

if __name__ == '__main__':
    for a in sys.argv[1:]:
        addr = int(a, 16)
        n, out = decode(addr)
        print(f'{addr:#x} ({n} bytes): ' + ' '.join(out))
