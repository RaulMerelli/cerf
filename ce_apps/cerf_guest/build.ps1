Set-Location $PSScriptRoot

$build   = "$PSScriptRoot/../../tools/build_ce_app.ps1"
$sources = @("main.cpp","cerf_virt_base.cpp","cerf_regs_map.cpp","cerf_debug_log.cpp",
             "cerf_eng_callbacks.cpp",
             "cerf_gpe.cpp","cerf_ddi_pdev.cpp","cerf_ddi_blt.cpp","cerf_ddi_device.cpp",
             "cerf_ddi_stroke.cpp","cerf_ddi_fill.cpp",
             "cerf_ddgpe.cpp","cerf_ddgpe_blt.cpp","cerf_ddgpe_vidmem.cpp","cerf_ddgpe_ddhal.cpp",
             "cerf_ddhal.cpp","cerf_ddhal_ce5.cpp",
             "cerf_dma_arena.cpp",
             "cerf_gradient.cpp",
             "cerf_pointer_pump.cpp","cerf_keyboard_pump.cpp","cerf_resize_pump.cpp","cerf_task_manager_pump.cpp",
             "cerf_power.cpp",
             "cerf_cursor.cpp",
             "cerf_getversionexw.cpp",
             "cerf_driver_in_driver.cpp","cerf_fs_afs.cpp","cerf_fs_transport.cpp",
             "cerf_fs_vol.cpp","cerf_fs_file.cpp","cerf_fs_find.cpp","cerf_fs_notify.cpp")
$libs    = @("coredll")
$baseInc = @("$PSScriptRoot/include")

$sources_ce2 = $sources + @("cerf_ce2_crt.cpp")

# Pure-ARMv4 (no-Thumb cores, e.g. SA-1110).
& $build `
    -Type dll -Target cerf_guest.dll -Arch arm -ObjDir obj_arm `
    -Sources $sources -Entry DllEntryPoint `
    -ExtraIncludes $baseInc `
    -Libs $libs `
    -CoreDllDef "$PSScriptRoot/coredll_byname.def" `
    -ForcedInclude "cerf_debug_log.h" `
    -LinkExtras "/MERGE:.rdata=.text"

# ARMV4I interworking (Thumb-capable cores).
& $build `
    -Type dll -Target cerf_guest.dll -Arch arm_thumb -ObjDir obj_thumb -DefFile cerf_guest.def `
    -Sources $sources -Entry DllEntryPoint `
    -ExtraIncludes $baseInc `
    -Libs $libs `
    -CoreDllDef "$PSScriptRoot/coredll_byname.def" `
    -ForcedInclude "cerf_debug_log.h" `
    -LinkExtras "/MERGE:.rdata=.text"

& $build `
    -Type dll -Target cerf_guest.dll -Arch mips -MipsIsa mips4 -ObjDir obj_mips4 -DefFile cerf_guest.def `
    -Sources $sources -Entry DllEntryPoint `
    -ExtraIncludes $baseInc `
    -Libs $libs `
    -CoreDllDef "$PSScriptRoot/coredll_byname.def" `
    -ForcedInclude "cerf_debug_log.h" `
    -LinkExtras "/MERGE:.rdata=.text"

& $build `
    -Type dll -Target cerf_guest.dll -Arch mips -MipsIsa mips2 -ObjDir obj_mips2 -DefFile cerf_guest.def `
    -Sources $sources -Entry DllEntryPoint `
    -ExtraIncludes $baseInc `
    -Libs $libs `
    -CoreDllDef "$PSScriptRoot/coredll_byname.def" `
    -ForcedInclude "cerf_debug_log.h" `
    -LinkExtras "/MERGE:.rdata=.text"

# CE 2.x MIPS-II body: coredll def trimmed of the CRT/soft-FP/C++ runtime.
& $build `
    -Type dll -Target cerf_guest.dll -Arch mips -MipsIsa mips2 -ObjDir obj_mips2_ce2 -DefFile cerf_guest.def `
    -Sources $sources_ce2 -Entry DllEntryPoint -OutSubdir mips2_ce2 `
    -ExtraIncludes $baseInc `
    -Libs $libs `
    -CoreDllDef "$PSScriptRoot/coredll_ce2.def" `
    -ForcedInclude "cerf_debug_log.h" `
    -LinkExtras "/MERGE:.rdata=.text"
