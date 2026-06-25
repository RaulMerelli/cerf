Set-Location $PSScriptRoot
$tools = "$PSScriptRoot/../../references/WindowsCE-Build-Tools"
$build = "$PSScriptRoot/../../tools/build_ce_app.ps1"

& $build -Type exe -Target CerfDemo.exe -Arch arm -ObjDir obj_arm `
    -Sources main.c,desktop.c -Entry WinMain `
    -ExtraIncludes "$tools/ce5-standard/Include/Armv4i" `
    -ExtraLibPaths "$tools/ce5-standard/Lib/Armv4i" `
    -Libs coredll `
    -LinkExtras "/FIXED:NO"

& $build -Type exe -Target CerfDemo.exe -Arch mips -ObjDir obj_mips `
    -Sources main.c,desktop.c -Entry WinMain `
    -ExtraIncludes "$tools/ce5-standard/Include/MIPSIV" `
    -ExtraLibPaths "$tools/ce5-standard/Lib/MIPSIV" `
    -Libs coredll `
    -LinkExtras "/FIXED:NO"
