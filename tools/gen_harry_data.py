#!/usr/bin/env python3
"""Copy the data Harry's state code reads out of original/PITFALL.EXE into src/game/harry_data.cpp.

Harry's state code (0x426810..0x431c99, the machine-translated 68000 half) keeps its animation
scripts and tables at absolute addresses, both in .data and inline in .text, and compares
script pointers with those addresses.  The port therefore keeps the original addresses as
values and reads the bytes through game::harryMem(va), backed by the blobs generated here.

tools/harry_data.json lists what to copy:
  "ranges":  [["0xstart", "0xend"], ...]   whole address ranges (used for the .data script area)
  "bytes":   ["0xaddr", ...]               FUN_00423134 byte scripts: copied up to their 0xf0
  "ptr":     ["0xaddr", ...]               FUN_004237a5 pointer scripts: walked opcode by opcode,
                                           following 0xf3 jumps and 0xf2/0xf4/0xf5 branches
  "raw":     {"0xaddr": "0xlen", ...}      fixed-length tables
Sprite slot pointers inside the scripts (0xf1) stay slot addresses; the slot names come from
tools/sprite_slots.json (kSpriteSlots).
"""
import json, os, struct, sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
exe = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, 'original', 'PITFALL.EXE')
d = open(exe, 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]; opt = struct.unpack_from('<H', d, pe + 20)[0]
secs = []
for i in range(nsec):
    o = pe + 24 + opt + i * 40
    vs, va, rs, ro = struct.unpack_from('<IIII', d, o + 8); secs.append((va, ro, max(rs, vs)))


def rd(va, n):
    for sva, ro, rs in secs:
        if sva <= va - 0x400000 < sva + rs:
            o = ro + (va - 0x400000 - sva); return d[o:o + n]
    raise ValueError(hex(va))


def byte_script_end(va):
    data = rd(va, 2048); pos = 0
    while pos < len(data):
        b = data[pos]
        if b == 0xf0: return va + pos + 1
        elif b == 0xfe: pos += 2
        elif b in (0xfc, 0xfd, 0xee, 0xef): pos += 3
        elif b in (0xfb, 0xed): pos += 6
        else: pos += 1
    raise ValueError(f'unterminated byte script {va:#x}')


def ptr_script_ranges(va, seen):
    """[start, end) ranges a FUN_004237a5 script can reach from va."""
    out, todo = [], [va]
    while todo:
        start = todo.pop()
        if start in seen: continue
        seen.add(start)
        pos = start
        while True:
            b = rd(pos, 1)[0]
            if b == 0xf0: pos += 1; break
            if b == 0xf3:
                todo.append(struct.unpack_from('<I', rd(pos + 1, 4))[0]); pos += 5; break
            if b in (0xf2, 0xf4, 0xf5):
                todo.append(struct.unpack_from('<I', rd(pos + 2, 4))[0]); pos += 6; continue
            if b == 0xf1: pos += 5
            elif b in (0xfc, 0xfd): pos += 3
            elif b in (0xfb, 0xed): pos += 6
            elif b in (0xf6, 0xfa): pos += 13
            else: pos += 1               # frames, f7 / f8 / f9, and any other byte (shown as a frame)
            if pos - start > 4096: raise ValueError(f'runaway pointer script {start:#x}')
        out.append((start, pos))
    return out


spec = json.load(open(os.path.join(root, 'tools', 'harry_data.json')))
ranges = [(int(a, 16), int(b, 16)) for a, b in spec.get('ranges', [])]
for a in spec.get('bytes', []):
    v = int(a, 16); ranges.append((v, byte_script_end(v)))
seen = set()
for a in spec.get('ptr', []):
    ranges += ptr_script_ranges(int(a, 16), seen)
for a, n in spec.get('raw', {}).items():
    v = int(a, 16); ranges.append((v, v + int(n, 16)))
ranges.sort()
merged = []
for a, b in ranges:
    if merged and a <= merged[-1][1]: merged[-1][1] = max(merged[-1][1], b)
    else: merged.append([a, b])

slots = json.load(open(os.path.join(root, 'tools', 'sprite_slots.json')))
out = ['// GENERATED from PITFALL.EXE (tools/gen_harry_data.py, tools/harry_data.json) - do not edit.',
       '// The bytes Harry\'s state code reads at their original addresses: animation scripts and tables.',
       '#include "game/harry.h"', '', 'namespace game {', 'namespace {']
for i, (a, b) in enumerate(merged):
    data = rd(a, b - a)
    out.append(f'const uint8_t kBlob{i}[] = {{   // {a:#x}..{b:#x}')
    for k in range(0, len(data), 16):
        out.append('    ' + ', '.join(f'0x{x:02x}' for x in data[k:k + 16]) + ',')
    out.append('};')
out.append('struct Blob { uint32_t va, len; const uint8_t* p; };')
out.append('const Blob kBlobs[] = {')
for i, (a, b) in enumerate(merged):
    out.append(f'    {{{a:#x}, {b - a:#x}, kBlob{i}}},')
out.append('};')
out.append('} // namespace')
out.append('')
out.append('const uint8_t* harryMem(uint32_t va) {')
out.append('    for (const Blob& b : kBlobs) if (va >= b.va && va < b.va + b.len) return b.p + (va - b.va);')
out.append('    return nullptr;')
out.append('}')
out.append('')
out.append('const SpriteSlot kSpriteSlots[] = {')
for k, v in sorted(slots.items(), key=lambda kv: int(kv[0], 16)):
    out.append(f'    {{{int(k, 16):#x}, "{v}"}},')
out.append('};')
out.append(f'const int kSpriteSlotCount = {len(slots)};')
out.append('')
out.append('} // namespace game')
path = os.path.join(root, 'src', 'game', 'harry_data.cpp')
open(path, 'w', newline='\n').write('\n'.join(out) + '\n')
print(f'{path}: {len(merged)} blobs, {sum(b - a for a, b in merged)} bytes')
