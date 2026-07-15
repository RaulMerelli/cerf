Set-Location $PSScriptRoot

$sources = @("main.cpp",
             "../cerf_guest/cerf_virt_base.cpp",
             "../cerf_guest/cerf_regs_map.cpp",
             "../cerf_guest/cerf_debug_log.cpp")
$baseInc = @("$PSScriptRoot/../cerf_guest",
             "$PSScriptRoot/../cerf_guest/include")
$build   = "$PSScriptRoot/../../tools/build_ce_app.ps1"

# Pure-ARMv4 (no-Thumb cores, e.g. SA-1110).
& $build `
    -Type dll -Target cerf_guest_stub.dll -Arch arm -ObjDir obj_arm `
    -Sources $sources -Entry DllEntryPoint `
    -ExtraIncludes $baseInc `
    -Libs coredll `
    -CoreDllDef "$PSScriptRoot/../cerf_guest/coredll_byname.def" `
    -LinkExtras "/MERGE:.rdata=.text"

# ARMV4I interworking (Thumb-capable cores).
& $build `
    -Type dll -Target cerf_guest_stub.dll -Arch arm_thumb -ObjDir obj_thumb -DefFile cerf_guest_stub.def `
    -Sources $sources -Entry DllEntryPoint `
    -ExtraIncludes $baseInc `
    -Libs coredll `
    -CoreDllDef "$PSScriptRoot/../cerf_guest/coredll_byname.def" `
    -LinkExtras "/MERGE:.rdata=.text"

& $build `
    -Type dll -Target cerf_guest_stub.dll -Arch mips -MipsIsa mips4 -ObjDir obj_mips4 -DefFile cerf_guest_stub.def `
    -Sources $sources -Entry DllEntryPoint `
    -ExtraIncludes $baseInc `
    -Libs coredll `
    -CoreDllDef "$PSScriptRoot/../cerf_guest/coredll_byname.def" `
    -LinkExtras "/MERGE:.rdata=.text"

& $build `
    -Type dll -Target cerf_guest_stub.dll -Arch mips -MipsIsa mips2 -ObjDir obj_mips2 -DefFile cerf_guest_stub.def `
    -Sources $sources -Entry DllEntryPoint `
    -ExtraIncludes $baseInc `
    -Libs coredll `
    -CoreDllDef "$PSScriptRoot/../cerf_guest/coredll_byname.def" `
    -LinkExtras "/MERGE:.rdata=.text"

# CE 2.x MIPS-II stub, staged beside the CE 2.x body so ArchDir resolves both
# from mips2_ce2.
& $build `
    -Type dll -Target cerf_guest_stub.dll -Arch mips -MipsIsa mips2 -ObjDir obj_mips2_ce2 -DefFile cerf_guest_stub.def `
    -Sources $sources -Entry DllEntryPoint -OutSubdir mips2_ce2 `
    -ExtraIncludes $baseInc `
    -Libs coredll `
    -CoreDllDef "$PSScriptRoot/../cerf_guest/coredll_byname.def" `
    -LinkExtras "/MERGE:.rdata=.text"
