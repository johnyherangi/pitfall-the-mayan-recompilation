# mkpad.py <capture> <joypad capture> <level>: the original's joypad by engine tick, for memdump's PAD.
#
# The joypad capture is capmem.sh's JOYOUT (t, the engine's count, the logic's, the joypad), read
# without a pause.  A tick's joypad is the one seen with its logic's end (the logic count up: the
# poll came before it); for a tick without one (an engine exit, or the next tick began before the
# sampler saw the end) its last record's, if that is not 0: the game can stop between the count's
# step and the poll for tens of milliseconds, the keys changing meanwhile; but a 0 the last record
# within 5 ms of its first, as a later one, with the count unchanged through a load or a wait, is
# the engine's own (a load clears the joypad).  A tick the joypad sampler did not see takes the joypad of the main
# capture's sample of its end (0x4517f8 holds the tick's poll through the logic), and one neither saw
# (the load's and the restarts' ticks run back to back after a pause, a tenth of a millisecond each)
# the next seen one's: they ran after the pause, with the keys as they were then.  The first line is "start <the level's first tick>";
# PRETICKS=13 for levels 0, 2, 5, 8 and 16, whose load runs on that many ticks.  With a fourth
# column in the joypad capture (DAT_0046d054, the "vampire" cheat's flag), each change of it gives a
# line "type <tick> vampire": the cheat came in after that tick (WM_CHAR runs between two ticks); with
# a fifth (the lives, DAT_00466527), the lives turning 99 give "type <tick> meowmeowlikemeowman"; with
# a sixth (the engine's start key, DAT_0046fb88, which a key's WM_KEYDOWN sets between two ticks and the
# logic clears), each tick's line has a third number, the start key as the tick began (the last record
# before it), for the logic's own tests of it (the map, the scenes, the 2600 game's end, the title).
# TYPED=<codes>: other cheat codes drive.py typed after "vampire" (CHEATS), whose flags no sampler reads,
# given a "type" line at the same tick ("mijmrowhtrae": level 24 for the 2600 level).
# A scene's wait for a key (0x4016fd) that takes the start key clears the joypad too, after the tick's
# FUN_004263f3 read it: the samplers see a 0 there, and the tick's buttons are those the main capture's
# sample of its end shows pressed (DAT_0046676c .. 6f: 0x40, 0x10, 0x20, 0x80).
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ticks import le, load, level_start, finished

samples = load(sys.argv[1], 8)
lv = int(sys.argv[3])
o_start = level_start(samples, lv) + int(os.environ.get('PRETICKS', '0'))

recs = {}
typed, flag, lives = [], None, None
keyed = []                                              # (engine tick, start key) of the records, in order
for line in open(sys.argv[2]):
    r = line.split()
    if len(r) < 4:
        continue
    try:
        recs.setdefault(le(r[1]), []).append((int(r[0]), le(r[2]), le(r[3])))
        if len(r) >= 5 and len(r[4]) == 2:
            f = le(r[4])
            if flag is not None and f != flag:
                typed.append((le(r[1]), 'vampire'))
            flag = f
        if len(r) >= 6 and len(r[5]) == 2:
            n = le(r[5])
            if lives is not None and n == 0x63 and lives != 0x63:
                typed.append((le(r[1]), 'meowmeowlikemeowman'))
            lives = n
        if len(r) >= 7 and len(r[6]) == 2:
            keyed.append((le(r[1]), le(r[6])))
    except ValueError:
        continue                                        # (a line cut short at the end)
pad = {}
for g, rs in recs.items():
    t0, lt0, _ = rs[0]
    ended = [j for t, lt, j in rs if lt > lt0]
    if ended:
        pad[g] = ended[0]
    else:
        last = rs[-1][2]
        pad[g] = last if last != 0 else [j for t, lt, j in rs if t - t0 <= 5000][-1]

for code in os.environ.get('TYPED', '').split():
    typed.append((typed[0][0] if typed else o_start, code))
print('start', o_start)
for g, code in typed:
    print('type', g, code)
main = {g: le(samples[i][8]) for g, i in finished(samples).items()}
key, j, cur = {}, 0, 0
for g in range(o_start, max(pad) + 1):                  # the start key before each tick
    while j < len(keyed) and keyed[j][0] < g:
        cur = keyed[j][1]
        j += 1
    key[g] = cur
fin = finished(samples)
for g in list(pad):
    if pad[g] == 0 and key.get(g) == 1 and key.get(g + 1) == 0 and g in fin:
        m = bytes.fromhex(samples[fin[g]][6])
        e = [m[a - 0x465800] for a in (0x46676c, 0x46676d, 0x46676e, 0x46676f)]
        pad[g] = (0x40 if e[0] else 0) | (0x10 if e[1] else 0) | (0x20 if e[2] else 0) | (0x80 if e[3] else 0)
known = sorted(pad)
k = 0
for g in range(o_start, known[-1] + 1):
    while known[k] < g:
        k += 1
    j = pad[g] if g in pad else main[g] if g in main else pad[known[k]]
    print(g, j, key[g]) if keyed else print(g, j)
