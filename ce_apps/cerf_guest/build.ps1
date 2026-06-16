Set-Location $PSScriptRoot

$tools   = "$PSScriptRoot/../../references/WindowsCE-Build-Tools"
$sources = @("main.cpp","cerf_regs_map.c","cerf_debug_log.cpp","cerf_ddgpe.cpp",
             "cerf_ddgpe_vidmem.cpp",
             "cerf_ddgpe_blt.cpp","cerf_ddhal.cpp","cerf_ddhal_ce5.cpp","cerf_gradient.cpp",
             "cerf_pointer_pump.cpp","cerf_keyboard_pump.cpp","cerf_resize_pump.cpp","cerf_task_manager_pump.cpp",
             "cerf_power.cpp",
             "cerf_cursor.cpp","cerf_ctbltstub.cpp","cerf_drvfnstubs.cpp",
             "cerf_cacherangeflush.cpp","cerf_cesetextendedpdata.cpp",
             "cerf_driver_in_driver.cpp","cerf_fs_afs.c","cerf_fs_transport.c",
             "cerf_fs_vol.c","cerf_fs_file.c","cerf_fs_find.c","cerf_fs_notify.c")
$libs    = @("coredll","ddgpe","gpe_lib","emul","emulrotate","genblt","genblt_cpu","aablt")
$incs    = @("$PSScriptRoot/shim","$tools/ce6-oak/INC","$tools/ce42-standard/Include/Armv4i")

# Pure-ARMv4 (no-Thumb cores, e.g. SA-1110) against the rebuilt Armv4 OAK libs.
& "$PSScriptRoot/../../tools/build_ce_app.ps1" `
    -Type dll -Target cerf_guest.dll -ObjDir obj_arm `
    -Sources $sources -Entry DllEntryPoint `
    -ExtraIncludes $incs `
    -ExtraLibPaths "$tools/ce6-oak/Lib/Armv4/retail" `
    -Libs $libs `
    -CoreDllDef "$PSScriptRoot/coredll_byname.def" `
    -ForcedInclude "ce6_shim.h","cerf_debug_log.h" `
    -LinkExtras "/MERGE:.rdata=.text"

# ARMV4I interworking (Thumb-capable cores) against the stock Armv4i OAK libs.
& "$PSScriptRoot/../../tools/build_ce_app.ps1" `
    -Type dll -Target cerf_guest_thumb.dll `
    -Interwork -ObjDir obj_thumb -DefFile cerf_guest.def `
    -Sources $sources -Entry DllEntryPoint `
    -ExtraIncludes $incs `
    -ExtraLibPaths "$tools/ce6-oak/Lib/Armv4i/retail" `
    -Libs $libs `
    -CoreDllDef "$PSScriptRoot/coredll_byname.def" `
    -ForcedInclude "ce6_shim.h","cerf_debug_log.h" `
    -LinkExtras "/MERGE:.rdata=.text"
