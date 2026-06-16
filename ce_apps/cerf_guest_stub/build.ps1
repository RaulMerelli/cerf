Set-Location $PSScriptRoot

$tools   = "$PSScriptRoot/../../references/WindowsCE-Build-Tools"
$sources = @("main.cpp",
             "../cerf_guest/cerf_regs_map.cpp",
             "../cerf_guest/cerf_debug_log.cpp")
$incs    = @("$PSScriptRoot/../cerf_guest",
             "$PSScriptRoot/../cerf_guest/shim",
             "$tools/ce6-oak/INC",
             "$tools/ce42-standard/Include/Armv4i")

# Pure-ARMv4 (no-Thumb cores, e.g. SA-1110).
& "$PSScriptRoot/../../tools/build_ce_app.ps1" `
    -Type dll -Target cerf_guest_stub.dll `
    -Sources $sources -Entry DllEntryPoint `
    -ExtraIncludes $incs `
    -Libs coredll `
    -ForcedInclude "ce6_shim.h" `
    -LinkExtras "/MERGE:.rdata=.text"

# ARMV4I interworking (Thumb-capable cores).
& "$PSScriptRoot/../../tools/build_ce_app.ps1" `
    -Type dll -Target cerf_guest_stub_thumb.dll `
    -Interwork -ObjDir obj_thumb -DefFile cerf_guest_stub.def `
    -Sources $sources -Entry DllEntryPoint `
    -ExtraIncludes $incs `
    -Libs coredll `
    -ForcedInclude "ce6_shim.h" `
    -LinkExtras "/MERGE:.rdata=.text"
