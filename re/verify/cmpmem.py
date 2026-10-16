# cmpmem.py <capture> <memdump output> <level> [--all]: the original's logic memory (0x465800 ..
# 0x4668ff) and its 80 object records, every byte of each (the free ones too), against the port's,
# tick by tick from the level's start (each tick's finished state, ticks.py; a tick without one is
# left out).  Prints each difference's first and last tick, its count and its first values
# (original / port); --all prints every differing tick.
#
#   PRETICKS=13       levels 0, 2, 5, 8 and 16 (their load runs on 13 ticks)
#   SKIPMEM=a:n,...   memory left out (hex address, byte count)
#   RECSKIP=t:o:n,... record bytes left out by type (hex type, offset, count)
#   PTRSKIP=1         leave out a record's 4-byte fields that hold an address of the executable or
#                     of the load block in the original (to look past pointers while a port of
#                     them is under way)
#   FOLLOW=1          go on past the level's end, through the levels after it (the map, the next
#                     level, the title), the ticks counted on as the engine's are; the tick of a load
#                     may be sampled with the new level set before the load's own engine tick
#                     (the title's: no logic tick tells it apart), where the port's line shows
#                     the old one until that tick: such a tick is left out
#
# A capture with the cutscenes' data (WATCH's 456000:2528, memdump's column after the records)
# has it compared too ('cut' differences), and one with the sound channels' state (4627be:5, the
# capmem.sh default's last range, memdump's last column: channel 0's priority and sound, channel
# 1's, channel 0's hold) that ('snd').  Its frame's copy of the actors (0x4564bf .. 0x45673e) and
# the record of the last one drawn (0x45673f .. 0x456752) are the frame's (FUN_00443c90), which the
# original's main loop composes when it can: on a tick after which it composed none (its copy not
# its actors), they are left out.
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ticks import le, load, finished, level_start

samples = load(sys.argv[1], 8)
lv = int(sys.argv[3])
show_all = '--all' in sys.argv[4:]
fin = finished(samples)
CUT, SND = 0x9e0, 5                                             # (the optional ranges by their sizes)


def extra(cols, n):
    """the optional range of n bytes among a line's last columns (b'' without one)"""
    for c in cols:
        if len(c) == 2 * n:
            return bytes.fromhex(c)
    return b''


orig = {}
for gf, i in fin.items():
    r = samples[i]
    orig[gf] = (le(r[4]), bytes.fromhex(r[6]), bytes.fromhex(r[7]), extra(r[9:], CUT), extra(r[9:], SND))
o_start = level_start(samples, lv) + int(os.environ.get('PRETICKS', '0'))

follow = bool(os.environ.get('FOLLOW'))
port = {}
p_start = None
for line in open(sys.argv[2]):
    r = line.split()
    plv = int(r[3])
    if plv != lv and (p_start is None or not follow):
        continue
    gf = int(r[1])
    if p_start is None:
        p_start = gf
    port[gf - p_start] = (int(r[0]), plv, bytes.fromhex(r[5]), bytes.fromhex(r[6]) if len(r) > 6 else b'',
                          extra(r[7:], CUT), extra(r[7:], SND))
print('original level start: tick', o_start, '- port:', p_start)

skip = set()
for spec in os.environ.get('SKIPMEM', '').split(','):
    if spec:
        a, n = spec.split(':')
        skip |= set(range(int(a, 16), int(a, 16) + int(n)))
recskip = {}
for spec in os.environ.get('RECSKIP', '').split(','):
    if spec:
        t, o, n = spec.split(':')
        recskip.setdefault(int(t, 16), set()).update(range(int(o, 16), int(o, 16) + int(n)))
ptrskip = bool(os.environ.get('PTRSKIP'))

def is_ptr(rec, j):
    if j < 0x30 or j + 4 > 0x6e:
        return False
    v = int.from_bytes(rec[j:j + 4], 'little')
    return 0x401000 <= v < 0x480000 or 0x016a0000 <= v < 0x01820020

summary = {}
compared = 0
for k in sorted(port):
    g = o_start + k
    if g not in orig:
        continue
    (olv, om, orr, oc, osd), (t, plv, pm, prr, pc, psd) = orig[g], port[k]
    if olv != plv and follow and port.get(k + 1, (0, None))[1] == olv and orig.get(g - 1, (None,))[0] == plv:
        continue                                                # (the load's tick, sampled before its engine tick)
    compared += 1
    diffs = []
    if olv != plv:
        diffs.append(('level', '%d / %d' % (olv, plv)))
        om = pm = orr = prr = oc = pc = osd = psd = b''
    md = [i for i in range(len(om)) if om[i] != pm[i] and (0x465800 + i) not in skip]
    groups = []
    for i in md:
        if groups and i - groups[-1][1] <= 2:
            groups[-1][1] = i
        else:
            groups.append([i, i])
    for a, b in groups:
        diffs.append(('mem %#x' % (0x465800 + a), om[a:b + 1].hex() + ' / ' + pm[a:b + 1].hex()))
    cgroups = []
    stale = oc[0x4bf:0x73f] != oc[0x23f:0x4bf]                # (no frame composed after this tick)
    for i in (range(len(oc)) if oc and pc else ()):
        if stale and 0x4bf <= i < 0x753:
            continue
        if oc[i] != pc[i]:
            if cgroups and i - cgroups[-1][1] <= 2:
                cgroups[-1][1] = i
            else:
                cgroups.append([i, i])
    for a, b in cgroups:
        diffs.append(('cut %#x' % (0x456000 + a), oc[a:b + 1].hex() + ' / ' + pc[a:b + 1].hex()))
    if osd and psd and osd != psd:
        diffs.append(('snd', osd.hex() + ' / ' + psd.hex()))
    for n in range(0x50 if orr and prr else 0):
        ob, pb = orr[n * 0x6e:(n + 1) * 0x6e], prr[n * 0x6e:(n + 1) * 0x6e]
        rs = recskip.get(ob[8], set()) | recskip.get(pb[8], set())
        idx = [i for i in range(0x6e) if ob[i] != pb[i] and i not in rs]
        if ptrskip:
            idx = [i for i in idx if not any(is_ptr(ob, j) for j in (i - (i & 1) - 2, i - (i & 1)))]
        if idx:
            diffs.append(('record %d type %d/%d' % (n, ob[8], pb[8]),
                          ' '.join('+%x:%02x/%02x' % (i, ob[i], pb[i]) for i in idx[:12])))
    if show_all and diffs:
        print('tick %d (original %d, port step %d)' % (k, g, t))
        for name, v in diffs:
            print('   ', name, v)
    for name, v in diffs:
        s = summary.setdefault(name, [k, k, 0, v])
        s[1] = k
        s[2] += 1
print('%d ticks compared' % compared)
for name, (k0, k1, n, v) in sorted(summary.items(), key=lambda x: x[1][0]):
    print('%-28s ticks %d .. %d (%d): %s' % (name, k0, k1, n, v[:90]))
print('%d differences' % len(summary))
