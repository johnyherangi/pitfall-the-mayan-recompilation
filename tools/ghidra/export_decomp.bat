@echo off
REM Runs Ghidra headless over PITFALL.EXE and writes re\pitfall_decomp.c (+ .symbols.txt, .data.txt)
set GHIDRA=C:\ghidra\ghidra_12.1.4_PUBLIC
set ROOT=%~dp0..\..
set PROJ=%TEMP%\pitfall_ghidra
if not exist "%PROJ%" mkdir "%PROJ%"
call "%GHIDRA%\support\analyzeHeadless.bat" "%PROJ%" pitfall -import "%ROOT%\original\PITFALL.EXE" -overwrite -scriptPath "%ROOT%\tools\ghidra" -postScript ExportDecomp.java "%ROOT%\re\pitfall_decomp.c"
echo Done. Output in %ROOT%\re\
pause
