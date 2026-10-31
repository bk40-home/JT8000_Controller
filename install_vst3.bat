@echo off
rem Install the freshly built JT-8000 Controller VST3 where Ableton Live loads it.
rem Run with right-click > "Run as administrator" (Program Files needs it). Close Live first.
set SRC=%~dp0build\JT8000_Controller_artefacts\Release\VST3\JT-8000 Controller.vst3\Contents\x86_64-win\JT-8000 Controller.vst3
set DST=C:\Program Files\Common Files\VST3\JT-8000 Controller.vst3
copy /y "%SRC%" "%DST%"
if errorlevel 1 (echo COPY FAILED - run as administrator, and close Live) else (echo Installed: %DST%)
pause
