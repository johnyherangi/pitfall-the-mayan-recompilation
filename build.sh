#!/bin/bash
# build.sh [modern] [test] [run] [pack]
#
# The whole build in one go, on macOS or Linux: the original's files imported from assets-in/ (if
# original/ has none yet), CMake configured, the modern shell built and copied to dist/modern/
# beside this file (on macOS as Pitfall.app, one app for Apple silicon and Intel Macs, macOS 10.15
# and later).  "test" runs the tests against original/ after the build; "run" starts the game
# afterwards; "pack" copies original/ into dist/modern/ too (on macOS into the app's Resources,
# the app then signed again), so that folder alone can be moved to another machine.  (The classic
# shell is the original's Windows window: build.bat.)
#
# Needs CMake 3.21+ (3.24+ on macOS) and a C++20 compiler (macOS: Xcode's command-line tools;
# Linux: g++ 12+ or clang 15+, ninja or make, and SDL's development packages, named below).
# ffmpeg on the path is optional (the music as Ogg instead of WAV, and the launcher's intro).
set -e
cd "$(dirname "$0")"
RUN=0; PACK=0; TEST=0
for a in "$@"; do case "$a" in run) RUN=1;; pack) PACK=1;; test) TEST=1;; modern) ;; classic) echo "build.sh: the classic shell is Windows only (build.bat)"; exit 1;; esac; done

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
    # (ls without -A: hidden files such as Finder's .DS_Store are not a disc)
    if [ -z "$(ls assets-in 2>/dev/null)" ]; then
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
    if ! command -v ffmpeg >/dev/null 2>&1; then
        echo "build.sh: ffmpeg was not found, so the disc's music goes into original/ as WAV files rather than Ogg."
        case "$(uname)" in
            Darwin) echo "  (brew install ffmpeg; afterwards  build-modern/phimport --force assets-in original  cuts it again as Ogg)" ;;
            *) echo "  (sudo apt install ffmpeg; afterwards  build-modern/phimport --force assets-in original  cuts it again as Ogg)" ;;
        esac
    fi
fi

# ---- the build
GEN=()
command -v ninja >/dev/null 2>&1 && GEN=(-G Ninja)
echo "==== the modern shell"
cmake -S . -B build-modern "${GEN[@]}" -DCMAKE_BUILD_TYPE=Release -DPITFALL_SHELL=modern
cmake --build build-modern --parallel
if [ "$TEST" = 1 ]; then
    echo "==== the tests, against original/"
    ctest --test-dir build-modern --output-on-failure
fi
mkdir -p dist/modern
if [ "$(uname)" = Darwin ]; then
    rm -rf dist/modern/Pitfall.app
    cp -R build-modern/Pitfall.app dist/modern/
    cp build-modern/phdump build-modern/phimport dist/modern/ 2>/dev/null || true
    if [ "$PACK" = 1 ]; then
        echo "==== copying original/ into the app"
        rm -rf dist/modern/Pitfall.app/Contents/Resources/original
        cp -R original dist/modern/Pitfall.app/Contents/Resources/
        codesign --force --sign - dist/modern/Pitfall.app        # (the signature then covers the game's files too)
    fi
    echo "==== dist/modern/Pitfall.app is ready (it finds original/ two folders up from dist/, or inside its Resources)"
    if [ "$RUN" = 1 ]; then open dist/modern/Pitfall.app --args "$PWD/original"; fi
else
    cp build-modern/pitfall build-modern/phdump build-modern/phimport dist/modern/
    if [ "$PACK" = 1 ]; then
        echo "==== copying original/ into dist/modern/"
        rm -rf dist/modern/original
        cp -R original dist/modern/
    fi
    echo "==== dist/modern/pitfall is ready (it finds original/ two folders up, or beside it)"
    if [ "$RUN" = 1 ]; then exec dist/modern/pitfall "$PWD/original"; fi
fi
exit 0
