Set-Location $PSScriptRoot
# Genuine Windows CE 2.11 (H/PC Pro) build for StrongARM SA-11x0, matching
# ce_apps/romdump's toolchain: CE 2.11 headers + CE 2.11 coredll import lib +
# _WIN32_WCE=211 + subsystem 2.11. Linking the real 2.11 coredll.lib makes the
# by-ordinal imports resolve against the device's own coredll. Plain ARMV4.
$SDK = "$PSScriptRoot/../../references/WindowsCE-Build-Tools"
& "$PSScriptRoot/../../tools/build_ce_app.ps1" `
    -Type exe -Target blankapp.exe `
    -Sources main.cpp -Entry WinMain `
    -Libs coredll `
    -SdkIncludes "$SDK/ce211-hpcpro/include" `
    -CoreLibDir "$SDK/ce211-hpcpro/lib/arm" `
    -WceVersion "211" `
    -SubsystemVersion "2.11"
