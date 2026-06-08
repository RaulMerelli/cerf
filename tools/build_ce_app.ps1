# Shared builder for CERF's CE 3.0 apps and DLLs: plain ARMv4 by default,
# ARMV4I Thumb-interworking with -Interwork. Raising the subsystem stamp
# from 3.00 locks the resulting binaries out of CE3 kernels.
param(
    [Parameter(Mandatory)][ValidateSet("exe","dll")][string]$Type,
    [Parameter(Mandatory)][string]$Target,
    [string[]]$Sources = @("main.c"),
    [string]$Entry,
    [string[]]$Libs = @("coredll"),
    [string[]]$LinkExtras = @(),
    [string[]]$ExtraIncludes = @(),
    [string[]]$ExtraLibPaths = @(),
    [string[]]$ForcedInclude = @(),
    [string]$DefFile,
    [string]$ObjDir = ".",
    [switch]$Interwork,
    # PE subsystem stamp. Default 3.00; lower it (e.g. "2.11") to let the
    # binary load on older H/PC Pro kernels — a lower stamp still loads on
    # CE3+, a higher one locks the binary out of older kernels.
    [string]$SubsystemVersion = "3.00",
    # Target a different CE SDK than the default CE3/HPC2000 set. When
    # $SdkIncludes is non-empty it REPLACES the default ce3 include dirs, and
    # $CoreLibDir REPLACES the default ce42 coredll import-lib dir. $WceVersion
    # sets _WIN32_WCE. Together these let a single source tree build against,
    # e.g., the WINCE211 (H/PC Pro / SA-1100) SDK for a genuine CE 2.11 binary.
    [string[]]$SdkIncludes = @(),
    [string]$CoreLibDir = "",
    [string]$WceVersion = "300",
    # Optional resource script (.rc) compiled to .res and linked in — e.g. to
    # give the EXE a shell/title-bar icon. Use classic BMP icon frames; CE's
    # shell does not render PNG-compressed (Vista) .ico entries.
    [string]$Rc = ""
)
$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path "$PSScriptRoot/..").Path
$SDK  = Join-Path $RepoRoot "references/WindowsCE-Build-Tools"
$CL   = Join-Path $SDK "bin/I386/ARM/cl.exe"
$LINK = Join-Path $SDK "bin/I386/link.exe"
# NB: not $RC — PowerShell variable names are case-insensitive, so $RC would
# alias the $Rc parameter (the .rc path) and clobber it.
$RESC = Join-Path $SDK "bin/I386/rc.exe"

$IncDirs = @()
foreach ($i in $ExtraIncludes) { $IncDirs += (Resolve-Path $i).Path }
if ($SdkIncludes.Count) {
    foreach ($i in $SdkIncludes) { $IncDirs += (Resolve-Path $i).Path }
} else {
    $IncDirs += @(
        (Join-Path $SDK "ce3-hpc2k/include"),
        (Join-Path $SDK "ce3-oak/INC")
    )
}
$IncDirs += $RepoRoot
$LIB       = if ($CoreLibDir) { (Resolve-Path $CoreLibDir).Path }
             else { Join-Path $SDK $(if ($Interwork) { "ce42-standard/Lib/Armv4i" }
                                     else            { "ce42-standard/Lib/Armv4"  }) }
$Machine   = if ($Interwork) { "THUMB" } else { "ARM" }
$ArchFlags = if ($Interwork) { @("/QRarch4T", "/QRinterwork-return", "/DARMV4I") }
             else            { @("/QRarch4", "/DARMV4") }
$Subsystem = "windowsce,$SubsystemVersion"
$WceDef    = "_WIN32_WCE=$WceVersion"

if (-not (Test-Path $CL))   { throw "WCE toolchain missing: $CL"   }
if (-not (Test-Path $LINK)) { throw "WCE toolchain missing: $LINK" }

# cl.exe and link.exe both depend on companion DLLs (c1.dll / c1xx.dll / c2.dll
# next to cl, mspdb*.dll under bin/I386). Wire PATH up before either is invoked.
$env:PATH = "$SDK\bin\I386\ARM;$SDK\bin\I386;" + $env:PATH

$Config = if ($env:CE_APPS_CONFIG) { $env:CE_APPS_CONFIG } else { "Release" }
$Mode   = if ($env:CE_APPS_MODE)   { $env:CE_APPS_MODE }   else { "dev" }
$devModeFlag = if ($Mode -eq "production") { "0" } else { "1" }
$OutDir = Join-Path $RepoRoot "build/$Config/Win32/ce_apps"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$StagedTarget = Join-Path $OutDir $Target

if (-not $Entry) {
    $Entry = if ($Type -eq "exe") { "WinMain" } else { "DllEntryPoint" }
}

