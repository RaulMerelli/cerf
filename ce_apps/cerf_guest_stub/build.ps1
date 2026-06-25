Set-Location $PSScriptRoot

$tools   = "$PSScriptRoot/../../references/WindowsCE-Build-Tools"
$sources = @("main.cpp",
             "../cerf_guest/cerf_regs_map.cpp",
             "../cerf_guest/cerf_debug_log.cpp")
$baseInc = @("$PSScriptRoot/../cerf_guest",
             "$PSScriptRoot/../cerf_guest/shim",
             "$tools/ce6-oak/INC")

# Pure-ARMv4 (no-Thumb cores, e.g. SA-1110).
& "$PSScriptRoot/../../tools/build_ce_app.ps1" `
    -Type dll -Target cerf_guest_stub.dll -Arch arm -ObjDir obj_arm `
    -Sources $sources -Entry DllEntryPoint `
    -ExtraIncludes ($baseInc + "$tools/ce42-standard/Include/Armv4i") `
    -Libs coredll `
    -ForcedInclude "ce6_shim.h" `
    -LinkExtras "/MERGE:.rdata=.text"

# ARMV4I interworking (Thumb-capable cores).
& "$PSScriptRoot/../../tools/build_ce_app.ps1" `
    -Type dll -Target cerf_guest_stub.dll -Arch arm_thumb -ObjDir obj_thumb -DefFile cerf_guest_stub.def `
    -Sources $sources -Entry DllEntryPoint `
    -ExtraIncludes ($baseInc + "$tools/ce42-standard/Include/Armv4i") `
    -Libs coredll `
    -ForcedInclude "ce6_shim.h" `
    -LinkExtras "/MERGE:.rdata=.text"

& "$PSScriptRoot/../../tools/build_ce_app.ps1" `
    -Type dll -Target cerf_guest_stub.dll -Arch mips -MipsIsa mips4 -ObjDir obj_mips4 -DefFile cerf_guest_stub.def `
    -Sources $sources -Entry DllEntryPoint `
    -ExtraIncludes ($baseInc + "$tools/ce42-standard/Include/Mipsiv") `
    -Libs coredll `
    -ForcedInclude "ce6_shim.h" `
    -LinkExtras "/MERGE:.rdata=.text"

& "$PSScriptRoot/../../tools/build_ce_app.ps1" `
    -Type dll -Target cerf_guest_stub.dll -Arch mips -MipsIsa mips2 -ObjDir obj_mips2 -DefFile cerf_guest_stub.def `
    -Sources $sources -Entry DllEntryPoint `
    -ExtraIncludes ($baseInc + "$tools/ce42-standard/Include/Mipsii") `
    -Libs coredll `
    -ForcedInclude "ce6_shim.h" `
    -LinkExtras "/MERGE:.rdata=.text"
