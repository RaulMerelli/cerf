# Regenerates the ce6-oak/Lib/Armv4 display libraries from their WINCE600
# Platform Builder sources with ARMv4 codegen. Not part of the build:
# requires a local references/WINCE600 tree; run manually, then place the
# outputs into the WindowsCE-Build-Tools submodule.
param()
$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path "$PSScriptRoot/..").Path
$SDK      = Join-Path $RepoRoot "references/WindowsCE-Build-Tools"
$WINCE    = Join-Path $RepoRoot "references/WINCE600"
$CL       = Join-Path $SDK "bin/I386/ARM/cl.exe"
$LINK     = Join-Path $SDK "bin/I386/link.exe"
$DISPLAY  = Join-Path $WINCE "PUBLIC/COMMON/OAK/DRIVERS/DISPLAY"

if (-not (Test-Path $CL))     { throw "WCE toolchain missing: $CL" }
if (-not (Test-Path $WINCE))  { throw "WINCE600 sources missing: $WINCE" }
$env:PATH = "$SDK\bin\I386\ARM;$SDK\bin\I386;" + $env:PATH

$OutDir = Join-Path $RepoRoot "tmp/ce_libs"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$IncDirs = @(
    (Join-Path $WINCE "PUBLIC/COMMON/SDK/INC"),
    (Join-Path $WINCE "PUBLIC/COMMON/DDK/INC"),
    (Join-Path $WINCE "PUBLIC/COMMON/OAK/INC")
)

# lib link name -> source directory (each carries a 'sources' file).
$LibDirs = [ordered]@{
    "gpe_lib"    = (Join-Path $DISPLAY "GPE")
    "ddgpe"      = (Join-Path $DISPLAY "DDGPE")
    "emul"       = (Join-Path $DISPLAY "EMUL")
    "emulrotate" = (Join-Path $DISPLAY "EMULROTATE")
    "genblt"     = (Join-Path $DISPLAY "GENBLT/GENBLT")
    "aablt"      = (Join-Path $DISPLAY "AABLT")
    "genblt_cpu" = (Join-Path $WINCE "PUBLIC/COMMON/OAK/CPULIBS/ARM/GENBLT_CPU")
}

# SOURCES= block of an NMAKE 'sources' file: backslash-continued file list.
function Get-SourcesList([string]$dir) {
    $names = @()
    $in = $false
    foreach ($line in (Get-Content (Join-Path $dir "sources"))) {
        $t = $line.Trim()
        if (-not $in) {
            if ($t -match '^SOURCES\s*=\s*(.*)$') {
                $in = $true
                $t = $Matches[1].Trim()
            } else { continue }
        }
        $cont = $t.EndsWith('\')
        $t = $t.TrimEnd('\').Trim()
        if ($t -match '^([\w\.]+\.(cpp|c))$') { $names += $Matches[1] }
        if (-not $cont) { $in = $false }
    }
    return $names
}

foreach ($name in $LibDirs.Keys) {
    $dir = $LibDirs[$name]
    $sources = Get-SourcesList $dir
    if ($sources.Count -eq 0) { throw "no SOURCES parsed for $name in $dir" }

    $target = Join-Path $OutDir "$name.lib"
    $stale = -not (Test-Path $target)
    if (-not $stale) {
        $t = (Get-Item $target).LastWriteTime
        foreach ($s in $sources) {
            if ((Get-Item (Join-Path $dir $s)).LastWriteTime -gt $t) { $stale = $true; break }
        }
    }
    if (-not $stale) {
        Write-Host "[CELIB] $name.lib up-to-date"
        continue
    }

    $objDir = Join-Path $OutDir "obj/$name"
    New-Item -ItemType Directory -Force -Path $objDir | Out-Null
    $incFlags = @("/I", $dir)
    foreach ($i in $IncDirs) { $incFlags += @("/I", $i) }

    $objs = @()
    foreach ($s in $sources) {
        $obj = Join-Path $objDir ([System.IO.Path]::ChangeExtension($s, ".obj"))
        $objs += $obj
        Write-Host "[CELIB] cl  $name/$s"
        & $CL /nologo /c /W3 /O2 /QRarch4 /DUNICODE /D_UNICODE /DUNDER_CE /DWINCEOEM /DARM /D_ARM_ /DARMV4 /D_WIN32_WCE=600 "/Fo$obj" @incFlags (Join-Path $dir $s)
        if ($LASTEXITCODE -ne 0) { throw "Compile failed: $name/$s" }
    }

    Write-Host "[CELIB] lib $name.lib"
    & $LINK /lib /nologo "/out:$target" @objs
    if ($LASTEXITCODE -ne 0) { throw "Lib failed: $name" }
}
Write-Host "[CELIB] done -> $OutDir"
