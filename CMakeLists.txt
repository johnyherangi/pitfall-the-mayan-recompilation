cmake_minimum_required(VERSION 3.21)
project(PitfallMayanRecompilation LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Pitfall: The Mayan Adventure (Windows 95, Kinesoft/Activision 1995)
# Reconstruction as a modern C++20 code base.
#
#   pitfall_ph      - .PH asset stream parsers (sprites, levels, sounds, palette)
#   pitfall_engine  - software renderer, input, options, audio mixer, timing
#   pitfall_game    - game flow, display list, HUD, attract/demo playback
#   phdump          - command line asset extractor / frame renderer (portable)
#   pitfall         - the game executable (Win32/GDI front end, like the original;
#                     the modern SDL2 shell with -DPITFALL_SHELL=modern)
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

# The shell: "classic" is the original's window (Win32: its menu bar, status bar, property sheet,
# dialogs and the "256 color mode" warning; Windows only), "modern" the SDL2 front end for Windows,
# macOS and Linux (an in-game menu, whole-pixel scaling, full screen).  The game under them is one.
set(PITFALL_SHELL "" CACHE STRING "The shell: classic (Win32, the default there) or modern (SDL2, the default elsewhere)")
option(PITFALL_USE_SDL     "(the old name of PITFALL_SHELL=modern)" OFF)
if(PITFALL_SHELL STREQUAL "")
    if(PITFALL_USE_SDL OR NOT WIN32)
        set(PITFALL_SHELL "modern")
    else()
        set(PITFALL_SHELL "classic")
    endif()
endif()
if(PITFALL_SHELL STREQUAL "classic" AND NOT WIN32)
    message(FATAL_ERROR "The classic shell is the original's Windows window: build it on Windows, or use -DPITFALL_SHELL=modern")
endif()
option(PITFALL_BUILD_TESTS "Build the asset round-trip tests" ON)
option(PITFALL_WARNINGS    "Enable strict compiler warnings" ON)

# The warnings go on the project's own targets (pitfall_warnings), not on SDL's sources.
if(MSVC)
    add_compile_options(/utf-8 /EHsc /bigobj)
    # C4310 (a cast truncates a constant): the logic's constants are written as the original's
    # 16-bit words, int16_t(0xffb8) for -0x48.
    set(PITFALL_WARNING_FLAGS /W4 /permissive- /wd4310)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    set(PITFALL_WARNING_FLAGS -Wall -Wextra -Wshadow)
endif()
function(pitfall_warnings target)
    if(PITFALL_WARNINGS)
        target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${PITFALL_WARNING_FLAGS}>)
    endif()
endfunction()

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
pitfall_warnings(pitfall_ph)
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
pitfall_warnings(pitfall_engine)
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
pitfall_warnings(pitfall_game)
target_include_directories(pitfall_game PUBLIC src)
target_link_libraries(pitfall_game PUBLIC pitfall_engine)

# ---- tools -----------------------------------------------------------------
add_executable(phdump src/tools/phdump.cpp)
target_link_libraries(phdump PRIVATE pitfall_game)
pitfall_warnings(phdump)

# ---- the original's files from the disc ------------------------------------
# phimport takes the game's files, music and intro out of the CD's folder, a .zip, an .iso or a
# .cue / .img image.  Put what you have in assets-in/ (PITFALL_ASSETS_DIR): the build fills
# original/ from it when INIT.PH is not there yet (the import_assets target does it again on demand).
add_executable(phimport src/tools/phimport.cpp third_party/miniz/miniz.c)
target_include_directories(phimport PRIVATE third_party/miniz)
pitfall_warnings(phimport)
set(PITFALL_ASSETS_DIR "${CMAKE_SOURCE_DIR}/assets-in" CACHE PATH "Where the original's disc, image, zip or folder is dropped for phimport")
set(PITFALL_DATA_DIR "${CMAKE_SOURCE_DIR}/original" CACHE PATH "Where the game's files go (and are run from)")
add_custom_target(import_assets
    COMMAND phimport "${PITFALL_ASSETS_DIR}" "${PITFALL_DATA_DIR}"
    DEPENDS phimport
    COMMENT "The original's files from ${PITFALL_ASSETS_DIR} into ${PITFALL_DATA_DIR}"
    VERBATIM)
