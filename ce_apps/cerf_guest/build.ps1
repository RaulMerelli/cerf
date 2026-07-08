Set-Location $PSScriptRoot

$tools   = "$PSScriptRoot/../../references/WindowsCE-Build-Tools"
$sources = @("main.cpp","cerf_virt_base.cpp","cerf_regs_map.cpp","cerf_debug_log.cpp","cerf_ddgpe.cpp",
             "cerf_ddgpe_vidmem.cpp",
             "cerf_ddgpe_blt.cpp","cerf_ddhal.cpp","cerf_ddhal_ce5.cpp","cerf_gradient.cpp",
             "cerf_pointer_pump.cpp","cerf_keyboard_pump.cpp","cerf_resize_pump.cpp","cerf_task_manager_pump.cpp",
             "cerf_power.cpp",
             "cerf_cursor.cpp","cerf_ctbltstub.cpp","cerf_drvfnstubs.cpp",
             "cerf_cacherangeflush.cpp","cerf_cesetextendedpdata.cpp","cerf_getversionexw.cpp",
             "cerf_driver_in_driver.cpp","cerf_fs_afs.cpp","cerf_fs_transport.cpp",
             "cerf_fs_vol.cpp","cerf_fs_file.cpp","cerf_fs_find.cpp","cerf_fs_notify.cpp")
$libs    = @("coredll","ddgpe","gpe_lib","emul","emulrotate","genblt","genblt_cpu","aablt")
$baseInc = @("$PSScriptRoot/shim","$tools/ce6-oak/INC")

# Pure-ARMv4 (no-Thumb cores, e.g. SA-1110) against the rebuilt Armv4 OAK libs.
& "$PSScriptRoot/../../tools/build_ce_app.ps1" `
    -Type dll -Target cerf_guest.dll -Arch arm -ObjDir obj_arm `
    -Sources $sources -Entry DllEntryPoint `
    -ExtraIncludes ($baseInc + "$tools/ce42-standard/Include/Armv4i") `
    -ExtraLibPaths "$tools/ce6-oak/Lib/Armv4/retail" `
    -Libs $libs `
    -CoreDllDef "$PSScriptRoot/coredll_byname.def" `
    -ForcedInclude "ce6_shim.h","cerf_debug_log.h" `
    -LinkExtras "/MERGE:.rdata=.text"

# ARMV4I interworking (Thumb-capable cores) against the stock Armv4i OAK libs.
& "$PSScriptRoot/../../tools/build_ce_app.ps1" `
    -Type dll -Target cerf_guest.dll -Arch arm_thumb -ObjDir obj_thumb -DefFile cerf_guest.def `
    -Sources $sources -Entry DllEntryPoint `
    -ExtraIncludes ($baseInc + "$tools/ce42-standard/Include/Armv4i") `
    -ExtraLibPaths "$tools/ce6-oak/Lib/Armv4i/retail" `
    -Libs $libs `
    -CoreDllDef "$PSScriptRoot/coredll_byname.def" `
    -ForcedInclude "ce6_shim.h","cerf_debug_log.h" `
    -LinkExtras "/MERGE:.rdata=.text"

& "$PSScriptRoot/../../tools/build_ce_app.ps1" `
    -Type dll -Target cerf_guest.dll -Arch mips -MipsIsa mips4 -ObjDir obj_mips4 -DefFile cerf_guest.def `
    -Sources $sources -Entry DllEntryPoint `
    -ExtraIncludes ($baseInc + "$tools/ce42-standard/Include/Mipsiv") `
    -ExtraLibPaths "$tools/ce6-oak/Lib/Mipsiv/retail" `
    -Libs $libs `
    -CoreDllDef "$PSScriptRoot/coredll_byname.def" `
    -ForcedInclude "ce6_shim.h","cerf_debug_log.h" `
    -LinkExtras "/MERGE:.rdata=.text"

& "$PSScriptRoot/../../tools/build_ce_app.ps1" `
    -Type dll -Target cerf_guest.dll -Arch mips -MipsIsa mips2 -ObjDir obj_mips2 -DefFile cerf_guest.def `
    -Sources $sources -Entry DllEntryPoint `
    -ExtraIncludes ($baseInc + "$tools/ce42-standard/Include/Mipsii") `
    -ExtraLibPaths "$tools/ce6-oak/Lib/Mipsii/retail" `
    -Libs $libs `
    -CoreDllDef "$PSScriptRoot/coredll_byname.def" `
    -ForcedInclude "ce6_shim.h","cerf_debug_log.h" `
    -LinkExtras "/MERGE:.rdata=.text"

# CE 2.x MIPS-II body: static CE 2.x libc CRT + a coredll def trimmed of the
# CRT/soft-FP/C++ runtime; separate from the stock mips2 body (CE3+).
& "$PSScriptRoot/../../tools/build_ce_app.ps1" `
    -Type dll -Target cerf_guest.dll -Arch mips -MipsIsa mips2 -ObjDir obj_mips2_ce2 -DefFile cerf_guest.def `
    -Sources $sources -Entry DllEntryPoint -OutSubdir mips2_ce2 `
    -ExtraIncludes ($baseInc + "$tools/ce42-standard/Include/Mipsii") `
    -ExtraLibPaths "$tools/ce6-oak/Lib/Mipsii/retail","$tools/ce2_toolkits_beta/WCE/LIB/WCE100/WCEMIPS" `
    -Libs $libs `
    -CoreDllDef "$PSScriptRoot/coredll_ce2.def" `
    -CrtLibs libc `
    -ForcedInclude "ce6_shim.h","cerf_debug_log.h" `
    -LinkExtras "/MERGE:.rdata=.text"
