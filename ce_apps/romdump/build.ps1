Set-Location $PSScriptRoot
$SDK   = "$PSScriptRoot/../../references/WindowsCE-Build-Tools"
$build = "$PSScriptRoot/../../tools/build_ce_app.ps1"

& $build -Type exe -Target romdump.exe -Arch arm -ObjDir obj_arm `
    -Sources main.cpp,step_preset.cpp,step_config.cpp,step_dump.cpp,dump.cpp -Entry WinMain -Libs coredll -Rc romdump.rc `
    -SdkIncludes "$SDK/ce211-hpcpro/include" `
    -CoreLibDir "$SDK/ce211-hpcpro/lib/arm" `
    -WceVersion "100" -SubsystemVersion "1.00"

& $build -Type exe -Target romdump.exe -Arch mips -MipsIsa mips1 -ObjDir obj_mips1 `
    -Sources main.cpp,step_preset.cpp,step_config.cpp,step_dump.cpp,dump.cpp -Entry WinMain -Libs coredll -Rc romdump.rc `
    -SdkIncludes "$SDK/ce2_toolkits_beta/WCE/INCLUDE/WCE100" `
    -CoreLibDir "$SDK/ce2_toolkits_beta/WCE/LIB/WCE100/WCEMIPS" `
    -CrtLibs libc `
    -WceVersion "100" -SubsystemVersion "1.00"

& $build -Type exe -Target romdump.exe -Arch mips -MipsIsa mips4 -ObjDir obj_mips4 `
    -Sources main.cpp,step_preset.cpp,step_config.cpp,step_dump.cpp,dump.cpp -Entry WinMain -Libs coredll -Rc romdump.rc
