@echo off
REM === GO TO PROJECT DIR ===
set "libdragonProjectPath=%~dp0"
cd /d "%libdragonProjectPath%"

REM === BUILD ===
libdragon make clean