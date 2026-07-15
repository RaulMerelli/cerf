Set-Location $PSScriptRoot
$build = "$PSScriptRoot/../../tools/build_ce_app.ps1"
$def   = "$PSScriptRoot/../cerf_guest/coredll_byname.def"

& $build -Type exe -Target CerfDemo.exe -Arch arm -ObjDir obj_arm `
    -Sources main.c,desktop.c -Entry WinMain `
    -Libs coredll -CoreDllDef $def `
    -LinkExtras "/FIXED:NO"

& $build -Type exe -Target CerfDemo.exe -Arch mips -ObjDir obj_mips `
    -Sources main.c,desktop.c -Entry WinMain `
    -Libs coredll -CoreDllDef $def `
    -LinkExtras "/FIXED:NO"
