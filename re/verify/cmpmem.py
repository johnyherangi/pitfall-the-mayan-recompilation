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
#                     level, the title), the ticks counted on as the engine's are
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ticks import le, load, finished, level_start

samples = load(sys.argv[1], 8)
lv = int(sys.argv[3])
show_all = '--all' in sys.argv[4:]
fin = finished(samples)
orig = {}
for gf, i in fin.items():
    r = samples[i]
    orig[gf] = (le(r[4]), bytes.fromhex(r[6]), bytes.fromhex(r[7]))
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
    port[gf - p_start] = (int(r[0]), plv, bytes.fromhex(r[5]), bytes.fromhex(r[6]) if len(r) > 6 else b'')
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
    compared += 1
    (olv, om, orr), (t, plv, pm, prr) = orig[g], port[k]
    diffs = []
    if olv != plv:
        diffs.append(('level', '%d / %d' % (olv, plv)))
        om = pm = orr = prr = b''
    md = [i for i in range(len(om)) if om[i] != pm[i] and (0x465800 + i) not in skip]
    groups = []
    for i in md:
        if groups and i - groups[-1][1] <= 2:
            groups[-1][1] = i
        else:
            groups.append([i, i])
    for a, b in groups:
        diffs.append(('mem %#x' % (0x465800 + a), om[a:b + 1].hex() + ' / ' + pm[a:b + 1].hex()))
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
