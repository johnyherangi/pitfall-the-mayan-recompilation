# ticks.py: the original's samples (capmem.sh) by engine tick.
#
# Within an engine tick (FUN_00443370) the engine's count (0x46fb74) goes up first, then the joypad
# is polled (FUN_00444940), then the logic runs (FUN_0043628c) and its count (0x4601d9) goes up at its
# end (0x4363fb).  A finished tick's sample therefore has the logic count a steady distance ahead of
# the engine's: the distance goes up with a load's ticks and down by one for good with an engine exit
# (the logic did not finish that tick), and a tick seen only with a smaller distance that the ticks
# after it come back up from (within 60 ticks: the game was not scheduled between the tick's start
# and its end, or it ran ticks back to back after a pause, all of them seen before their ends) is a
# tick whose end was not seen.  The last sample of a tick at the steady distance is the tick's state;
# a tick without one is left out.

def le(h):
    return int.from_bytes(bytes.fromhex(h), 'little')


def load(path, ncols):
    """the records of a capture with at least ncols columns (a last line cut short is left out)"""
    out = []
    for line in open(path):
        r = line.split()
        if len(r) >= ncols:
            out.append(r)
    return out


def finished(samples):
    """{gf: index of the tick's last finished sample}"""
    gf = [le(r[1]) for r in samples]
    lt = [le(r[2]) for r in samples]
    off = {}
    for i, g in enumerate(gf):
        off[g] = max(off.get(g, -10**9), lt[i] - g)
    ticks = sorted(off)
    steady, cur = {}, None
    for j, g in enumerate(ticks):
        o = off[g]
        if cur is None or o >= cur:
            cur = o                                     # (up: a load's ticks)
        else:
            later = [off[h] for h in ticks[j + 1:j + 61] if h - g <= 60]
            if len(later) >= 3 and max(later) < cur:
                cur = o                                 # down for good: an engine exit
        steady[g] = cur
    fin = {}
    for i, g in enumerate(gf):
        if lt[i] - g >= steady[g]:
            fin[g] = i
    return fin


def level_start(samples, lv):
    """the level's first tick: the one after the tick its load came in, the first time the level
    variable (0x451a2c) turns to it after the intro (21; at the boot it is 0)"""
    seen_intro = False
    for r in samples:
        v = le(r[4])
        if v == 21:
            seen_intro = True
        elif seen_intro and v == lv:
            return le(r[1]) + 1
    raise SystemExit('level %d never starts' % lv)