file(GLOB _assets_in "${PITFALL_ASSETS_DIR}/*")
if(_assets_in AND NOT EXISTS "${PITFALL_DATA_DIR}/INIT.PH" AND NOT EXISTS "${PITFALL_DATA_DIR}/init.ph")
    set_target_properties(import_assets PROPERTIES EXCLUDE_FROM_ALL FALSE)
    add_custom_command(TARGET import_assets POST_BUILD COMMAND ${CMAKE_COMMAND} -E echo "import_assets: done")
    message(STATUS "assets-in/ has files and ${PITFALL_DATA_DIR} has no INIT.PH: the build will import them")
else()
    set_target_properties(import_assets PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()

# ---- the game executable ---------------------------------------------------
if(PITFALL_SHELL STREQUAL "modern")
    include(FetchContent)
    FetchContent_Declare(SDL2
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        release-2.30.9
        GIT_SHALLOW    TRUE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL2)
    add_executable(pitfall WIN32 MACOSX_BUNDLE src/app/main_sdl.cpp)
    target_link_libraries(pitfall PRIVATE pitfall_game SDL2::SDL2-static SDL2::SDL2main)
    pitfall_warnings(pitfall)
    if(MINGW)
        target_link_options(pitfall PRIVATE -static)   # (no MinGW DLLs to ship)
    endif()
    if(APPLE)
        set_target_properties(pitfall PROPERTIES
            OUTPUT_NAME "Pitfall"
            MACOSX_BUNDLE_BUNDLE_NAME "Pitfall: The Mayan Adventure"
            MACOSX_BUNDLE_GUI_IDENTIFIER "com.activision.pitfall.recompilation"
            MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
            MACOSX_BUNDLE_ICON_FILE "pitfall.icns")
        target_sources(pitfall PRIVATE src/app/pitfall.icns)
        set_source_files_properties(src/app/pitfall.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    endif()
elseif(PITFALL_SHELL STREQUAL "classic")
    add_executable(pitfall WIN32 src/app/main_win32.cpp src/app/win32_sheet.cpp src/app/win32_cd.cpp src/app/win32_gamepad.cpp src/app/pitfall.rc)
    target_link_libraries(pitfall PRIVATE pitfall_game winmm gdi32 user32 shell32 comctl32)
    pitfall_warnings(pitfall)
    if(MINGW)
        target_link_options(pitfall PRIVATE -municode -static)   # (wWinMain; no MinGW DLLs to ship)
    endif()
endif()

# The CD's launcher (PITSTART.EXE), with its dialogs, pictures and sound read from the CD's files.
if(WIN32)
    add_executable(pitstart WIN32 src/app/pitstart_win32.cpp)
    target_link_libraries(pitstart PRIVATE winmm vfw32 gdi32 user32 shell32)
    pitfall_warnings(pitstart)
    if(MINGW)
        target_link_options(pitstart PRIVATE -static)
    endif()
endif()

# ---- tests -----------------------------------------------------------------
if(PITFALL_BUILD_TESTS)
    enable_testing()
    add_executable(ph_tests tests/ph_tests.cpp)
    target_link_libraries(ph_tests PRIVATE pitfall_game)
    pitfall_warnings(ph_tests)
    if(MSVC)
        target_link_options(ph_tests PRIVATE /STACK:8388608)   # (the tests keep their Games, 135 KB each, on the stack)
    endif()
    add_test(NAME ph_tests COMMAND ph_tests ${CMAKE_SOURCE_DIR}/original)
endif()
