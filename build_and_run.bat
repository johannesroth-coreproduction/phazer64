@echo off
REM === CONFIG ===
set "aresPath=C:\Data\N64\ares-windows-x64\ares-v147\ares.exe"

REM === GO TO PROJECT DIR ===
set "libdragonProjectPath=%~dp0"
cd /d "%libdragonProjectPath%"

REM === BUILD ===
libdragon make -j16
if errorlevel 1 (
    echo Build failed.
    exit /b 1
)

REM === FIND THE .Z64 FILE ===
set "z64File="
for %%f in (*.z64) do set "z64File=%%f"

REM === RUN IF BUILD SUCCEEDED ===
if defined z64File (
    start "" /wait "%aresPath%" "%libdragonProjectPath%%z64File%"
) else (
    echo Build failed: No .z64 file found.
)