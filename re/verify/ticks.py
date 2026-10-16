# ticks.py: the original's samples (capmem.sh) by engine tick.
#
# Within an engine tick (FUN_00443370) the engine's count (0x46fb74) goes up first, then the joypad
# is polled (FUN_00444940), then the logic runs (FUN_0043628c) and its count (0x4601d9) goes up at its
# end (0x4363fb).  A sample is therefore a tick's finished state when its logic count is the one the
# finished states around it have (lt - gf the largest found within three ticks); the last such sample
# of a tick is the tick's state.  A tick seen only with a smaller count is one whose end was not seen
# (the game was not scheduled between its start and its end, which can take a whole tick's time),
# unless the ticks after it keep the smaller count (the logic did not run in it: an engine exit).
# A tick without a finished sample is left out.

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
    def kept(g):
        later = [off[g + d] for d in range(1, 9) if (g + d) in off]
        return len(later) >= 3 and max(later) == off[g]
    fin = {}
    for i, g in enumerate(gf):
        want = max(off[g + d] for d in range(-3, 4) if (g + d) in off)
        if lt[i] - g >= want or (lt[i] - g == off[g] and kept(g)):
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
