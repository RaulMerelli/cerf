Set-Location $PSScriptRoot
$build = "$PSScriptRoot/../../tools/build_ce_app.ps1"
$src   = "main.cpp,step_preset.cpp,step_config.cpp,step_dump.cpp,dump.cpp"

& $build -Type exe -Target romdump.exe -Arch arm -ObjDir obj_arm `
    -Sources ($src -split ",") -Entry WinMain -Libs coredll -Rc romdump.rc `
    -WceVersion "100" -SubsystemVersion "1.00"

& $build -Type exe -Target romdump.exe -Arch mips -MipsIsa mips1 -ObjDir obj_mips1 `
    -Sources ($src -split ",") -Entry WinMain -Libs coredll -Rc romdump.rc `
    -WceVersion "100" -SubsystemVersion "1.00"

& $build -Type exe -Target romdump.exe -Arch mips -MipsIsa mips4 -ObjDir obj_mips4 `
    -Sources ($src -split ",") -Entry WinMain -Libs coredll -Rc romdump.rc
