Set-Location $PSScriptRoot
$build = "$PSScriptRoot/../../tools/build_ce_app.ps1"
& $build -Type exe -Target sampleapp.exe -Arch arm -ObjDir obj_arm `
    -Sources main.c -Entry WinMain -Libs coredll,commctrl,commdlg
& $build -Type exe -Target sampleapp.exe -Arch mips -ObjDir obj_mips `
    -Sources main.c -Entry WinMain -Libs coredll,commctrl,commdlg
