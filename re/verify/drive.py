#!/usr/bin/env python3
# drive.py <pid> <level> <plan>: plays the running original from its memory, for the long captures
# (capmem.sh's DRIVE=<plan>).
#
# After each logic tick (its count, 0x4601d9, going up) the state is read through /proc/<pid>/mem
# (Harry's record at 0x465b0f, the objects from the list's head at 0x462848) and the keys the plan
# wants for the next tick are pressed or let go through XTest on the virtual display (the original
# reads them with GetAsyncKeyState when its next tick polls the joypad).  With Debug_AllowCheat = 1
# in the registry, Ctrl and the arrows fly Harry 7 px a tick (FUN_00426810, the joypad's 0x100);
# "vampire" is typed once the level runs (DAT_0046d054: no damage; mkpad.py finds the tick it came
# in, from JOYOUT's fourth column).  The plans:
#
#   jaguar     level 0: over the arena to its right edge and down along it (the jaguar's .als entry
#              is met by the camera coming down), then the whip whenever the jaguar is within 70 px
#   stones     level 3: to the stone jaguars' ledge, the whip at the awake ones, then up the rope
#              their death leaves and out to the left
#   jaguarman  level 8: down beside the arena and back up, the whip at the jaguar man, then into
#              the stone face's mouth
#   spirit     level 10: the whip and the sling at the Warrior Spirit, under its head
#   end        level 10: the Warrior Spirit shot down from the fly, the last letter picked up, and on
#              through the reunion, the end and its credits to the title
#   sweep      any level: rows across the whole level, top to bottom, ROW px apart (default 160)
#   vault      levels 11 .. 13: the Simon game played right, round after round
#   play       any level: dropped at spots across it and played at random there (SEED, PLAY ticks
#              a spot, PLAYX x PLAYY px apart)
#   vortex     levels 2, 3, 4 and 8: dropped onto the level's vortex, through it to a vault (its
#              game played as by vault, until it lets Harry out, back to the level) or on level 3
#              to the 2600 level (played at random, SEED, or with A26=left run to his deaths, until
#              its game is over and back), or with "mijmrowhtrae" typed (CHEATS) to level 24, where
#              the crate that stands for Earthworm Jim is whipped (back to Copan 200 ticks later)
#   idle       nothing (the cheat alone)
import ctypes
import os
import subprocess
import sys
import time

LEVEL, LTICK, GTICK = 0x451a2c, 0x4601d9, 0x46fb74
HARRY, HEAD = 0x465b0f, 0x462848


class Mem:
    def __init__(self, pid):
        self.f = open('/proc/%d/mem' % pid, 'rb', buffering=0)

    def rd(self, a, n):
        self.f.seek(a)
        return self.f.read(n)

    def u8(self, a):
        return self.rd(a, 1)[0]

    def s16(self, a):
        return int.from_bytes(self.rd(a, 2), 'little', signed=True)

    def u32(self, a):
        return int.from_bytes(self.rd(a, 4), 'little')


class Obj:
    def __init__(self, addr, b):
        self.addr, self.b = addr, b
        self.x = int.from_bytes(b[0:2], 'little', signed=True)
        self.y = int.from_bytes(b[4:6], 'little', signed=True)
        self.type, self.facing = b[8], b[0xb]

    def l(self, off):
        return int.from_bytes(self.b[off:off + 4], 'little')

    def w(self, off):
        return int.from_bytes(self.b[off:off + 2], 'little', signed=True)


class State:
    def __init__(self, m):
        self.m = m

    def level(self):
        return self.m.u32(LEVEL)

    def hx(self):
        return self.m.s16(HARRY)

    def hy(self):
        return self.m.s16(HARRY + 4)

    def w(self, a):
        return self.m.s16(a)

    def on_ground(self):
        return self.m.u8(0x465b70) != 0

    def size(self):
        return self.m.s16(0x465bae), self.m.s16(0x465bb0)

    def objects(self):
        out, a = [], self.m.u32(HEAD)
        while a != 0xffffffff and len(out) < 0x50:
            b = self.m.rd(a, 0x6e)
            out.append(Obj(a, b))
            a = int.from_bytes(b[0x18:0x1c], 'little')
        return out

    def find(self, t):
        return [o for o in self.objects() if o.type == t]


