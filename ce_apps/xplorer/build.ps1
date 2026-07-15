Set-Location $PSScriptRoot
# Genuine Windows CE 2.11 (H/PC Pro): _WIN32_WCE=211 + subsystem 2.11. Built for
# both StrongARM SA-11x0 (ARM) and the MIPS R-series H/PC Pro devices.
$build = "$PSScriptRoot/../../tools/build_ce_app.ps1"
$src   = "main.c,xplorer_view.c,xplorer_icons.c,xplorer_desktop.c,xplorer_taskbar.c,xplorer_taskmgr.c,xplorer_run.c,xplorer_navglyph.c"

& $build -Type exe -Target xplorer.exe -Arch arm -ObjDir obj_arm `
    -Sources ($src -split ",") -Entry WinMain -Libs coredll `
    -WceVersion "211" -SubsystemVersion "2.11"

& $build -Type exe -Target xplorer.exe -Arch mips -MipsIsa mips2 -ObjDir obj_mips2 `
    -Sources ($src -split ",") -Entry WinMain -Libs coredll `
    -WceVersion "211" -SubsystemVersion "2.11"

& $build -Type exe -Target xplorer.exe -Arch mips -MipsIsa mips4 -ObjDir obj_mips4 `
    -Sources ($src -split ",") -Entry WinMain -Libs coredll
