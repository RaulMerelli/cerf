Set-Location $PSScriptRoot
# Genuine Windows CE 2.11 (H/PC Pro): CE 2.11 headers + CE 2.11 coredll import
# lib + _WIN32_WCE=211 + subsystem 2.11. Built for both StrongARM SA-11x0 (ARM)
# and the MIPS R-series H/PC Pro devices; the CE 2.11 headers are arch-shared.
$SDK   = "$PSScriptRoot/../../references/WindowsCE-Build-Tools"
$build = "$PSScriptRoot/../../tools/build_ce_app.ps1"
$src   = "main.c,xplorer_view.c,xplorer_icons.c,xplorer_desktop.c,xplorer_taskbar.c,xplorer_taskmgr.c,xplorer_run.c,xplorer_navglyph.c"

& $build -Type exe -Target xplorer.exe -Arch arm -ObjDir obj_arm `
    -Sources ($src -split ",") -Entry WinMain -Libs coredll `
    -SdkIncludes "$SDK/ce211-hpcpro/include" `
    -CoreLibDir "$SDK/ce211-hpcpro/lib/arm" `
    -WceVersion "211" -SubsystemVersion "2.11"

& $build -Type exe -Target xplorer.exe -Arch mips -MipsIsa mips2 -ObjDir obj_mips `
    -Sources ($src -split ",") -Entry WinMain -Libs coredll `
    -SdkIncludes "$SDK/ce211-hpcpro/include" `
    -CoreLibDir "$SDK/ce211-hpcpro/lib/mips" `
    -WceVersion "211" -SubsystemVersion "2.11"
