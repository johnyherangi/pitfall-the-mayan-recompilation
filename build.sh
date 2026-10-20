#!/bin/bash
# build.sh [modern] [run] [pack]
#
# The whole build in one go, on macOS or Linux: the original's files imported from assets-in/ (if
# original/ has none yet), CMake configured, the modern shell built and copied to dist/modern/
# beside this file (on macOS as Pitfall.app).  "run" starts the game afterwards; "pack" copies
# original/ into dist/modern/ too (on macOS into the app's Resources), so that folder alone can
# be moved to another machine.  (The classic shell is the original's Windows window: build.bat.)
#
# Needs CMake 3.21+ and a C++20 compiler (macOS: Xcode's command-line tools; Linux: g++ 12+ or
# clang 15+, ninja or make, and SDL's development packages, named below).  ffmpeg on the path is
# optional (the music as Ogg instead of WAV, and the launcher's intro).
set -e
cd "$(dirname "$0")"
RUN=0; PACK=0
for a in "$@"; do case "$a" in run) RUN=1;; pack) PACK=1;; modern) ;; classic) echo "build.sh: the classic shell is Windows only (build.bat)"; exit 1;; esac; done

# ---- the tools
if ! command -v cmake >/dev/null 2>&1; then
    echo "build.sh: CMake was not found."
    case "$(uname)" in
        Darwin) echo "  Install Xcode's command-line tools (xcode-select --install) and CMake (brew install cmake)." ;;
        *) echo "  Debian / Ubuntu:  sudo apt install cmake ninja-build g++ libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxinerama-dev libxss-dev libgl-dev libasound2-dev libpulse-dev libdbus-1-dev libudev-dev" ;;
    esac
    exit 1
fi
if [ "$(uname)" != Darwin ] && ! pkg-config --exists x11 2>/dev/null; then
    echo "build.sh: SDL needs the X11 and sound development packages.  Debian / Ubuntu:"
    echo "  sudo apt install ninja-build g++ libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxinerama-dev libxss-dev libgl-dev libasound2-dev libpulse-dev libdbus-1-dev libudev-dev"
    echo "  (going on anyway; the configure step will say what is missing)"
fi

# ---- the original's files
if [ ! -e original/INIT.PH ] && [ ! -e original/init.ph ] && [ ! -e original/ASSETS/INIT.PH ]; then
    if [ -z "$(ls -A assets-in 2>/dev/null)" ]; then
        mkdir -p assets-in
        cat <<'MSG'
build.sh: the original game's files are not here yet.

  Put what you have of the disc into the folder  assets-in/  next to this file: the CD's folder,
  a .zip of it, an .iso, or a .cue with its .img / .bin (a CloneCD or CDRWIN image, which also
  carries the music).  Then run build.sh again: the build takes the game's files, the music and
  the intro out of it into original/.
MSG
        exit 1
    fi
fi

# ---- the build
GEN=()
command -v ninja >/dev/null 2>&1 && GEN=(-G Ninja)
echo "==== the modern shell"
cmake -S . -B build-modern "${GEN[@]}" -DCMAKE_BUILD_TYPE=Release -DPITFALL_SHELL=modern
cmake --build build-modern --parallel
mkdir -p dist/modern
if [ "$(uname)" = Darwin ]; then
    rm -rf dist/modern/Pitfall.app
    cp -R build-modern/Pitfall.app dist/modern/
    cp build-modern/phdump build-modern/phimport dist/modern/ 2>/dev/null || true
    [ "$PACK" = 1 ] && { echo "==== copying original/ into the app"; rm -rf dist/modern/Pitfall.app/Contents/Resources/original; cp -R original dist/modern/Pitfall.app/Contents/Resources/; }
    echo "==== dist/modern/Pitfall.app is ready (it finds original/ two folders up from dist/, or inside its Resources)"
    [ "$RUN" = 1 ] && open dist/modern/Pitfall.app --args "$PWD/original"
else
    cp build-modern/pitfall build-modern/phdump build-modern/phimport dist/modern/
    [ "$PACK" = 1 ] && { echo "==== copying original/ into dist/modern/"; rm -rf dist/modern/original; cp -R original dist/modern/; }
    echo "==== dist/modern/pitfall is ready (it finds original/ two folders up, or beside it)"
    [ "$RUN" = 1 ] && exec dist/modern/pitfall "$PWD/original"
fi
exit 0