class Keys:
    def __init__(self):
        self.x = ctypes.CDLL('libX11.so.6')
        self.t = ctypes.CDLL('libXtst.so.6')
        self.x.XOpenDisplay.restype = ctypes.c_void_p
        self.x.XOpenDisplay.argtypes = [ctypes.c_char_p]
        self.x.XStringToKeysym.restype = ctypes.c_ulong
        self.x.XStringToKeysym.argtypes = [ctypes.c_char_p]
        self.x.XKeysymToKeycode.restype = ctypes.c_ubyte
        self.x.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        self.x.XFlush.argtypes = [ctypes.c_void_p]
        self.t.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
        self.d = self.x.XOpenDisplay(os.environ['DISPLAY'].encode())
        self.down = set()

    def code(self, k):
        return self.x.XKeysymToKeycode(self.d, self.x.XStringToKeysym(k.encode()))

    def ev(self, k, down):
        self.t.XTestFakeKeyEvent(self.d, self.code(k), 1 if down else 0, 0)

    def hold(self, keys):
        """the keys held from now on"""
        keys = set(keys)
        for k in self.down - keys:
            self.ev(k, False)
        for k in keys - self.down:
            self.ev(k, True)
        self.down = keys
        self.x.XFlush(self.d)

    def type(self, text):
        for c in text:
            self.ev(c, True); self.x.XFlush(self.d); time.sleep(0.04)
            self.ev(c, False); self.x.XFlush(self.d); time.sleep(0.04)


# ---------------------------------------------------------------- the plans: a generator of each
# tick's keys, reading the state after the tick before

CTRL = 'Control_L'


def fly(s, x, y, tol=7):
    """with Ctrl, to within tol px of (x, y), or until Harry gets no nearer for 100 ticks (something
    holds him: the jaguar man walking him into its arena, say) or 3000 ticks have gone by"""
    best, since = None, 0
    for _ in range(3000):
        hx, hy = s.hx(), s.hy()
        dx, dy = x - hx, y - hy
        if abs(dx) <= tol and abs(dy) <= tol:
            return
        d = abs(dx) + abs(dy)
        if best is None or d < best:
            best, since = d, 0
        else:
            since += 1
            if since >= 100:
                return
        k = {CTRL}
        if abs(dx) > tol:
            k.add('Right' if dx > 0 else 'Left')
        if abs(dy) > tol:
            k.add('Down' if dy > 0 else 'Up')
        yield k


def idle(s, n):
    for _ in range(n):
        yield set()


def whip_near(s, objs, t, reach=70):
    return any(abs(o.x - s.hx()) < reach for o in objs) and t % 3 == 0


def plan_jaguar(s):
    yield from fly(s, 3030, 400)
    yield from fly(s, 3030, 980)
    t = 0
    while s.level() == 0:
        yield {'q'} if whip_near(s, s.find(55), t) else set()
        t += 1
    while True:
        yield set()


def plan_stones(s):
    for leg in ((2400, 700), (2400, 140), (2250, 140)):
        yield from fly(s, *leg)
    phase, t = 0, 0
    while s.level() == 3:
        hx, hy = s.hx(), s.hy()
        k, rope = set(), None
        for o in s.objects():
            if o.type == 0x8c and o.l(0x32) != 0x41cb3a and abs(o.x - hx) < 70 and t % 3 == 0:
                k.add('q')
            if o.type == 0x9a:
                rope = o
        on_rope = s.w(0x466508) != 0 and not s.on_ground()
        if rope and phase == 0:
            phase = 1
        if phase == 1 and on_rope:
            phase = 2
        if phase == 2 and hy <= 4:
            phase = 3
        if phase == 0:
            if abs(hx - 1990) > 4:
                k.add('Right' if hx < 1990 else 'Left')
        else:
            k = set()
            if phase == 1:
                if abs(hx - 1968) > 3:
                    k.add('Right' if hx < 1968 else 'Left')
                else:
                    k.add('Up')
                    if t % 40 < 6:
                        k.add('w')
            elif phase == 2:
                k.add('Up')
            else:
                k.add('Left')
                if on_rope and t % 8 < 4:
                    k.add('w')
        t += 1
        yield k
    while True:
        yield set()


def plan_jaguarman(s):
    for leg in ((5800, 280), (5800, 150)):
        yield from fly(s, *leg)
    t, shown, dead = 0, False, False
    while s.level() == 8:
        hx, hy = s.hx(), s.hy()
        k, boss = set(), False
        for o in s.objects():
            if o.type == 0x9e:
                boss = True
                st = o.l(0x32)
                if st != 0x41ecc3:
                    shown = True
                    if abs(o.x - hx) < 70 and t % 3 == 0:
                        k.add('q')
                    if abs(o.x - hx) >= 60 and hy > 280:
                        k.add('Left' if o.x < hx else 'Right')
        if shown and not boss:
            dead = True
        if dead:
            k = set()
            if hy < 200:
                k.add('Right')
            elif abs(hx - 6008) > 4:
                k.add('Right' if hx < 6008 else 'Left')
            elif t % 30 < 4:
                k.add('w')
        t += 1
        yield k
    while True:
        yield set()


