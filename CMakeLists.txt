cmake_minimum_required(VERSION 3.21)
project(PitfallMayanRecompilation LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Pitfall: The Mayan Adventure (Windows 95, Kinesoft/Activision 1995)
# Reconstruction as a modern C++20 code base.
#
#   pitfall_ph      - .PH asset stream parsers (sprites, levels, sounds, palette)
#   pitfall_engine  - software renderer, input, options, audio mixer, timing
#   pitfall_game    - game flow, display list, HUD, attract/demo playback
#   phdump          - command line asset extractor / frame renderer (portable)
#   pitfall         - the game executable (Win32/GDI front end, like the original;
#                     optional SDL2 front end with -DPITFALL_USE_SDL=ON)
#
# Build (Visual Studio 2022, or 2026 with "Visual Studio 18 2026"):
#   cmake -S . -B build -G "Visual Studio 17 2022" -A x64
#   cmake --build build --config Release
# Then copy the original game's *.PH files next to pitfall.exe (or pass the
# data directory as the first argument / -data <dir>).
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PITFALL_USE_SDL     "Build the SDL2 front end instead of the Win32/GDI one" OFF)
option(PITFALL_BUILD_TESTS "Build the asset round-trip tests" ON)
option(PITFALL_WARNINGS    "Enable strict compiler warnings" ON)

if(MSVC)
    add_compile_options(/utf-8 /EHsc /bigobj)
    if(PITFALL_WARNINGS)
        # C4310 (a cast truncates a constant): the logic's constants are written as the original's
        # 16-bit words, int16_t(0xffb8) for -0x48.
        add_compile_options(/W4 /permissive- /wd4310)
    endif()
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    if(PITFALL_WARNINGS)
        add_compile_options(-Wall -Wextra -Wshadow)
    endif()
endif()

# ---- asset library ---------------------------------------------------------
add_library(pitfall_ph STATIC
    src/ph/ph_stream.cpp
    src/ph/palette.cpp
    src/ph/sprite.cpp
    src/ph/level.cpp
    src/ph/manifests_data.cpp
    src/ph/arena_data.cpp
    src/ph/tables.cpp
    src/ph/tables_data.cpp
    src/ph/object_templates_data.cpp
    src/ph/slot_bindings_data.cpp
)
target_include_directories(pitfall_ph PUBLIC src)

# ---- engine ----------------------------------------------------------------
add_library(pitfall_engine STATIC
    src/engine/renderer.cpp
    src/engine/input.cpp
    src/engine/options.cpp
    src/engine/audio.cpp
    src/engine/music.cpp
    src/engine/stb_vorbis_impl.cpp
)
target_include_directories(pitfall_engine PUBLIC src)
target_link_libraries(pitfall_engine PUBLIC pitfall_ph)

# ---- game ------------------------------------------------------------------
add_library(pitfall_game STATIC
    src/game/game.cpp
    src/game/hud.cpp
    src/game/title.cpp
    src/game/cheats.cpp
    src/game/demo.cpp
    src/game/camera.cpp
    src/game/play_logic.cpp src/game/scroll.cpp src/game/harry.cpp src/game/harry_states.cpp src/game/harry_stubs.cpp src/game/harry_data.cpp src/game/objects.cpp src/game/handlers.cpp src/game/atari2600.cpp src/game/map_screen.cpp src/game/cutscene.cpp src/game/anim_scripts_data.cpp
)
target_include_directories(pitfall_game PUBLIC src)
target_link_libraries(pitfall_game PUBLIC pitfall_engine)

# ---- tools -----------------------------------------------------------------
add_executable(phdump src/tools/phdump.cpp)
target_link_libraries(phdump PRIVATE pitfall_game)

# ---- the game executable ---------------------------------------------------
if(PITFALL_USE_SDL)
    include(FetchContent)
    FetchContent_Declare(SDL2
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        release-2.30.9
        GIT_SHALLOW    TRUE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL2)
    add_executable(pitfall WIN32 src/app/main_sdl.cpp)
    target_link_libraries(pitfall PRIVATE pitfall_game SDL2::SDL2-static SDL2::SDL2main)
elseif(WIN32)
    add_executable(pitfall WIN32 src/app/main_win32.cpp src/app/win32_sheet.cpp src/app/win32_cd.cpp src/app/pitfall.rc)
    target_link_libraries(pitfall PRIVATE pitfall_game winmm gdi32 user32 shell32 comctl32)
    if(MINGW)
        target_link_options(pitfall PRIVATE -municode -static)   # (wWinMain; no MinGW DLLs to ship)
    endif()
endif()

# ---- tests -----------------------------------------------------------------
if(PITFALL_BUILD_TESTS)
    enable_testing()
    add_executable(ph_tests tests/ph_tests.cpp)
    target_link_libraries(ph_tests PRIVATE pitfall_game)
    add_test(NAME ph_tests COMMAND ph_tests ${CMAKE_SOURCE_DIR}/original)
endif()
