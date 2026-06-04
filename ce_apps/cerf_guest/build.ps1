Set-Location $PSScriptRoot
& "$PSScriptRoot/../../tools/build_ce_app.ps1" `
    -Type dll -Target cerf_guest.dll `
    -Sources main.cpp,cerf_debug_log.cpp,cerf_ddgpe.cpp,cerf_ddgpe_blt.cpp,cerf_ddhal.cpp,cerf_gradient.cpp,cerf_pointer_pump.cpp,cerf_resize_pump.cpp,cerf_cursor.cpp,cerf_driver_in_driver.cpp,cerf_fs_afs.c,cerf_fs_transport.c,cerf_fs_vol.c,cerf_fs_file.c,cerf_fs_find.c,cerf_fs_notify.c `
    -Entry DllEntryPoint `
    -ExtraIncludes "$PSScriptRoot/shim","$PSScriptRoot/../../references/WindowsCE-Build-Tools/ce6-oak/INC","$PSScriptRoot/../../references/WindowsCE-Build-Tools/ce42-standard/Include/Armv4i" `
    -ExtraLibPaths "$PSScriptRoot/../../references/WindowsCE-Build-Tools/ce6-oak/Lib/Armv4i/retail" `
    -Libs coredll,ddgpe,gpe_lib,emul,emulrotate,genblt,genblt_cpu,ctbltstub,aablt,drvalphablendstub,drvgradfillstub `
    -ForcedInclude "ce6_shim.h","cerf_debug_log.h" `
    -LinkExtras "/MERGE:.rdata=.text"
