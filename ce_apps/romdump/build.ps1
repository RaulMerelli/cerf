Set-Location $PSScriptRoot
# Genuine Windows CE 2.11 (H/PC Pro) build for StrongARM SA-11x0: CE 2.11
# headers + CE 2.11 coredll import lib + _WIN32_WCE=211 + subsystem 2.11.
# All CE 2.11 SDK bits live in the WindowsCE-Build-Tools submodule (ce211-*),
# alongside the ce3/ce42 sets, so this builds on CI and any clone. Linking the
# real 2.11 coredll.lib makes the by-ordinal imports resolve against the
# device's own coredll. Plain ARMV4, no Thumb.
$SDK = "$PSScriptRoot/../../references/WindowsCE-Build-Tools"
& "$PSScriptRoot/../../tools/build_ce_app.ps1" `
    -Type exe -Target romdump.exe `
    -Sources main.cpp,dump.cpp,paint.cpp -Entry WinMain `
    -Libs coredll `
    -Rc romdump.rc `
    -SdkIncludes "$SDK/ce211-hpcpro/include" `
    -CoreLibDir "$SDK/ce211-hpcpro/lib/arm" `
    -WceVersion "211" `
    -SubsystemVersion "2.11"
