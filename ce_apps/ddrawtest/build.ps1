Set-Location $PSScriptRoot
$tools = "$PSScriptRoot/../../references/WindowsCE-Build-Tools"
$build = "$PSScriptRoot/../../tools/build_ce_app.ps1"

& $build -Type exe -Target ddrawtest.exe -Arch arm_thumb -ObjDir obj_thumb -WceVersion 500 `
    -Sources main.c -Entry WinMain `
    -ExtraIncludes "$tools/ce6-oak/INC","$tools/ce42-standard/Include/Armv4i" `
    -Libs coredll
& $build -Type exe -Target ddrawtest.exe -Arch mips -ObjDir obj_mips -WceVersion 500 `
    -Sources main.c -Entry WinMain `
    -ExtraIncludes "$tools/ce6-oak/INC","$tools/ce42-standard/Include/Mipsiv" `
    -Libs coredll

# CE 5.0 variant: ce5-standard headers - devemu_ce5 IDirectDraw is WITH-Compact, so these
# headers' vtable macros hit the correct slots (CreateSurface=6, SetCooperativeLevel=20,
# Lock/Unlock on the surface) and define DDSCL_EXCLUSIVE. CERF_CE5_DESC (main_ce5.c) gates
# the CE5 DDSURFACEDESC + the EXCLUSIVE|FULLSCREEN coop level.
& $build -Type exe -Target ddrawtest_ce5.exe -Arch arm_thumb -ObjDir obj_ce5_thumb -WceVersion 500 `
    -Sources main_ce5.c -Entry WinMain `
    -ExtraIncludes "$tools/ce5-standard/Include/Armv4i" `
    -Libs coredll
& $build -Type exe -Target ddrawtest_ce5.exe -Arch mips -ObjDir obj_ce5_mips -WceVersion 500 `
    -Sources main_ce5.c -Entry WinMain `
    -ExtraIncludes "$tools/ce5-standard/Include/MIPSIV" `
    -Libs coredll
