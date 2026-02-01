@echo off
REM === CONFIG ===
set "sc64Path=C:\Data\N64\sc64-deployer-windows-v2.20.2\sc64deployer.exe"
REM set "sc64Path=C:\Data\N64\UNFLoader.exe"

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
    "%sc64Path%" upload --reboot --tv ntsc "%libdragonProjectPath%%z64File%"
	REM "%sc64Path%" -d -r "%libdragonProjectPath%%z64File%"
	"%sc64Path%" debug
) else (
    echo Build failed: No .z64 file found.
)