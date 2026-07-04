Set-Location $PSScriptRoot
# CE 2.11 (H/PC Pro) headers + CE 2.11 coredll import lib + _WIN32_WCE=211.
# Subsystem stamped 1.00 (lowest) so the binary also loads on the older CE 1.x/2.0
# kernels of the Velo/Nino. Built for StrongARM SA-11x0 (ARM) and the MIPS-I
# R3000/TX39 devices (PR31500/PR31700); the CE 2.11 headers are arch-shared.
$SDK   = "$PSScriptRoot/../../references/WindowsCE-Build-Tools"
$build = "$PSScriptRoot/../../tools/build_ce_app.ps1"

& $build -Type exe -Target romdump.exe -Arch arm -ObjDir obj_arm `
    -Sources main.cpp,dump.cpp,paint.cpp -Entry WinMain -Libs coredll -Rc romdump.rc `
    -SdkIncludes "$SDK/ce211-hpcpro/include" `
    -CoreLibDir "$SDK/ce211-hpcpro/lib/arm" `
    -WceVersion "100" -SubsystemVersion "1.00"

& $build -Type exe -Target romdump.exe -Arch mips -MipsIsa mips1 -ObjDir obj_mips1 `
    -Sources main.cpp,dump.cpp,paint.cpp -Entry WinMain -Libs coredll -Rc romdump.rc `
    -SdkIncludes "$SDK/ce211-hpcpro/include" `
    -CoreLibDir "$SDK/ce211-hpcpro/lib/mips" `
    -WceVersion "100" -SubsystemVersion "1.00"

& $build -Type exe -Target romdump.exe -Arch mips -MipsIsa mips4 -ObjDir obj_mips4 `
    -Sources main.cpp,dump.cpp,paint.cpp -Entry WinMain -Libs coredll -Rc romdump.rc
