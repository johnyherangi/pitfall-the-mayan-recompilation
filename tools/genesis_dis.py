#!/usr/bin/env python3
"""Annotated 68000 disassembly of the Genesis ROM, cross-referenced to PITFALL.EXE.

usage: genesis_dis.py <type>            disassemble the object handler for .als behaviour type
       genesis_dis.py <start> <end|+len> disassemble a ROM range (hex)
       genesis_dis.py --map              rebuild re/genesis_xref.json (sprite pointers, calls, RAM)

Needs original/PITFALL.EXE, original_genesis/<rom>.bin (not part of the repository), re/index.tsv
and python3 -m pip install capstone.

The Windows build is a mechanical translation of this code, so the sequence of sprite
pointers, subroutine calls and RAM globals inside a handler is the same in both; aligning
the two sequences names the ROM's sprite pointers (-> .PH sprite names), its subroutines
(-> FUN_/named PC routines) and its RAM (-> PC globals).  ROM handler table: 0x10621e;
PC handler table: 0x462350.
"""
import glob, json, os, re, struct, subprocess, sys
import signal
if hasattr(signal, 'SIGPIPE'): signal.signal(signal.SIGPIPE, signal.SIG_DFL)   # quiet when piped into head

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path = [p for p in sys.path if os.path.abspath(p or '.') not in (ROOT, os.path.join(ROOT, 're'))]
import capstone   # python3 -m pip install capstone

# Inputs (override with the environment): the PC executable, the Genesis ROM (not part of the
# repository) and the optional Ghidra symbol list (address<TAB>name) for the PC build.
EXE_PATH = os.environ.get('PITFALL_EXE', os.path.join(ROOT, 'original', 'PITFALL.EXE'))
ROM_PATH = os.environ.get('GENESIS_ROM') or (glob.glob(os.path.join(ROOT, 'original_genesis', '*.bin')) + [''])[0]
INDEX_PATH = os.environ.get('PITFALL_INDEX', os.path.join(ROOT, 're', 'index.tsv'))
XREF = os.environ.get('GENESIS_XREF', os.path.join(ROOT, 're', 'genesis_xref.json'))
ROM = open(ROM_PATH, 'rb').read()
EXE = open(EXE_PATH, 'rb').read()
ROM_TABLE, PC_TABLE = 0x10621e, 0x462350

names = {}
if os.path.exists(INDEX_PATH):
    for line in open(INDEX_PATH):
        p = line.split('\t')
        names[int(p[0], 16)] = p[1]
slots = json.load(open(os.path.join(ROOT, 'tools', 'sprite_slots.json')))
slotname = {int(k, 16): v for k, v in slots.items()}


def pc_off(va):
    return va - 0x401000 + 0x400 if va < 0x456000 else va - 0x456000 + 0x4ec00


rom_tab = [struct.unpack_from('>I', ROM, ROM_TABLE + 4 * t)[0] for t in range(256)]
pc_tab = [struct.unpack_from('<I', EXE, pc_off(PC_TABLE + 4 * t))[0] for t in range(256)]
rom_starts = sorted(set(v for v in rom_tab if 0x200 <= v < 0x180000))
pc_starts = sorted(set(v for v in pc_tab if 0x401000 <= v < 0x438000))


def handler_range(t):
    r, p = rom_tab[t], pc_tab[t]
    ri, pi = rom_starts.index(r), pc_starts.index(p)
    rend = rom_starts[ri + 1] if ri + 1 < len(rom_starts) else r + 0x1000
    pend = pc_starts[pi + 1] if pi + 1 < len(pc_starts) else p + 0x1000
    return (r, rend), (p, pend)


md = capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_M68K_000)


def rom_disasm(start, end):
    return list(md.disasm(ROM[start:end], start))


def pc_disasm(start, end):
    out = subprocess.run(['objdump', '-D', '-b', 'binary', '-m', 'i386', '-M', 'intel',
                          '--adjust-vma=0x400c00', f'--start-address={start:#x}', f'--stop-address={end:#x}',
                          EXE_PATH], capture_output=True, text=True).stdout
    ins = []
    for line in out.splitlines():
        m = re.match(r'\s*([0-9a-f]+):\t([0-9a-f ]+)\t(.*)', line)
        if m: ins.append((int(m.group(1), 16), re.sub(r'\s+', ' ', m.group(3))))
    return ins


