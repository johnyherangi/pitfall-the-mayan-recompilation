#!/bin/bash
# capmem.sh <menu-item-y> <seconds> <out.txt> [T:down|up:KEY ...]
#
# Runs the original under Wine on a virtual display, dismisses its warning, picks a level from its
# File menu 7 s after the start (the item at screen y <menu-item-y>, see README.md), holds and lets
# go of keys at their times (seconds after the start, xdotool key names) and meanwhile samples its
# memory with memsample (one record per change: the microseconds, then each range in hex).
#
#   WATCH     the ranges (memsample's form); the default is what cmpmem.py reads: the engine's tick
#             count, the logic's, the random generator, the level, a5, the logic's memory, the
#             object records and the joypad
#   JOYOUT    a second file: the two tick counts, the joypad, the "vampire" cheat's flag, the
#             lives and the engine's start key (DAT_0046fb88, any key's WM_KEYDOWN) alone, read
#             without a pause, so that every tick's joypad is seen (mkpad.py's JOY)
#   DRIVE     "<level> <plan>": drive.py plays the level from its memory once it runs (it needs
#             Debug_AllowCheat = 1 in the registry; DRIVE_LOG, a file for its keys; CHEATS, the codes
#             it types first, "vampire" by default)
#   PF_GAME   the folder with PITFALL.EXE and its files, where it is run (default: original)
#   WINEPREFIX  a 32-bit prefix whose HKLM\Software\Activision\Pitfall has Option_Slow = 1 (no CD)
#   PF_DISPLAY  the virtual display (default :91)
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
Y=$1; SECS=$2; OUT=$(realpath -m "$3"); shift 3
GAME=$(realpath "${PF_GAME:-original}")
export WINEARCH=win32 WINEDEBUG=-all DISPLAY=${PF_DISPLAY:-:91}
WATCH=${WATCH:-"46fb74:4 4601d9:4 466772:12 451a2c:4 4601b4:4 465800:4352 462854:8800 4517f8:4"}
SAMPLER=$HERE/memsample
[ -x "$SAMPLER" ] || gcc -O2 -o "$SAMPLER" "$HERE/memsample.c" || exit 1
Xvfb $DISPLAY -screen 0 1024x768x24 > /dev/null 2>&1 &
XPID=$!
sleep 1
cd "$GAME"
xdotool mousemove 300 260
wineserver -k 2>/dev/null
OLD=$(pgrep -f -i "pitfall.exe" | tr '\n' ' ')
start=$(date +%s.%N)
wine PITFALL.EXE > /dev/null 2>&1 &
PID=""
for i in $(seq 1 150); do
  sleep 0.1
  for p in $(pgrep -f -i "pitfall.exe"); do
    case " $OLD " in *" $p "*) continue;; esac          # (one left from an earlier run)
    grep -q -i "pitfall.exe" /proc/$p/maps 2>/dev/null && grep -q "^00400000" /proc/$p/maps && PID=$p
  done
  [ -n "$PID" ] && break
done
[ -n "$PID" ] || { echo "PITFALL.EXE did not start" >&2; kill $XPID; exit 1; }
"$SAMPLER" $PID "$OUT" $SECS $WATCH &
MPID=$!
[ -n "${JOYOUT:-}" ] && NAP_US=0 "$SAMPLER" $PID "$(realpath -m "$JOYOUT")" $SECS 46fb74:4 4601d9:4 4517f8:4 46d054:1 466527:1 46fb88:1 &
for i in 1 2 3 4; do sleep 1; xdotool search --name "^Pitfall$" > /dev/null 2>&1 && xdotool key Return; done
at() { local now=$(date +%s.%N); local d=$(echo "$1 - ($now - $start)" | bc); [ $(echo "$d > 0" | bc) = 1 ] && sleep $d; }
at 7
xdotool mousemove 157 150 click 1
sleep 0.7
xdotool mousemove 200 $Y click 1
xdotool mousemove 300 260
DPID=""
if [ -n "${DRIVE:-}" ]; then
  python3 "$HERE/drive.py" $PID $DRIVE "$(realpath -m "${DRIVE_LOG:-/dev/null}")" &
  DPID=$!
fi
for step in "$@"; do
  t=${step%%:*}; rest=${step#*:}; kind=${rest%%:*}; key=${rest#*:}
  at $t
  W=$(xdotool search --name "^Pitfall$" | head -1); [ -n "$W" ] && xdotool windowfocus $W 2>/dev/null
  case $kind in down) xdotool keydown $key ;; up) xdotool keyup $key ;; esac
done
wait $MPID; status=$?
[ -n "$DPID" ] && kill $DPID 2>/dev/null
wineserver -k 2>/dev/null
sleep 1
kill $XPID 2>/dev/null          # (gone already when another server had the display)
exit $status
