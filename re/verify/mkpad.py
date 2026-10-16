# mkpad.py <capture> <joypad capture> <level>: the original's joypad by engine tick, for memdump's PAD.
#
# The joypad capture is capmem.sh's JOYOUT (t, the engine's count, the logic's, the joypad), read
# without a pause.  A tick's joypad is the one seen with its logic's end (the logic count up: the
# poll came before it); for a tick without one (an engine exit) the last record within 5 ms of its
# first, as a later one, with the count unchanged through a load or a wait, is the engine's own (a
# load clears the joypad).  A tick not seen at all (the load's and the restarts' ticks run back to
# back after a pause, a tenth of a millisecond each) takes the next seen one's: they ran after the
# pause, with the keys as they were then.  The first line is "start <the level's first tick>";
# PRETICKS=13 for levels 0, 2, 5, 8 and 16, whose load runs on that many ticks.  With a fourth
# column in the joypad capture (DAT_0046d054, the "vampire" cheat's flag), each change of it gives a
# line "type <tick> vampire": the cheat came in after that tick (WM_CHAR runs between two ticks); with
# a fifth (the lives, DAT_00466527), the lives turning 99 give "type <tick> meowmeowlikemeowman".
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ticks import le, load, level_start

samples = load(sys.argv[1], 8)
lv = int(sys.argv[3])
o_start = level_start(samples, lv) + int(os.environ.get('PRETICKS', '0'))

recs = {}
typed, flag, lives = [], None, None
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
    except ValueError:
        continue                                        # (a line cut short at the end)
pad = {}
for g, rs in recs.items():
    t0, lt0, _ = rs[0]
    ended = [j for t, lt, j in rs if lt > lt0]
    pad[g] = ended[0] if ended else [j for t, lt, j in rs if t - t0 <= 5000][-1]

print('start', o_start)
for g, code in typed:
    print('type', g, code)
known = sorted(pad)
k = 0
for g in range(o_start, known[-1] + 1):
    while known[k] < g:
        k += 1
    print(g, pad[known[k]])