def plan_spirit(s):
    t = 0
    while s.level() == 10:
        hx = s.hx()
        k = set()
        for o in s.find(0x69):
            if abs(o.x - hx) < 70 and t % 3 == 0:
                k.add('q')
            elif t % 24 == 0:
                k.add('e')
        t += 1
        yield k
    while True:
        yield set()


def plan_sweep(s):
    row = int(os.environ.get('ROW', '160'))
    w, h = s.size()
    right = True
    for y in list(range(40, h - 16, row)) + [h - 16]:
        yield from fly(s, s.hx(), y)
        yield from fly(s, w - 16 if right else 16, y)
        right = not right
    while True:
        yield set()


def plan_vault(s):
    """the Simon game of a Loltun vault (11 .. 13), played right: on the player's turn (the leading
    lever, [0x46617c], at its check 0x40be92 with the combination no longer shown, 0x46617f) the
    keys of the combination at 0x466194 (a lever's place among the type-26 records of the list;
    the keys 1 .. 3 send Harry to the left lever, below x 0x84, the middle one, below 0xb4, and the
    right one), one every 8 ticks"""
    t, keyed, turn = 0, 0, False
    while True:
        k = set()
        objs = s.objects()
        levers = [o for o in objs if o.type == 0x1a]
        lead = [o for o in objs if o.addr == s.m.u32(0x46617c)]
        mine = bool(lead) and lead[0].l(0x52) == 0x40be92 and s.m.u8(0x46617f) == 0
        if mine and not turn:
            keyed = 0
        turn = mine
        if mine and t % 8 == 0:
            combo = []
            for i in range(12):
                c = s.m.u8(0x466194 + i)
                if c == 0xff:
                    break
                combo.append(c)
            if keyed < len(combo) and combo[keyed] < len(levers):
                x = levers[combo[keyed]].x
                k.add('1' if x < 0x84 else '2' if x < 0xb4 else '3')
                keyed += 1
        t += 1
        yield k


