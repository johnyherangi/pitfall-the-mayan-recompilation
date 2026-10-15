#!/usr/bin/env python3
"""Annotated disassembly of a PITFALL.EXE address range (objdump + symbol notes).

usage: pc_dis.py <start> <end|+len>      (needs objdump; PITFALL_EXE overrides original/PITFALL.EXE)
The Genesis-translated code keeps the 68000 registers in memory:
  d0..d7 at 0x460180 + 4*n, a0..a7 at 0x4601a0 + 4*n (ax.w low halves used as .w)
"""
import re, subprocess, sys
import signal
if hasattr(signal, 'SIGPIPE'): signal.signal(signal.SIGPIPE, signal.SIG_DFL)   # quiet when piped into head
import os
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
EXE_PATH = os.environ.get('PITFALL_EXE', os.path.join(ROOT, 'original', 'PITFALL.EXE'))
EXE = EXE_PATH
if not os.path.exists(EXE): sys.exit(f'PITFALL.EXE not found at {EXE} (set PITFALL_EXE)')
TEXT_VA, TEXT_RAW = 0x401000, 0x400

names = {}
for line in open(os.path.join(HERE, 'index.tsv')):
    p = line.split('\t')
    names[int(p[0], 16)] = p[1]

REG = {}
for i in range(8):
    REG[0x460180 + 4 * i] = f'd{i}'
    REG[0x4601a0 + 4 * i] = f'a{i}'