def rom_sequences(ins):
    """sprite pointer immediates, call/jump targets and RAM addresses, in order."""
    sprites, calls, ram = [], [], []
    for i in ins:
        for m in re.finditer(r'#\$([0-9a-f]+)', i.op_str):
            v = int(m.group(1), 16)
            # sprite pointers: stored into / compared with an object's +0xe or Harry's sprite
            # (ff34be), or loaded into a data register for the effect spawners
            to_sprite = '$e(a' in i.op_str or 'ff34be' in i.op_str
            if (0x1000 <= v < 0x100000 and to_sprite) or (0x60000 <= v < 0x80000 and not to_sprite):
                sprites.append(v)
        if i.mnemonic in ('jsr', 'jmp', 'bsr.w', 'bsr.b'):
            m = re.match(r'\$([0-9a-f]+)', i.op_str)
            if m: calls.append(int(m.group(1), 16))
        for m in re.finditer(r'\$(ff[0-9a-f]{4})\.l', i.op_str):
            ram.append(int(m.group(1), 16))
    return sprites, calls, ram


def pc_sequences(ins):
    sprites, calls, ram = [], [], []
    for _, s in ins:
        for m in re.finditer(r'ds:0x(4[67][0-9a-f]{4})', s):
            v = int(m.group(1), 16)
            if v in slotname: sprites.append(slotname[v])
        if s.startswith(('call', 'jmp')):
            m = re.match(r'(call|jmp) 0x([0-9a-f]+)', s)
            if m and int(m.group(2), 16) != 0x405313: calls.append(int(m.group(2), 16))
        for m in re.finditer(r'0x(46[0-9a-f]{4})', s):
            v = int(m.group(1), 16)
            if v not in slotname and not (0x460180 <= v < 0x4601c0): ram.append(v)
    return sprites, calls, ram


def build_map():
    xref = {'sprite': {}, 'call': {}, 'ram': {}}
    for t in range(256):
        if not (0x200 <= rom_tab[t] < 0x180000) or not (0x401000 <= pc_tab[t] < 0x438000): continue
        (rs, re_), (ps, pe) = handler_range(t)
        rsq = rom_sequences(rom_disasm(rs, re_))
        psq = pc_sequences(pc_disasm(ps, pe))
        for kind, a, b in zip(('sprite', 'call', 'ram'), rsq, psq):
            def consistent(a, b):
                fw, bw = {}, {}
                return all(fw.setdefault(x, y) == y and bw.setdefault(y, x) == x for x, y in zip(a, b))
            if len(a) != len(b) or not a or not consistent(a, b):
                # Different counts (the translation sometimes repeats or drops a load): fall
                # back to the distinct values in order of first use, if those line up.
                a = list(dict.fromkeys(a)); b = list(dict.fromkeys(b))
                if len(a) != len(b) or not a or not consistent(a, b): continue
                weight = 1
            else:
                weight = 2
            for x, y in zip(a, b):
                xref[kind].setdefault(str(x), {})
                xref[kind][str(x)][str(y)] = xref[kind][str(x)].get(str(y), 0) + weight
    # keep the majority vote
    final = {}
    for kind in xref:
        final[kind] = {k: max(v.items(), key=lambda kv: kv[1])[0] for k, v in xref[kind].items()}
    os.makedirs(os.path.dirname(XREF), exist_ok=True)
    json.dump(final, open(XREF, 'w'), indent=0)
    print({k: len(v) for k, v in final.items()})


def load_map():
    if not os.path.exists(XREF): build_map()
    x = json.load(open(XREF))
    return ({int(k): v for k, v in x['sprite'].items()}, {int(k): int(v) for k, v in x['call'].items()},
            {int(k): int(v) for k, v in x['ram'].items()})


def pc_name(va):
    n = names.get(va)
    return n or f'0x{va:x}'


def annotate(start, end):
    sprite, call, ram = load_map()
    for i in rom_disasm(start, end):
        notes = []
        for m in re.finditer(r'#\$([0-9a-f]+)', i.op_str):
            v = int(m.group(1), 16)
            if v in sprite: notes.append(f'spr:{sprite[v]}')
        if i.mnemonic in ('jsr', 'jmp', 'bsr.w', 'bsr.b'):
            m = re.match(r'\$([0-9a-f]+)', i.op_str)
            if m:
                v = int(m.group(1), 16)
                if v in call: notes.append(f'-> PC {pc_name(call[v])}')
        for m in re.finditer(r'\$(ff[0-9a-f]{4})\.l', i.op_str):
            v = int(m.group(1), 16)
            if v in ram: notes.append(f'{m.group(1)}={pc_name(ram[v])}')
        print(f'{i.address:06x}: {i.mnemonic:<8} {i.op_str:<36} {"; " + " ".join(notes) if notes else ""}')


def main():
    if sys.argv[1] == '--map':
        build_map(); return
    if len(sys.argv) == 2:
        t = int(sys.argv[1], 0)
        (rs, re_), (ps, pe) = handler_range(t)
        print(f'; type {t}: ROM {rs:#x}..{re_:#x} ({re_ - rs} bytes)  PC {ps:#x}..{pe:#x} ({pe - ps} bytes)')
        annotate(rs, re_); return
    start = int(sys.argv[1], 16)
    e = sys.argv[2]
    end = start + int(e[1:], 0) if e.startswith('+') else int(e, 16)
    annotate(start, end)


main()
