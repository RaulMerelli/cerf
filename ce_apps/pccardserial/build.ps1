Set-Location $PSScriptRoot
$build = "$PSScriptRoot/../../tools/build_ce_app.ps1"

& $build -Type exe -Target pccardserial.exe -Arch arm -ObjDir obj_arm `
    -Sources main.cpp -Entry WinMain -Libs coredll `
    -WceVersion "100" -SubsystemVersion "1.00"

& $build -Type exe -Target pccardserial.exe -Arch mips -MipsIsa mips1 -ObjDir obj_mips1 `
    -Sources main.cpp -Entry WinMain -Libs coredll `
    -WceVersion "100" -SubsystemVersion "1.00"

& $build -Type exe -Target pccardserial.exe -Arch mips -MipsIsa mips4 -ObjDir obj_mips4 `
    -Sources main.cpp -Entry WinMain -Libs coredll