KNOWN = {
    0x470000: 'spr:fxifloor',
    0x470004: 'spr:fxishado',
    0x470008: 'spr:fxisplsh',
    0x47000c: 'spr:fxitrch',
    0x470010: 'spr:fxivortx',
    0x470018: 'spr:fxrbmrng',
    0x47001c: 'spr:fxrbmrot',
    0x470020: 'spr:fxrdust2',
    0x470024: 'spr:fxrfirep',
    0x470028: 'spr:fxzdeath',
    0x47002c: 'spr:fxzexpl2',
    0x470030: 'spr:fxzfireb',
    0x470034: 'spr:fxzfirej',
    0x470038: 'spr:fxzleaf2',
    0x47003c: 'spr:fxzpots1',
    0x470040: 'spr:fxzpots2',
    0x470044: 'spr:fxzpots3',
    0x470048: 'spr:fxzspark',
    0x470054: 'spr:fx_deat2',
    0x470058: 'spr:fx_drip1',
    0x47005c: 'spr:fx_expl3',
    0x470060: 'spr:fx_hrtbt',
    0x470064: 'spr:fx_leaf1',
    0x470068: 'spr:fx_leaf3',
    0x470070: 'spr:fx_logo',
    0x470074: 'spr:fx_mls',
    0x470078: 'spr:fx_rippl',
    0x47007c: 'spr:fx_sprk1',
    0x470080: 'spr:fx_sprk2',
    0x470084: 'spr:fx_stone',
    0x470088: 'spr:gargrok',
    0x47008c: 'spr:gvrvapor',
    0x470090: 'spr:hkidive',
    0x470094: 'spr:hkifly',
    0x470098: 'spr:hkiglide',
    0x47009c: 'spr:hkistun',
    0x4700a0: 'spr:hkiturn',
    0x4700a4: 'spr:hyibflip',
    0x4700a8: 'spr:hyibland',
    0x4700ac: 'spr:hyiblrec',
    0x4700b0: 'spr:hyibmrng',
    0x4700b4: 'spr:hyibng3b',
    0x4700b8: 'spr:hyibngab',
    0x4700bc: 'spr:hyibngac',
    0x4700c0: 'spr:hyibngbc',
    0x4700c4: 'spr:hyibngrv',
    0x4700c8: 'spr:hyibung1',
    0x4700cc: 'spr:hyibung2',
    0x4700d0: 'spr:hyibung3',
    0x4700d4: 'spr:hyicl2rn',
    0x4700d8: 'spr:hyicr2cl',
    0x4700dc: 'spr:hyicrcyc',
    0x4700e0: 'spr:hyicrtrn',
    0x4700e4: 'spr:hyicrwhp',
    0x4700e8: 'spr:hyidazed',
    0x4700ec: 'spr:hyidie',
    0x4700f0: 'spr:hyiding',
    0x4700f4: 'spr:hyidrope',
    0x4700f8: 'spr:hyidvine',
    0x4700fc: 'spr:hyidzip',
    0x470100: 'spr:hyiemrg',
    0x470104: 'spr:hyienter',
    0x470108: 'spr:hyifall',
    0x47010c: 'spr:hyifcrwl',
    0x470110: 'spr:hyigland',
    0x470114: 'spr:hyihang',
    0x470118: 'spr:hyihjump',
    0x47011c: 'spr:hyihndad',
    0x470120: 'spr:hyihshot',
    0x470124: 'spr:hyihwall',
    0x470128: 'spr:hyihwhip',
    0x47012c: 'spr:hyijp2sn',
    0x470130: 'spr:hyilean',
    0x470134: 'spr:hyilooka',
    0x470138: 'spr:hyilsbal',
    0x47013c: 'spr:hyimatch',
    0x470140: 'spr:hyimrop2',
    0x470144: 'spr:hyimzip',
    0x470148: 'spr:hyininja',
    0x47014c: 'spr:hyipee',
    0x470150: 'spr:hyipsbal',
    0x470154: 'spr:hyipull',
    0x470158: 'spr:hyipulup',
    0x47015c: 'spr:hyipush',
    0x470160: 'spr:hyird2rn',
    0x470164: 'spr:hyiready',
    0x470168: 'spr:hyirn2cl',
    0x47016c: 'spr:hyirn2sn',
    0x470170: 'spr:hyiroped',
    0x470174: 'spr:hyirsht1',
    0x470178: 'spr:hyirtrn2',
    0x47017c: 'spr:hyirun',
    0x470180: 'spr:hyirun2',
    0x470184: 'spr:hyirwhip',
    0x470188: 'spr:hyirwhpr',
    0x47018c: 'spr:hyishoot',
    0x470190: 'spr:hyisink',
    0x470194: 'spr:hyiskte1',
    0x470198: 'spr:hyiskte2',
    0x47019c: 'spr:hyiskte3',
    0x4701a0: 'spr:hyiskte4',
    0x4701a4: 'spr:hyislice',
    0x4701a8: 'spr:hyisomr1',
    0x4701ac: 'spr:hyisomr2',
    0x4701b0: 'spr:hyisomr3',
    0x4701b4: 'spr:hyispin',
    0x4701b8: 'spr:hyisprng',
    0x4701bc: 'spr:hyisspi2',
    0x4701c0: 'spr:hyisswng',
    0x4701c4: 'spr:hyist2cr',
    0x4701c8: 'spr:hyist2pu',
    0x4701cc: 'spr:hyisvine',
    0x4701d0: 'spr:hyitrip1',
    0x4701d4: 'spr:hyitwirl',
    0x4701d8: 'spr:hyiurope',
    0x4701dc: 'spr:hyivgrab',
    0x4701e0: 'spr:hyivjump',
    0x4701e4: 'spr:hyivwhip',
    0x4701e8: 'spr:hyiwhip',
    0x4701ec: 'spr:hyiwhiph',
    0x4701f0: 'spr:hyizen',
    0x470200: 'spr:jgumiss',
    0x470204: 'spr:jgurevrs',
    0x470208: 'spr:jgurn2lo',
    0x470214: 'spr:jgustill',
    0x470218: 'spr:jgustone',
    0x47021c: 'spr:jm0swipe',
    0x470220: 'spr:jm1swipe',
    0x470224: 'spr:jmzmorf2',
    0x470228: 'spr:jmzmorfa',
    0x47022c: 'spr:jmzmorfb',
    0x470230: 'spr:jmzready',
    0x470238: 'spr:ltr_lg',
    0x47023c: 'spr:ltr_md',
    0x470240: 'spr:ltr_sm',
    0x470244: 'spr:mirbmrng',
    0x470248: 'spr:mi_1up',
    0x47024c: 'spr:mi_chili',
    0x470250: 'spr:mi_coin',
    0x470254: 'spr:mi_croc',
    0x47025c: 'spr:mi_go',
    0x470260: 'spr:mi_gold',
    0x470264: 'spr:mi_life',
    0x470268: 'spr:mi_pouc2',
    0x47026c: 'spr:mi_prcnt',
    0x470270: 'spr:mi_ptfal',
    0x470274: 'spr:mi_ring',
    0x470278: 'spr:mi_sign',
    0x47027c: 'spr:mi_silvr',
    0x470280: 'spr:mi_stonb',
    0x470284: 'spr:mi_stone',
    0x470288: 'spr:mi_time',
    0x47028c: 'spr:mkzbounc',
    0x470290: 'spr:mkzhvine',
    0x470294: 'spr:mkzrn2st',
    0x470298: 'spr:mkzrun',
    0x47029c: 'spr:mkztoss',
    0x4702a0: 'spr:mkzturn',
    0x4702a4: 'spr:obfbston',
    0x4702a8: 'spr:obfleve2',
    0x4702ac: 'spr:obftung',
    0x4702b0: 'spr:obiaweb',
    0x4702b4: 'spr:obichain',
    0x4702b8: 'spr:obihweb',
    0x4702bc: 'spr:obiturtl',
    0x4702c0: 'spr:obivweb',
    0x4702c4: 'spr:obrbrnch',
    0x4702c8: 'spr:obrcactu',
    0x4702cc: 'spr:obrgarg',
    0x4702d0: 'spr:obrpit1',
    0x4702d4: 'spr:obrston0',
    0x4702d8: 'spr:obrwhee1',
    0x4702dc: 'spr:obrwhee2',
    0x4702e0: 'spr:obzaxe',
    0x4702e4: 'spr:obzfrock',
    0x4702e8: 'spr:obzleve3',
    0x4702ec: 'spr:obzlever',
    0x4702f0: 'spr:obzpit2a',
    0x4702f4: 'spr:obzpit2b',
    0x4702f8: 'spr:obzsball',
    0x4702fc: 'spr:obzspear',
    0x470300: 'spr:obztnt',
    0x470304: 'spr:ob_bunge',
    0x470308: 'spr:ob_bungs',
    0x47030c: 'spr:ob_ceil',
    0x470310: 'spr:ob_chute',
    0x470314: 'spr:ob_door',
    0x470318: 'spr:ob_door3',
    0x47031c: 'spr:ob_float',
    0x470320: 'spr:ob_lite',
    0x470324: 'spr:ob_liter',
    0x470328: 'spr:ob_mcar',
    0x47032c: 'spr:ob_pouch',
    0x470334: 'spr:ob_scar',
    0x470338: 'spr:ob_sling',
    0x47033c: 'spr:ob_spik1',
    0x470340: 'spr:ob_spik2',
    0x470344: 'spr:ob_whelz',
    0x47034c: 'spr:plzexpl',
    0x470350: 'spr:plzspik',
    0x470354: 'spr:ropes',
    0x470358: 'spr:rtz1run',
    0x47035c: 'spr:rtzfall',
    0x470360: 'spr:rtzrun',
    0x470364: 'spr:rtzturn',
    0x470368: 'spr:rtzup',
    0x470380: 'spr:scffish',
    0x470384: 'spr:scorp',
    0x470388: 'spr:sc_eyes1',
    0x47038c: 'spr:sc_eyes2',
    0x470390: 'spr:sk0chop',
    0x470394: 'spr:skichop',
    0x470398: 'spr:skidie',
    0x47039c: 'spr:skijaw',
    0x4703a0: 'spr:skimorph',
    0x4703a4: 'spr:skiready',
    0x4703a8: 'spr:skirib',
    0x4703ac: 'spr:skiroll',
    0x4703b0: 'spr:skithrow',
    0x4703b4: 'spr:skiturn',
    0x4703b8: 'spr:skiwalk',
    0x4703bc: 'spr:skzantic',
    0x4703c4: 'spr:skzbone',
    0x4703c8: 'spr:snilunge',
    0x4703cc: 'spr:snimiss',
    0x4703d0: 'spr:snislthr',
    0x4703d8: 'spr:sniturn',
    0x4703dc: 'spr:snixhole',
    0x4703e0: 'spr:snix_dot',
    0x4703e4: 'spr:snuhit',
    0x4703e8: 'spr:sn_dot',
    0x4703ec: 'spr:sn_hole',
    0x4703f0: 'spr:sp_jump',
    0x4703f4: 'spr:sp_walk',
    0x4703f8: 'spr:stifly',
    0x4703fc: 'spr:tgfarrow',
    0x470400: 'spr:tgrharro',
    0x470404: 'spr:tgrlarro',
    0x470408: 'spr:tgrpeel',
    0x47040c: 'spr:tgrturn',
    0x470410: 'spr:tgrwalk',
    0x470414: 'spr:tprapea2',
    0x470418: 'spr:tprhigh',
    0x47041c: 'spr:tpzhit',
    0x470420: 'spr:tszbody1',
    0x470424: 'spr:tszhang',
    0x470428: 'spr:tszlimp',
    0x47042c: 'spr:tszspit',
    0x470430: 'spr:tszup',
    0x470434: 'spr:whiroll',
    0x470438: 'spr:whirun',
    0x47043c: 'spr:whitoss',
    0x470440: 'spr:Pitfall',
    0x470448: 'spr:bosss',
    0x47044c: 'spr:als2600',
    0x470450: 'spr:chrs2600',
    0x470454: 'spr:scoline',
    0x47045c: 'spr:hatman',
    0x470460: 'spr:hy_sr_g',
    0x470464: 'spr:hurry',
    0x470468: 'spr:ew_ready',
    0x472724: 'spr:R6008_-_not_enough_space_for_arg',
    0x47272c: 'spr:R6009_-_not_enough_space_for_env',

    0x4601b8: 'a6(harry?)', 0x462848: 'objListHead', 0x465b0f: 'harry.x', 0x465b13: 'harry.y',
    0x465b19: 'harry.damage', 0x465ba4: 'harry.vx', 0x465ba8: 'harry.vy', 0x4517f8: 'joypad',
    0x45190c: 'levelTick', 0x46fb74: 'globalTick', 0x465b1a: 'harry.facing', 0x465b70: 'harry.onGround',
    0x4658ac: 'levelColumns', 0x465bae: 'levelW', 0x465bb0: 'levelH', 0x450504: 'scrollX', 0x450508: 'scrollY',
    0x46653b: 'score', 0x466526: 'treasures', 0x465ae3: 'lives', 0x46652d: 'invuln', 0x465b77: 'hurtTimer',
    0x4601c0: 'ccr/flags?', 0x462350: 'handlerTable', 0x4678c4: 'templates', 0x4668c4: 'collHeights',
    0x465b69: 'harryCont', 0x46b1a4: 'engineExit', 0x46b1a8: 'exitCmd', 0x4658b4: 'camX', 0x4658b6: 'camY', 0x46652f: 'harryHurtFlag?', 0x4627c1: 'scriptEvent',
}

