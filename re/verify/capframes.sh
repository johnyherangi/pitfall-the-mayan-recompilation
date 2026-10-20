#!/bin/bash
# capframes.sh <menu-item-y> <seconds> <out.raw> [T:down|up:KEY ...]: capmem.sh's run of the original,
# its game area (320x224 at 144,159 on the virtual display) recorded at 20 frames a second instead
# (ffmpeg, lossless), then converted to raw RGB24 frames for cmpframes.  A menu-item-y of 0 picks
# nothing: the attract run.  PF_GAME, WINEPREFIX, PF_DISPLAY as for capmem.sh.
set -u
Y=$1; SECS=$2; OUT=$(realpath -m "$3"); shift 3
GAME=$(realpath "${PF_GAME:-original}")
export WINEARCH=win32 WINEDEBUG=-all DISPLAY=${PF_DISPLAY:-:91}
Xvfb $DISPLAY -screen 0 1024x768x24 > /dev/null 2>&1 &
XPID=$!
sleep 1
cd "$GAME"
wineserver -k 2>/dev/null                # (an earlier run's game)
xdotool mousemove 300 260
ffmpeg -y -loglevel error -f x11grab -draw_mouse 0 -video_size 320x224 -framerate 20 -i $DISPLAY.0+144,159 -c:v ffv1 -t $SECS "$OUT.mkv" &
FPID=$!
start=$(date +%s.%N)
wine PITFALL.EXE > /dev/null 2>&1 &
for i in 1 2 3 4; do sleep 1; xdotool search --name "^Pitfall$" > /dev/null 2>&1 && xdotool key Return; done
at() { local now=$(date +%s.%N); local d=$(echo "$1 - ($now - $start)" | bc); [ $(echo "$d > 0" | bc) = 1 ] && sleep $d; }
if [ "$Y" != 0 ]; then
  at 7
  xdotool mousemove 157 150 click 1
  sleep 0.7
  xdotool mousemove 200 $Y click 1
  xdotool mousemove 300 260
fi
for step in "$@"; do
  t=${step%%:*}; rest=${step#*:}; kind=${rest%%:*}; key=${rest#*:}
  at $t
  W=$(xdotool search --name "^Pitfall$" | head -1); [ -n "$W" ] && xdotool windowfocus $W 2>/dev/null
  case $kind in down) xdotool keydown $key ;; up) xdotool keyup $key ;; esac
done
wait $FPID
wineserver -k 2>/dev/null
sleep 1
kill $XPID 2>/dev/null
ffmpeg -y -loglevel error -i "$OUT.mkv" -f rawvideo -pix_fmt rgb24 "$OUT" && rm -f "$OUT.mkv"
