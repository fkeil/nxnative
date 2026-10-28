@echo off
setlocal
cd /d "%~dp0"

set "DM_GAME=0100A3D008C5C000"
set "DM_GAMES_DIR=C:\Users\nando\Downloads\0100A3D008C5C000_build\profile"
set "DM_EXE=C:\Users\nando\Downloads\0100A3D008C5C000_build\patched\v0"
set "DM_ROMFS=C:\Users\nando\Downloads\0100A3D008C5C000_build\extract\v0\RomFS.img"
set "DM_SAVE=C:\Users\nando\Downloads\0100A3D008C5C000_build\run\save"
set "DM_TEMP=C:\Users\nando\Downloads\0100A3D008C5C000_build\run\temp"
set "DM_AUDIO=1"
set "DM_OPMODE=0"
set "DM_KEYS=f60:A,f100:Up~0.2,f130:A~0.2,f170:A,f210:A,f250:A,f300:A,f350:A,f400:A,f500:A,f600:A,f700:A,f800:A,f900:A,f1000:A,f1100:A"
set "DM_AUTO_A=0"
set "DM_MAX_DRAWS="
set "DM_NVN_SHOT=60,120,200,350,500,700,900,1200:shot_vk_%%d.bmp"

if "%DM_AUDIO_DEV%"=="" (
    rem Change to 1 for Odyssey G93SD monitor, 4 for laptop speakers, 0 for Jabra headset
    set "DM_AUDIO_DEV=1"
)

echo ========================================================
echo Launching Pokemon Scarlet on Snapdragon ARM64 (Vulkan)
echo ========================================================
echo Audio output device ID: %DM_AUDIO_DEV%
echo   0: Headphones (Jabra Evolve 75 SE)
echo   1: Odyssey G93SD Monitor
echo   4: Laptop Speakers (Qualcomm Aqstic)
echo   To change, run: set DM_AUDIO_DEV=4 ^& run_pokemon.bat
echo ========================================================
echo Controls:
echo   WASD: Left Stick Movement
echo   J: A button (Confirm)
echo   K: B button (Back / Run)
echo   U: X button (Menu)
echo   I: Y button
echo   Enter: Plus (+) button
echo   XInput Controller: Fully supported (plug and play)
echo ========================================================

build_win\dmhost.exe "C:\Users\nando\Downloads\0100A3D008C5C000_build\patched\v0" %*
pause
