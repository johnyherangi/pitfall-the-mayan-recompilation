@echo off
REM Decompiles the object-type handlers (0x405170 dispatcher + table at 0x462350) into re\pitfall_handlers.c
set GHIDRA=C:\ghidra\ghidra_12.1.4_PUBLIC
set ROOT=%~dp0..\..
set PROJ=%TEMP%\pitfall_ghidra
if not exist "%PROJ%" mkdir "%PROJ%"
call "%GHIDRA%\support\analyzeHeadless.bat" "%PROJ%" pitfall -import "%ROOT%\original\PITFALL.EXE" -overwrite -scriptPath "%ROOT%\tools\ghidra" -postScript ExportHandlers.java "%ROOT%\re\pitfall_handlers.c"
echo Done. Output in %ROOT%\re\pitfall_handlers.c
pause
