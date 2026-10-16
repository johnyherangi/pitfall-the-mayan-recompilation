# framepred.py <joypad capture> <frames>: each picture capmem.sh's FRAMES kept (xgrab.c), with the
# engine ticks the original was on when it was first and last grabbed, for memdump's FRAMEPRED.
#
# The joypad capture (JOYOUT) has the engine's count (0x46fb74) as each record saw it, its times from
# the sampler's start (JOYOUT.t0); the frames' .times have each grab's picture and the times it was
# asked for and came back, on the same clock (CLOCK_MONOTONIC).  A record comes when the sampler
# sees the count change, which may be late (it was not running), never early: during a grab the
# game was on the tick of the record before its asking or a later one, up to the tick of the record
# after its return.  The count goes up as a tick begins, before its poll, logic and drawing, so a
# grab on tick g shows tick g's picture or an earlier one (the X server, behind under load, may draw
# the game's pictures a few ticks late).  Writes "picture first-tick last-tick latest-tick" lines:
# the record's before the picture's first grab, the one's after its last and the one's before its
# last (-1: before the game's first record).
import bisect
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ticks import le

joy, frames = sys.argv[1], sys.argv[2]
t0 = int(open(joy + '.t0').read().split()[0])
ts, gs = [], []
for line in open(joy):
    r = line.split()
    if len(r) < 2 or len(r[1]) != 8:
        continue
    t = int(r[0]) + t0
    if ts and t < ts[-1]:
        continue
    ts.append(t)
    gs.append(le(r[1]))


def before(t):
    i = bisect.bisect_right(ts, t) - 1
    return gs[i] if i >= 0 else -1


def after(t):
    i = bisect.bisect_right(ts, t)
    return gs[i] if i < len(ts) else gs[-1]


span = {}
for line in open(frames + '.times'):
    r = line.split()
    if len(r) < 2:
        continue
    j, a = int(r[0]), int(r[1])
    b = int(r[2]) if len(r) > 2 else a
    if j in span:
        span[j][1:] = [after(b), before(a)]
    else:
        span[j] = [before(a), after(b), before(a)]
for j in sorted(span):
    print(j, *span[j])
