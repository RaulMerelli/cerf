# Shared builder for CERF's Windows CE 5.0 ARMV4I apps and DLLs.
#
# Per-app build.ps1 scripts (one per directory under ce_apps/) call this
# helper to compile their C source(s) and link an .exe or .dll using the
# bundled WCE5 SDK at references/wince5-full-sdk/. Output is staged into
# build/<Config>/Win32/platform/prebuilt/ — the canonical location where every
# CERF-owned ARM CE binary lives at runtime (used by tests and by ad-hoc
# launches).
#
# Incremental: skips cl when each .obj is newer than its source; skips link
# when the staged artifact is newer than every .obj. The top-level build.ps1
# fires every per-app build unconditionally on each cerf rebuild, so a
# no-change run is a stat-only sweep.
#
# The caller (per-app build.ps1) is expected to Set-Location into its own
# directory before invoking this helper.
param(
    [Parameter(Mandatory)][ValidateSet("exe","dll")][string]$Type,
    [Parameter(Mandatory)][string]$Target,
    [string[]]$Sources = @("main.c"),
    [string]$Entry,
    [string[]]$Libs = @("coredll")
)
$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path "$PSScriptRoot/..").Path
$SDK  = Join-Path $RepoRoot "references/wince5-full-sdk"
$CL   = Join-Path $SDK "bin/I386/ARM/cl.exe"
$LINK = Join-Path $SDK "bin/I386/link.exe"
$INC  = Join-Path $SDK "Include/Armv4i"
$LIB  = Join-Path $SDK "Lib/ARMV4I"

if (-not (Test-Path $CL))   { throw "WCE5 SDK toolchain missing: $CL"   }
if (-not (Test-Path $LINK)) { throw "WCE5 SDK toolchain missing: $LINK" }

# cl.exe and link.exe both depend on companion DLLs (c1.dll / c1xx.dll / c2.dll
# next to cl, mspdb*.dll under bin/I386). Wire PATH up before either is invoked.
$env:PATH = "$SDK\bin\I386\ARM;$SDK\bin\I386;" + $env:PATH

$Config = if ($env:CE_APPS_CONFIG) { $env:CE_APPS_CONFIG } else { "Release" }
$OutDir = Join-Path $RepoRoot "build/$Config/Win32/platform/prebuilt"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$StagedTarget = Join-Path $OutDir $Target

if (-not $Entry) {
    $Entry = if ($Type -eq "exe") { "WinMain" } else { "DllEntryPoint" }
}

# Compile: each .obj is co-located with its source in the per-app dir, so
# the per-app dir caches state between runs.
$objs = @()
foreach ($src in $Sources) {
    $obj = [System.IO.Path]::ChangeExtension((Split-Path $src -Leaf), ".obj")
    $objs += $obj
    $needCompile = -not (Test-Path $obj)
    if (-not $needCompile) {
        $needCompile = ((Get-Item $src).LastWriteTime -gt (Get-Item $obj).LastWriteTime)
    }
    if ($needCompile) {
        Write-Host "[CE] cl  $src"
        & $CL /nologo /c /W3 /O2 /DUNICODE /D_UNICODE /DUNDER_CE /DARM /D_ARM_ /I $INC $src
        if ($LASTEXITCODE -ne 0) { throw "Compile failed: $src" }
    }
}

# Link: staged target lives under build/<Config>/Win32/platform/prebuilt/.
$needLink = -not (Test-Path $StagedTarget)
if (-not $needLink) {
    $stagedTime = (Get-Item $StagedTarget).LastWriteTime
    foreach ($obj in $objs) {
        if ((Get-Item $obj).LastWriteTime -gt $stagedTime) {
            $needLink = $true; break
        }
    }
}

if ($needLink) {
    Write-Host "[CE] link $Target -> $StagedTarget"
    $linkArgs = @("/nologo", "/subsystem:windowsce", "/entry:$Entry",
                  "/machine:THUMB", "/nodefaultlib", "/libpath:$LIB",
                  "/out:$StagedTarget")
    if ($Type -eq "dll") {
        # /IMPLIB keeps the auto-generated import .lib/.exp in the per-app dir
        # so the staged prebuilt/ tree stays runtime-only.
        $implib = [System.IO.Path]::ChangeExtension($Target, ".lib")
        $linkArgs += "/dll"
        $linkArgs += "/implib:$implib"
    }
    $linkArgs += $objs
    foreach ($lib in $Libs) { $linkArgs += "$lib.lib" }
    & $LINK @linkArgs
    if ($LASTEXITCODE -ne 0) { throw "Link failed: $Target" }
    Write-Host "[CE]      $((Get-Item $StagedTarget).Length) bytes"
} else {
    Write-Host "[CE] $Target up-to-date"
}
