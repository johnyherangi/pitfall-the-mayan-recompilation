#!/usr/bin/env python3
"""Per-level sprite slot bindings from FUN_00447190 (re/pitfall_decomp.c).

The Genesis-derived code addresses sprites through fixed slots (0x46ffc8..0x470470,
named in tools/sprite_slots.json, e.g. "obzpit2a"); when a level loads, FUN_00447190
binds each slot to one of that level's sprites by name (`FUN_004453a0(name, &slot)`),
and the name can differ from the slot's ("obrpit2a" in level 3, "obtpit2a" in level 4).
This writes src/ph/slot_bindings_data.cpp with the bindings whose names differ, so the
loader can alias the slot name to the level's sprite.

usage: gen_slot_bindings.py [re/pitfall_decomp.c] [original/PITFALL.EXE]
"""
import json, os, re, struct, sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
decomp = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, 're', 'pitfall_decomp.c')
exe = sys.argv[2] if len(sys.argv) > 2 else os.path.join(root, 'original', 'PITFALL.EXE')
d = open(exe, 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]; opt = struct.unpack_from('<H', d, pe + 20)[0]
secs = []
for i in range(nsec):
    o = pe + 24 + opt + i * 40
    vs, va, rs, ro = struct.unpack_from('<IIII', d, o + 8); secs.append((va, ro, rs))

def cstring(va):
    for sva, ro, rs in secs:
        if sva <= va - 0x400000 < sva + rs:
            o = ro + (va - 0x400000 - sva); e = d.index(b'\0', o); return d[o:e].decode('ascii')
    raise ValueError(hex(va))

slots = json.load(open(os.path.join(root, 'tools', 'sprite_slots.json')))
src = open(decomp).read()
start = src.index('void FUN_00447190(void)')
end = src.index('\n}\n', start)
body = src[start:end]
level = None
out = []
for line in body.splitlines():
    m = re.match(r'\s*case (0x[0-9a-f]+|\d+):', line)
    if m: level = int(m.group(1), 0); continue
    m = re.search(r'FUN_004453a0\((.*?),\s*\(int \*\)&(\w+)\)', line)
    if not m or level is None: continue
    name_expr, slot_sym = m.groups()
    sa = re.search(r'([0-9a-f]{8})$', slot_sym)
    sm = re.search(r'([0-9a-f]{8})$', name_expr.strip())
    if not sa or not sm: raise SystemExit(f'cannot parse: {line}')
    slot = int(sa.group(1), 16); name = cstring(int(sm.group(1), 16))
    slot_name = slots.get(f'0x{slot:06x}', '')
    if slot_name and slot_name != name:
        out.append((level, slot_name, name))
lines = ['// GENERATED from FUN_00447190 (tools/gen_slot_bindings.py): per-level sprite slot aliases.',
         '// A Genesis sprite slot (its name in tools/sprite_slots.json) is bound to a differently named',
         '// sprite of the level, e.g. slot "obzpit2a" -> "obrpit2a" in level 3 and "obtpit2a" in level 4.',
         '#include "ph/tables.h"', 'namespace ph {', 'const SlotBinding kSlotBindings[] = {']
for level, slot_name, name in out:
    lines.append(f'    {{{level}, "{slot_name}", "{name}"}},')
lines += ['    {-1, "", ""},', '};', '} // namespace ph']
open(os.path.join(root, 'src', 'ph', 'slot_bindings_data.cpp'), 'w').write('\n'.join(lines) + '\n')
print(f'wrote {len(out)} bindings')
