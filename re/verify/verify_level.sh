#!/bin/bash
# verify_level.sh <level> <menu-item-y> [workdir]: one level of the original against the port, by
# their memory.  The original is captured for 50 s (capmem.sh: the level picked from the menu, the
# right arrow held from 14 s, the whip every 0.6 s, a jump every 2.3 s, four sling shots; in the
# vaults, 11 to 13, which read the keys 1 to 3 alone, the three levers in turn every 0.9 s), the
# port replays its joypad tick by tick (memdump) and the two are compared (cmpmem.py).  Run it from
# the repository root after a CMake/Ninja build in build/; PF_GAME, WINEPREFIX and PF_DISPLAY as for
# capmem.sh.
set -eu
LV=$1; Y=$2; W=${3:-verify-out}
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$W"
pre=0; case $LV in 0|2|5|8|16) pre=13;; esac
[ -x "$W/memdump" ] || g++ -std=c++20 -O2 -Isrc "$HERE/memdump.cpp" \
    build/libpitfall_game.a build/libpitfall_engine.a build/libpitfall_ph.a -o "$W/memdump"
KEYS=$(python3 - $LV <<'PY'
import sys
ev = []
if 11 <= int(sys.argv[1]) <= 13:
    t, k = 15.0, 0
    while t < 46:
        ev += [(round(t, 2), 'down', '123'[k % 3]), (round(t + 0.2, 2), 'up', '123'[k % 3])]; t += 0.9; k += 1
else:
    ev = [(14.0, 'down', 'Right')]
    t = 15.0
    while t < 46:
        ev += [(round(t, 2), 'down', 'q'), (round(t + 0.2, 2), 'up', 'q')]; t += 0.6
    t = 16.1
    while t < 46:
        ev += [(round(t, 2), 'down', 'w'), (round(t + 0.3, 2), 'up', 'w')]; t += 2.3
    for e in (21.45, 27.45, 33.45, 39.45):
        ev += [(e, 'down', 'e'), (round(e + 0.15, 2), 'up', 'e')]
print(' '.join('%s:%s:%s' % x for x in sorted(ev)))
PY
)
JOYOUT="$W/joy$LV.txt" bash "$HERE/capmem.sh" $Y 50 "$W/orig$LV.txt" $KEYS
PRETICKS=$pre python3 "$HERE/mkpad.py" "$W/orig$LV.txt" "$W/joy$LV.txt" $LV > "$W/pad$LV.txt"
start=$(head -1 "$W/pad$LV.txt" | awk '{print $2}')
MENU=$LV@$(( start - pre + 40 )) PAD="$W/pad$LV.txt" "$W/memdump" 2500 > "$W/port$LV.txt"
PRETICKS=$pre python3 "$HERE/cmpmem.py" "$W/orig$LV.txt" "$W/port$LV.txt" $LV | tee "$W/result$LV.txt"