New-Item -ItemType Directory -Force -Path $ObjDir | Out-Null

# Mode-change marker — bust .obj cache if CERF_DEV_MODE flipped between runs.
$modeMarker = Join-Path $ObjDir ".build_mode"
$cachedMode = if (Test-Path $modeMarker) { (Get-Content $modeMarker -Raw).Trim() } else { "" }
if ($cachedMode -ne $Mode) {
    Get-ChildItem -Path $ObjDir -Filter "*.obj" -ErrorAction SilentlyContinue | Remove-Item -Force
    Set-Content -Path $modeMarker -Value $Mode -Encoding ASCII -NoNewline
}

# Compile: .obj files land in $ObjDir (default: the per-app dir, co-located
# with sources) so each variant keeps its own incremental cache.
$objs = @()
foreach ($src in $Sources) {
    $obj = Join-Path $ObjDir ([System.IO.Path]::ChangeExtension((Split-Path $src -Leaf), ".obj"))
    $objs += $obj
    $needCompile = -not (Test-Path $obj)
    if (-not $needCompile) {
        $needCompile = ((Get-Item $src).LastWriteTime -gt (Get-Item $obj).LastWriteTime)
    }
    if ($needCompile) {
        Write-Host "[CE] cl  $src"
        $incFlags = @()
        foreach ($i in $IncDirs) { $incFlags += @("/I", $i) }
        $fiFlag = @()
        foreach ($fi in $ForcedInclude) { $fiFlag += @("/FI", $fi) }
        & $CL /nologo /c /W3 /WX /O2 @ArchFlags /DUNICODE /D_UNICODE /DUNDER_CE /DARM /D_ARM_ "/D$WceDef" "/DCERF_DEV_MODE=$devModeFlag" "/Fo$obj" @incFlags @fiFlag $src
        if ($LASTEXITCODE -ne 0) { throw "Compile failed: $src" }
    }
}

# Compile the optional resource script (.rc -> .res) for the EXE/DLL icon
# and other resources. rc.exe needs rcdll.dll, already on PATH via bin/I386.
$res = ""
if ($Rc) {
    if (-not (Test-Path $RESC)) { throw "WCE resource compiler missing: $RESC" }
    $rcPath = (Resolve-Path $Rc).Path
    $res = Join-Path $ObjDir ([System.IO.Path]::ChangeExtension((Split-Path $Rc -Leaf), ".res"))
    $needRc = -not (Test-Path $res)
    if (-not $needRc) {
        $needRc = ((Get-Item $rcPath).LastWriteTime -gt (Get-Item $res).LastWriteTime)
    }
    if ($needRc) {
        Write-Host "[CE] rc  $Rc"
        & $RESC /r /fo $res $rcPath
        if ($LASTEXITCODE -ne 0) { throw "Resource compile failed: $Rc" }
    }
}

# Link: staged target lives under build/<Config>/Win32/ce_apps/.
$needLink = -not (Test-Path $StagedTarget)
if (-not $needLink) {
    $stagedTime = (Get-Item $StagedTarget).LastWriteTime
    foreach ($obj in $objs) {
        if ((Get-Item $obj).LastWriteTime -gt $stagedTime) {
            $needLink = $true; break
        }
    }
    if ($res -and (Get-Item $res).LastWriteTime -gt $stagedTime) { $needLink = $true }
}

if ($needLink) {
    Write-Host "[CE] link $Target -> $StagedTarget"
    $linkArgs = @("/nologo", "/subsystem:$Subsystem", "/entry:$Entry",
                  "/machine:$Machine", "/nodefaultlib", "/libpath:$LIB",
                  "/out:$StagedTarget")
    foreach ($p in $ExtraLibPaths) { $linkArgs += "/libpath:$((Resolve-Path $p).Path)" }
    if ($Type -eq "dll") {
        $implib = [System.IO.Path]::ChangeExtension($Target, ".lib")
        $linkArgs += "/dll"
        $linkArgs += "/implib:$implib"
        $defFile = if ($DefFile) { $DefFile }
                   else { [System.IO.Path]::ChangeExtension($Target, ".def") }
        if (Test-Path $defFile) {
            $linkArgs += "/def:$defFile"
        }
    }
    foreach ($extra in $LinkExtras) { $linkArgs += $extra }
    $linkArgs += $objs
    if ($res) { $linkArgs += $res }
    foreach ($lib in $Libs) { $linkArgs += "$lib.lib" }
    & $LINK @linkArgs
    if ($LASTEXITCODE -ne 0) { throw "Link failed: $Target" }
    Write-Host "[CE]      $((Get-Item $StagedTarget).Length) bytes"
} else {
    Write-Host "[CE] $Target up-to-date"
}
