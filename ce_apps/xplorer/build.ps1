Set-Location $PSScriptRoot

$SDK = "$PSScriptRoot/../../references/WindowsCE-Build-Tools"
& "$PSScriptRoot/../../tools/build_ce_app.ps1" `
    -Type exe -Target xplorer.exe `
    -Sources main.c,xplorer_view.c,xplorer_icons.c,xplorer_desktop.c,xplorer_taskbar.c,xplorer_taskmgr.c,xplorer_run.c,xplorer_navglyph.c -Entry WinMain `
    -Libs coredll `
    -SdkIncludes "$SDK/ce211-hpcpro/include" `
    -CoreLibDir "$SDK/ce211-hpcpro/lib/arm" `
    -WceVersion "211" `
    -SubsystemVersion "2.11"
