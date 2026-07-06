Set-Location $PSScriptRoot
$SDK   = "$PSScriptRoot/../../references/WindowsCE-Build-Tools"
$build = "$PSScriptRoot/../../tools/build_ce_app.ps1"

& $build -Type exe -Target romdump.exe -Arch arm -ObjDir obj_arm `
    -Sources main.cpp,dump.cpp,paint.cpp -Entry WinMain -Libs coredll -Rc romdump.rc `
    -SdkIncludes "$SDK/ce211-hpcpro/include" `
    -CoreLibDir "$SDK/ce211-hpcpro/lib/arm" `
    -WceVersion "100" -SubsystemVersion "1.00"

& $build -Type exe -Target romdump.exe -Arch mips -MipsIsa mips1 -ObjDir obj_mips1 `
    -Sources main.cpp,dump.cpp,paint.cpp -Entry WinMain -Libs coredll -Rc romdump.rc `
    -SdkIncludes "$SDK/ce2_toolkits_beta/WCE/INCLUDE/WCE200" `
    -CoreLibDir "$SDK/ce2_toolkits_beta/WCE/LIB/WCE200/WCEMIPS" `
    -CrtLibs libc `
    -WceVersion "200" -SubsystemVersion "1.00"

& $build -Type exe -Target romdump.exe -Arch mips -MipsIsa mips4 -ObjDir obj_mips4 `
    -Sources main.cpp,dump.cpp,paint.cpp -Entry WinMain -Libs coredll -Rc romdump.rc
