Set-Location $PSScriptRoot
$tools = "$PSScriptRoot/../../references/WindowsCE-Build-Tools"
& "$PSScriptRoot/../../tools/build_ce_app.ps1" `
    -Type exe -Target ddrawtest.exe `
    -Interwork -WceVersion 500 `
    -Sources main.c -Entry WinMain `
    -ExtraIncludes "$tools/ce6-oak/INC","$tools/ce42-standard/Include/Armv4i" `
    -Libs coredll
