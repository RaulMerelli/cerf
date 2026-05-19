Set-Location $PSScriptRoot
& "$PSScriptRoot/../../tools/build_ce_app.ps1" `
    -Type exe -Target sampleapp.exe `
    -Sources main.c -Entry WinMain `
    -Libs coredll,commctrl,commdlg