def plan_play(s):
    """at spots across the level (PLAYX x PLAYY px apart), dropped from the fly and played at
    random (SEED) for PLAY ticks each: a way and the buttons held for 4 .. 40 ticks"""
    import random
    rnd = random.Random(int(os.environ.get('SEED', '1')))
    w, h = s.size()
    dx, dy = int(os.environ.get('PLAYX', '480')), int(os.environ.get('PLAYY', '240'))
    n = int(os.environ.get('PLAY', '360'))
    ways = [(), ('Left',), ('Right',), ('Up',), ('Down',), ('Left', 'Up'), ('Right', 'Up'), ('Left', 'Down'),
            ('Right', 'Down')]
    right = True
    for y in range(dy // 2, h, dy):
        xs = list(range(dx // 2, w, dx))
        for x in (xs if right else xs[::-1]):
            yield from fly(s, x, y)
            t = 0
            while t < n:
                k = set(rnd.choice(ways))
                for b in 'qwe':
                    if rnd.random() < 0.3:
                        k.add(b)
                for _ in range(rnd.randint(4, 40)):
                    yield k
                    t += 1
        right = not right
    while True:
        yield set()


def plan_a26(s):
    """the 2600 level: a way and the buttons held at random (SEED) for 4 .. 40 ticks at a time, or with
    A26=left Left alone held (no jumps: Harry runs into what kills him until the game is over, and the
    held key, the engine's start key as it repeats, goes on out of it)"""
    if os.environ.get('A26') == 'left':
        while True:
            yield {'Left'}
    import random
    rnd = random.Random(int(os.environ.get('SEED', '1')))
    ways = [(), ('Left',), ('Right',), ('Up',), ('Down',), ('Left', 'Up'), ('Right', 'Up'), ('Left', 'Down'),
            ('Right', 'Down')]
    while True:
        k = set(rnd.choice(ways))
        for b in 'qwe':
            if rnd.random() < 0.3:
                k.add(b)
        for _ in range(rnd.randint(4, 40)):
            yield k


def plan_end(s):
    """level 10: the Warrior Spirit shot down from the fly.  Harry is flown to 27 px under its head and
    31 px before it (by his facing), let go there and the sling fired as he falls (every 4 ticks for 20
    ticks), again and again, and left alone while he is stunned (0x427316, hyidazed) or held and thrown
    (0x42c2ff) until he is up again; once it is dead, he walks under the last letter of PITFALL it drops;
    then nothing: the reunion plays out, and the end's words and credits for END ticks (2000), when a
    button skips the rest of them (as a key does) and the title comes"""
    phase, t0, t = 0, 0, 0
    while s.level() == 10:
        k = set()
        objs = s.objects()
        sp = [o for o in objs if o.type == 0x69]
        hx, hy = s.hx(), s.hy()
        if not sp:
            letter = [o for o in objs if o.type == 0xb0]
            if letter and abs(hx - letter[0].x) > 7:
                k.add('Right' if hx < letter[0].x else 'Left')
        else:
            a = sp[0].l(0x64)
            head = Obj(a, s.m.rd(a, 0x6e))
            cont = s.m.u32(0x465b69)
            held = cont in (0x427316, 0x42c2ff) or s.m.u32(0x465b1d) == s.m.u32(0x4700e8)
            if held and phase != 1:
                phase = 2
            if phase == 2:
                if not held and s.on_ground():
                    phase = 0
            elif phase == 0:
                tx = head.x + (31 if s.m.u8(0x465b1a) else -31)
                ty = head.y + 27
                k.add(CTRL)
                if abs(hx - tx) > 7:
                    k.add('Right' if hx < tx else 'Left')
                if abs(hy - ty) > 7:
                    k.add('Down' if hy < ty else 'Up')
                if (abs(hx - tx) <= 7 and abs(hy - ty) <= 7) or t - t0 > 200:
                    phase, t0 = 1, t
            else:
                if (t - t0) % 4 == 1:
                    k.add('e')
                if t - t0 >= 20:
                    phase, t0 = 0, t
        t += 1
        yield k
    while s.level() != 23:
        yield set()
    for _ in range(int(os.environ.get('END', '2000'))):
        yield set()
    for _ in range(3):
        yield {'q'}
    while True:
        yield set()


def plan_ew(s):
    """level 24 (Copan's vortex with "mijmrowhtrae" typed): let go to the left of the crate that stands
    for Earthworm Jim (type 34, at 1336, 871), walked up to it and the whip until it blows up; then
    nothing (200 ticks later the level goes back to Copan)"""
    yield from fly(s, 1286, 860)
    t = 0
    while s.level() == 24:
        crates = [o for o in s.objects() if o.type in (34, 50)]
        k = set()
        if crates and s.on_ground():
            dx = crates[0].x - s.hx()
            if abs(dx) > 48:
                k.add('Right' if dx > 0 else 'Left')
            elif t % 3 == 0:
                k.add('q')
        t += 1
        yield k
    while True:
        yield set()


# the vortices (type 71: the .als id 0x46) by level, where their entries put them
VORTICES = {2: (4808, 672), 3: (1960, 1360), 4: (2424, 32), 8: (6040, 400)}


def plan_vortex(s):
    """flown to the level's vortex and let go there: Harry falls to the ground in its box and spins
    away, to a Loltun vault (from levels 2, 4 and 8: its game played as plan_vault plays it, until
    the way out, or the ceiling, lets him out, back to the level) or to the 2600 level (from level 3:
    played at random until its game is over and a key goes back); then nothing"""
    lv = s.level()
    yield from fly(s, *VORTICES[lv])
    while s.level() == lv:
        yield set()
    there = s.level()
    gen = plan_vault(s) if there in (11, 12, 13) else plan_ew(s) if there == 24 else plan_a26(s)
    while s.level() == there:
        yield next(gen)
    while True:
        yield set()


def plan_idle(s):
    while True:
        yield set()


PLANS = {'jaguar': plan_jaguar, 'stones': plan_stones, 'jaguarman': plan_jaguarman, 'spirit': plan_spirit,
         'sweep': plan_sweep, 'vault': plan_vault, 'play': plan_play, 'vortex': plan_vortex, 'end': plan_end,
         'idle': plan_idle}


def main():
    pid, lv, plan = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
    log = open(sys.argv[4], 'w') if len(sys.argv) > 4 else None
    m = Mem(pid)
    s = State(m)
    keys = Keys()
    while s.level() != lv:                                 # the level picked from the menu
        time.sleep(0.01)
    t0 = m.u32(LTICK)
    while (m.u32(LTICK) - t0) & 0xffffffff < 30:            # and running
        time.sleep(0.01)
    subprocess.run(['xdotool', 'search', '--name', '^Pitfall$', 'windowfocus'], stderr=subprocess.DEVNULL)
    for code in os.environ.get('CHEATS', 'vampire').split():
        keys.type(code)
    if log:
        print('typed', m.u32(GTICK), 'level size', *s.size(), file=log, flush=True)
    gen = PLANS[plan](s)
    last = m.u32(LTICK)
    while True:
        while True:                                        # the next logic tick's end
            lt = m.u32(LTICK)
            if lt != last:
                last = lt
                break
            time.sleep(0.0003)
        k = next(gen)
        if k != keys.down:
            keys.hold(k)
            if log:
                print(m.u32(GTICK), lt, ' '.join(sorted(k)), s.hx(), s.hy(), file=log, flush=True)


if __name__ == '__main__':
    try:
        main()
    except (KeyboardInterrupt, BrokenPipeError):
        pass