def sym(addr):
    if addr in names: return names[addr]
    if addr in REG: return REG[addr]
    if addr in KNOWN: return KNOWN[addr]
    for base in (0x460180, 0x4601a0):
        for i in range(8):
            r = base + 4 * i
            if r < addr < r + 4:
                return f'{REG[r]}+{addr - r}'
    return None

def main():
    start = int(sys.argv[1], 16)
    e = sys.argv[2]
    end = start + int(e[1:], 0) if e.startswith('+') else int(e, 16)
    out = subprocess.run(['objdump', '-D', '-b', 'binary', '-m', 'i386', '-M', 'intel',
                          f'--adjust-vma={TEXT_VA - TEXT_RAW:#x}', f'--start-address={start:#x}',
                          f'--stop-address={end:#x}', EXE], capture_output=True, text=True).stdout
    for line in out.splitlines():
        m = re.match(r'\s*([0-9a-f]+):\t([0-9a-f ]+)\t(.*)', line)
        if not m: continue
        addr, _, ins = m.groups()
        notes = []
        for hx in re.findall(r'0x([0-9a-f]{6})', ins):
            s = sym(int(hx, 16))
            if s: notes.append(f'{hx}={s}')
        ins = re.sub(r'\s+', ' ', ins)
        def regsub(m):
            size, hx = m.group(1), int(m.group(2), 16)
            s2 = sym(hx)
            if s2 and (hx in REG or hx in KNOWN or '+' in (s2 or '')):
                suf = {'BYTE': '.b', 'WORD': '.w', 'DWORD': '.l'}[size]
                return s2 + suf
            return m.group(0)
        ins = re.sub(r'(BYTE|WORD|DWORD) PTR ds:0x([0-9a-f]{6})', regsub, ins)
        # moffs loads / stores: a named variable, or [0x...] so they cannot be mistaken for immediates
        ins = re.sub(r'\b(ax|eax|al),ds:0x([0-9a-f]{6})', lambda m: f'{m.group(1)},{sym(int(m.group(2),16)) or "[0x"+m.group(2)+"]"}', ins)
        ins = re.sub(r'ds:0x([0-9a-f]{6}),(ax|eax|al)', lambda m: f'{sym(int(m.group(1),16)) or "[0x"+m.group(1)+"]"},{m.group(2)}', ins)
        notes = [n for n in notes if not n.split('=')[1].startswith(('d', 'a')) or n.split('=')[1] in names.values()]
        if ins.startswith('mov edi,a6.l'): ins = 'edi = a6 (obj)'
        print(f'{addr}: {ins:<44} {"; " + " ".join(notes) if notes else ""}')

main()
