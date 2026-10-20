@echo off
rem build.bat [classic^|modern^|both] [run] [pack]
rem
rem The whole build in one go, on Windows:  the original's files imported from assets-in\ (if
rem original\ has none yet), CMake configured with Visual Studio, the shell built and copied to
rem dist\<shell>\ beside this file.  "run" starts the game afterwards; "pack" copies original\
rem into dist\<shell>\ too, so that folder alone can be moved to another machine.
rem
rem   classic  the original's window, as Windows 95 showed it (the default)
rem   modern   a window of today, scaled, with an in-game menu (also for macOS and Linux: build.sh)
rem
rem Needs Visual Studio 2022 or 2026 with "Desktop development with C++" (which brings CMake);
rem ffmpeg on the path is optional (the music as Ogg instead of WAV, and the launcher's intro).
setlocal EnableDelayedExpansion
cd /d "%~dp0"
set SHELL=classic
set RUN=0
set PACK=0
for %%a in (%*) do (
    if /i "%%a"=="classic" set SHELL=classic
    if /i "%%a"=="modern" set SHELL=modern
    if /i "%%a"=="both" set SHELL=both
    if /i "%%a"=="run" set RUN=1
    if /i "%%a"=="pack" set PACK=1
)

rem ---- the tools
set CMAKE=cmake
where cmake >nul 2>nul
if errorlevel 1 (
    for %%y in (2026 2022) do for %%e in (Community Professional Enterprise BuildTools Preview) do (
        if exist "%ProgramFiles%\Microsoft Visual Studio\%%y\%%e\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
            set "CMAKE=%ProgramFiles%\Microsoft Visual Studio\%%y\%%e\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        )
    )
)
"%CMAKE%" --version >nul 2>nul
if errorlevel 1 (
    echo build.bat: CMake was not found.  Install Visual Studio 2022 or 2026 with "Desktop development
    echo with C++" ^(it includes CMake^), or CMake from cmake.org, then run this again.
    exit /b 1
)

rem ---- the original's files
if not exist original\INIT.PH if not exist original\ASSETS\INIT.PH (
    dir /b assets-in 2>nul | findstr . >nul
    if errorlevel 1 (
        echo build.bat: the original game's files are not here yet.
        echo.
        echo   Put what you have of the disc into the folder  assets-in\  next to this file:
        echo   the CD's folder, a .zip of it, an .iso, or a .cue with its .img / .bin ^(a CloneCD or
        echo   CDRWIN image, which also carries the music^).  Then run build.bat again: the build
        echo   takes the game's files, the music and the intro out of it into original\.
        echo.
        if not exist assets-in mkdir assets-in
        exit /b 1
    )
)

if "%SHELL%"=="both" (
    call :build classic || exit /b 1
    call :build modern || exit /b 1
) else (
    call :build %SHELL% || exit /b 1
)
if "%RUN%"=="1" (
    if "%SHELL%"=="both" set SHELL=classic
    echo build.bat: starting dist\!SHELL!\pitfall.exe
    start "" "dist\!SHELL!\pitfall.exe" "%~dp0original"
)
exit /b 0

:build
set S=%1
echo.
echo ==== the %S% shell
"%CMAKE%" -S . -B build-%S% -A x64 -DPITFALL_SHELL=%S% || exit /b 1
"%CMAKE%" --build build-%S% --config Release --parallel || exit /b 1
if not exist dist\%S% mkdir dist\%S%
copy /y build-%S%\Release\*.exe dist\%S%\ >nul
if exist build-%S%\Release\*.dll copy /y build-%S%\Release\*.dll dist\%S%\ >nul
if "%PACK%"=="1" (
    echo ==== copying original\ into dist\%S%\ ^(the game's files, music and intro^)
    robocopy original dist\%S%\original /e /np /nfl /ndl /njh /njs >nul
)
echo ==== dist\%S%\pitfall.exe is ready ^(it finds original\ two folders up, or beside it^)
exit /b 0
