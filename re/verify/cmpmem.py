# cmpmem.py <capture> <memdump output> <level> [--all]: the original's logic memory (0x465800 ..
# 0x4668ff) and object records against the port's, tick by tick from the level's start (each tick's
# finished state, ticks.py; a tick without one is left out).  Prints each difference's first and
# last tick, its count and its first values (original / port); --all prints every differing tick.
#
#   PRETICKS=13       levels 0, 2, 5, 8 and 16 (their load runs on 13 ticks)
#   SKIPMEM=a:n,...   memory left out (hex address, byte count); the default is Harry's sprite,
#                     0x465b1d:4, which the port keeps as the slot's address (the original has the
#                     sprite's own in the load block)
#   RECSKIP=t:o:n,... record bytes left out by type (hex type, offset, count)
#   NOPTRSKIP=1       compare the records' pointer fields too: by default a 4-byte field holding an
#                     address of the executable or of the load block is left out where the port
#                     keeps a reference, an index or nothing
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
    orig[gf] = (bytes.fromhex(r[6]), bytes.fromhex(r[7]))
o_start = level_start(samples, lv) + int(os.environ.get('PRETICKS', '0'))

port = {}
p_start = None
for line in open(sys.argv[2]):
    r = line.split()
    if int(r[3]) != lv:
        continue
    gf = int(r[1])
    if p_start is None:
        p_start = gf
    port[gf - p_start] = (int(r[0]), bytes.fromhex(r[5]), bytes.fromhex(r[6]))
print('original level start: tick', o_start, '- port:', p_start)

skip = set()
for spec in os.environ.get('SKIPMEM', '465b1d:4').split(','):
    if spec:
        a, n = spec.split(':')
        skip |= set(range(int(a, 16), int(a, 16) + int(n)))
recskip = {}
for spec in os.environ.get('RECSKIP', '').split(','):
    if spec:
        t, o, n = spec.split(':')
        recskip.setdefault(int(t, 16), set()).update(range(int(o, 16), int(o, 16) + int(n)))
ptrskip = not os.environ.get('NOPTRSKIP')
FIELDS = list(range(0, 0xe)) + list(range(0x1c, 0x21)) + [0x28, 0x29, 0x2d, 0x2e] + list(range(0x30, 0x6e))

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
    (om, orr), (t, pm, prr) = orig[g], port[k]
    diffs = []
    md = [i for i in range(len(om)) if om[i] != pm[i] and (0x465800 + i) not in skip]
    groups = []
    for i in md:
        if groups and i - groups[-1][1] <= 2:
            groups[-1][1] = i
        else:
            groups.append([i, i])
    for a, b in groups:
        diffs.append(('mem %#x' % (0x465800 + a), om[a:b + 1].hex() + ' / ' + pm[a:b + 1].hex()))
    for n in range(0x50):
        ob, pb = orr[n * 0x6e:(n + 1) * 0x6e], prr[n * 0x6e:(n + 1) * 0x6e]
        if ob[8] == 0 and pb[8] == 0:
            continue
        rs = recskip.get(ob[8], set()) | recskip.get(pb[8], set())
        idx = [i for i in FIELDS if ob[i] != pb[i] and i not in rs]
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
