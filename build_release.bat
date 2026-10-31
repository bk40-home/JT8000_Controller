@echo off
rem Rebuild the JT8000 plugin (Release) - the day-to-day rebuild from Instructions.txt.
rem Output goes to build_log.txt next to this file; "BUILD EXIT 0" at the end = success.
cd /d "%~dp0build"
echo Building JT8000 Controller (Release)... see build_log.txt
cmake --build . --config Release > "%~dp0build_log.txt" 2>&1
echo BUILD EXIT %ERRORLEVEL%>> "%~dp0build_log.txt"
echo Done: BUILD EXIT %ERRORLEVEL%
timeout /t 5 >nul
