@echo off
setlocal
cd /d "%~dp0"
set "DM_GAME=0100A3D008C5C000"
set "DM_GAMES_DIR=C:\Users\nando\Downloads\0100A3D008C5C000_build\profile"
set "DM_EXE=C:\Users\nando\Downloads\0100A3D008C5C000_build\patched\v0"
set "DM_ROMFS=C:\Users\nando\Downloads\0100A3D008C5C000_build\extract\v0\RomFS.img"
set "DM_SAVE=C:\Users\nando\Downloads\0100A3D008C5C000_build\run\save"
set "DM_TEMP=C:\Users\nando\Downloads\0100A3D008C5C000_build\run\temp"
set "DM_AUDIO=0"
set "DM_OPMODE=0"
set "DM_FRAMES=400"
set "DM_KEYS=f60:A,f100:Up~0.2,f130:A~0.2,f170:A,f210:A,f250:A,f300:A,f350:A"
set "DM_NVN_SHOT=60,100,120,150,200,280,350,390:shot_vk_%%d.bmp"
build_win\dmhost.exe "C:\Users\nando\Downloads\0100A3D008C5C000_build\patched\v0" 2>&1
