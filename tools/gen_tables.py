#!/usr/bin/env python3
"""Regenerate src/ph/tables_data.cpp and src/ph/object_templates_data.cpp from original/PITFALL.EXE.

Data recovered from the executable:
  0x4668c4  collision shape height table (16 bytes per shape)
  0x461f8e  trigger class table
  0x433334  Harry's start facing per level
  0x4678c4  object template pointer table (256 entries) -> templates
  0x468824  sprite hit boxes {slot, dx, w, dy, h} (FUN_00424af1 collision test)
Sprite slot names come from tools/object_templates.json / tools/sprite_slots.json (Ghidra symbol table).
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
    vs, va, rs, ro = struct.unpack_from('<IIII', d, o + 8); secs.append((va, ro, rs))
def rd(va, n):
    for sva, ro, rs in secs:
        if sva <= va - 0x400000 < sva + rs:
            o = ro + (va - 0x400000 - sva); return d[o:o + n]
heights = rd(0x4668c4, 16 * 256); trgattr = rd(0x461f8e, 0xff); facing = rd(0x433334, 50)
out = ['// GENERATED from PITFALL.EXE data (tools/gen_tables.py) - do not edit by hand.',
       '#include "ph/tables.h"', 'namespace ph {',
       '// 0x4668c4: surface height per x within a 16x16 collision cell, 16 bytes per shape id',
       '// (all 256 ids: the probes index the table with the whole id byte).',
       'const uint8_t kCollisionHeights[256 * 16] = {']
for i in range(256):
    out.append('    ' + ', '.join(f'0x{b:02x}' for b in heights[i * 16:(i + 1) * 16]) + ',')
out += ['};', '// 0x461f8e: trigger class per trigger type (copied to 0x4658c0 at level start).', 'const uint8_t kTriggerClass[255] = {']
for i in range(0, 255, 16):
    out.append('    ' + ', '.join(f'0x{b:02x}' for b in trgattr[i:i + 16]) + ',')
out += ['};', "// 0x433334: Harry's initial facing per level (0 = right, 1 = left).",
        'const uint8_t kStartFacing[25] = {' + ', '.join(f'0x{facing[i * 2]:02x}' for i in range(25)) + '};',
        '} // namespace ph']
open(os.path.join(root, 'src', 'ph', 'tables_data.cpp'), 'w').write('\n'.join(out) + '\n')

t = json.load(open(os.path.join(root, 'tools', 'object_templates.json')))
out = ['// GENERATED from PITFALL.EXE data at 0x4678c4 (tools/gen_tables.py): object templates by .als id.',
       '// FUN_0040454e: x = col*16 + 0x18 + xoff, y = row*16 - 0x10 + yoff (+ floor snap when snap != 0).',
       '#include "ph/tables.h"', 'namespace ph {', 'const ObjectTemplate kObjectTemplates[256] = {']
for i in range(256):
    v = t.get(str(i))
    if v:
        out.append(f'    {{{v["type"]}, {v["sub"]}, 0x{v["flags"]:04x}, {v["snd"]}, {v["xoff"]}, {v["yoff"]}, {v["snap"]}, "{v["sprite"] or ""}"}},  // 0x{i:02x}')
    else:
        out.append('    {0, 0, 0, 0, 0, 0, 0, ""},')
out += ['};']
slots = json.load(open(os.path.join(root, 'tools', 'sprite_slots.json')))
out += ['// 0x468824: collision boxes relative to the object position, looked up by sprite (FUN_00424af1).',
        'const SpriteHitbox kHitboxes[] = {']
a = 0x468824
n = 0
while True:
    key = struct.unpack('<i', rd(a, 4))[0]
    if key == -1: break
    dx, w, dy, h = struct.unpack('<hhhh', rd(a + 4, 8))
    out.append(f'    {{"{slots.get(f"0x{key:06x}", "")}", {dx}, {w}, {dy}, {h}}},')
    a += 12; n += 1
out += ['};', f'const int kHitboxCount = {n};', '} // namespace ph']
open(os.path.join(root, 'src', 'ph', 'object_templates_data.cpp'), 'w').write('\n'.join(out) + '\n')
print('wrote tables_data.cpp and object_templates_data.cpp')
