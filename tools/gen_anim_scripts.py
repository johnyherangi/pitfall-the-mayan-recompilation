#!/usr/bin/env python3
"""Extract object animation scripts from original/PITFALL.EXE into src/game/anim_scripts_data.cpp.

Script format (FUN_00423134, the object animation interpreter):
  byte < 0x80     show this frame
  f0              end of script: restart from the top and report "looped"
  fe ev           fire the level script event `ev` when it is pending (DAT_004627c1)
  fc id pri       play sound id (FUN_004262d9)          ee id pri: only when Harry is near
  fd id pri       play sound id, SFX channel (FUN_004261e2)   ef id pri: only when Harry is near
  fb id pri m a p random sound id + (rand & m) + a       ed ...: only when Harry is near
  f7              toggle the object's facing
  f9              clear obj+0x29 (vertical flip)
The addresses come from tools/anim_scripts.json ({"name": "0xaddr"}); its "raw" section
copies byte ranges ("0xaddr+0xlen") verbatim for handlers with their own script formats.
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
    raise ValueError(hex(va))

def script_len(addr):
    data = rd(addr, 1024)
    pos = 0
    while pos < len(data):
        b = data[pos]
        if b < 0x80 or b in (0xf7, 0xf9): pos += 1
        elif b == 0xf0: return pos + 1
        elif b == 0xfe: pos += 2
        elif b in (0xfc, 0xfd, 0xee, 0xef): pos += 3
        elif b in (0xfb, 0xed): pos += 6
        else: pos += 1          # any other byte is shown as the frame (0xff = hidden)
    raise ValueError('unterminated')

def anim2_extract(addr, slots, sprites, length=None):
    """FUN_004237a5 scripts embed 32-bit pointers: jump targets (0xf3) become offsets
    from the script start, sprite slots (0xf1) become indices into kAnim2Sprites."""
    data = bytearray(rd(addr, 1024))
    pos, end, targets = 0, None, set()
    while True:
        if length is not None and pos >= length: end = length; break
        b = data[pos]
        if b < 0x80 or b in (0xf7, 0xf8, 0xf9): pos += 1
        elif b == 0xf0:
            pos += 1
            if all(t < pos for t in targets): end = pos; break
        elif b in (0xfc, 0xfd): pos += 3
        elif b in (0xfb, 0xed): pos += 6
        elif b == 0xf3:
            t = struct.unpack_from('<I', data, pos + 1)[0] - addr
            if not 0 <= t < 1024: raise ValueError(f'jump out of range at {addr + pos:#x}')
            targets.add(t); struct.pack_into('<i', data, pos + 1, t); pos += 5
            if all(t < pos for t in targets): end = pos; break
        elif b == 0xf1:
            slot = struct.unpack_from('<I', data, pos + 1)[0]
            name = slots.get(f'0x{slot:06x}', '')
            if name not in sprites: sprites.append(name)
            struct.pack_into('<i', data, pos + 1, sprites.index(name)); pos += 5
        elif b in (0xf2, 0xf4, 0xf5): pos += 6          # Harry-only conditional jumps
        else: pos += 1          # any other byte is shown as the frame (0xff = hidden)
    return bytes(data[:end])

scripts = json.load(open(os.path.join(root, 'tools', 'anim_scripts.json')))
slots = json.load(open(os.path.join(root, 'tools', 'sprite_slots.json')))
out = ['// GENERATED from PITFALL.EXE data (tools/gen_anim_scripts.py) - object animation scripts.',
       '// Format: see tools/gen_anim_scripts.py / game::animStep (FUN_00423134) and anim2Step (FUN_004237a5).',
       '#include "game/objects.h"', 'namespace game {']
for name, addr in scripts['anim'].items():
    a = int(addr, 16)
    n = script_len(a)
    bytes_ = rd(a, n)
    out.append(f'const uint8_t kAnim_{name}[] = {{' + ', '.join(f'0x{b:02x}' for b in bytes_) + f'}};  // {addr}')
sprites = []
for name, addr in scripts['anim2'].items():
    if '+' in addr:                                   # "0xaddr+len": a fixed length (no 0xf0 terminator)
        a, n = (int(x, 0) for x in addr.split('+'))
        bytes_ = anim2_extract(a, slots, sprites, n)
    else:
        a = int(addr, 16)
        bytes_ = anim2_extract(a, slots, sprites)
    out.append(f'const uint8_t kAnim2_{name}[] = {{' + ', '.join(f'0x{b:02x}' for b in bytes_) + f'}};  // {addr}')
out.append('const char* const kAnim2Sprites[] = {' + ', '.join(f'"{n}"' for n in sprites) + ', ""};')
for name, spec in scripts.get('raw', {}).items():            # "0xaddr+0xlen": copied verbatim
    addr, n = (int(v, 16) for v in spec.split('+'))
    out.append(f'const uint8_t kData_{name}[] = {{' + ', '.join(f'0x{b:02x}' for b in rd(addr, n)) + f'}};  // {spec}')
out += ['} // namespace game']
open(os.path.join(root, 'src', 'game', 'anim_scripts_data.cpp'), 'w').write('\n'.join(out) + '\n')
print(f'wrote {len(scripts)} scripts')
