$env:DM_GAME = "0100A3D008C5C000"
$env:DM_GAMES_DIR = "C:\Users\nando\Downloads\0100A3D008C5C000_build\profile"
$env:DM_EXE = "C:\Users\nando\Downloads\0100A3D008C5C000_build\patched\v0"
$env:DM_ROMFS = "C:\Users\nando\Downloads\0100A3D008C5C000_build\extract\v0\RomFS.img"
$env:DM_SAVE = "C:\Users\nando\Downloads\0100A3D008C5C000_build\run\save"
$env:DM_TEMP = "C:\Users\nando\Downloads\0100A3D008C5C000_build\run\temp"
$env:DM_AUDIO = "1"

Write-Host "========================================================" -ForegroundColor Cyan
Write-Host "Launching Pokemon Scarlet on Snapdragon ARM64 (Vulkan)" -ForegroundColor Green
Write-Host "========================================================" -ForegroundColor Cyan
Write-Host "Controls:"
Write-Host "  WASD: Left Stick Movement"
Write-Host "  J: A button (Confirm)"
Write-Host "  K: B button (Back / Run)"
Write-Host "  U: X button (Menu)"
Write-Host "  I: Y button"
Write-Host "  Enter: Plus (+) button"
Write-Host "  XInput Controller: Fully supported (plug and play)"
Write-Host "========================================================" -ForegroundColor Cyan

.\build_win\dmhost.exe "C:\Users\nando\Downloads\0100A3D008C5C000_build\patched\v0" $args
