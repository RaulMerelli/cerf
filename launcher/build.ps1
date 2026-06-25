param(
    [string]$Config = "Release"
)

Set-Location $PSScriptRoot

# The launcher table renders per-cell images (CPU-arch badges in the SoC
# column), which needs Tk 9 (TIP 552). No Windows CPython ships Tk 9 until 3.15,
# so the launcher build uses a portable python-build-standalone CPython 3.15
# (Tk 9.0.3) -- a relocatable tarball extracted with the built-in `tar`, NO
# installer and NO registry, cached under the gitignored references/python (a
# first-build download, same pattern as vcpkg). cerf.exe is unaffected;
# PyInstaller bakes Tk 9 into launcher.exe, so end users need nothing installed.
$PBS_VERSION = "3.15.0b3"
$PBS_TAG     = "20260623"
$PBS_SHA256  = "a32eb39b3ab3c3dbc5d04c5c4dddda3966f8b0e91b6be979350baac0603cec34"
$PBS_URL     = "https://github.com/astral-sh/python-build-standalone/releases/download/$PBS_TAG/cpython-$PBS_VERSION%2B$PBS_TAG-x86_64-pc-windows-msvc-install_only.tar.gz"

function Get-LauncherPython {
    $repoRoot  = Split-Path $PSScriptRoot -Parent
    $cacheRoot = Join-Path $repoRoot "references\python"
    $target    = Join-Path $cacheRoot "cpython-$PBS_VERSION"
    $py        = Join-Path $target "python\python.exe"   # tarball extracts to python\
    if (Test-Path $py) { return $py }

    New-Item -ItemType Directory -Force -Path $cacheRoot | Out-Null
    $tgz = Join-Path $cacheRoot "cpython-$PBS_VERSION-windows-x64.tar.gz"
    $haveGood = (Test-Path $tgz) -and
                ((Get-FileHash -Algorithm SHA256 -Path $tgz).Hash.ToLower() -eq $PBS_SHA256)
    if (-not $haveGood) {
        Write-Host "[LAUNCHER] Downloading portable Python $PBS_VERSION (Tk 9) ..."
        $pp = $ProgressPreference; $ProgressPreference = "SilentlyContinue"
        Invoke-WebRequest -Uri $PBS_URL -OutFile $tgz
        $ProgressPreference = $pp
        $got = (Get-FileHash -Algorithm SHA256 -Path $tgz).Hash.ToLower()
        if ($got -ne $PBS_SHA256) {
            Write-Host "[LAUNCHER] FAILED! Python archive SHA256 mismatch (got $got, want $PBS_SHA256)."
            return $null
        }
    }
    Write-Host "[LAUNCHER] Extracting portable Python into references/python (no install, no registry) ..."
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    tar -xf $tgz -C $target
    if (-not (Test-Path $py)) {
        Write-Host "[LAUNCHER] FAILED! python.exe not present at $py after extract."
        return $null
    }
    return $py
}

$python = Get-LauncherPython
if (-not $python) { [Environment]::Exit(1) }

$null = & $python -c "import PyInstaller" 2>$null
if ($LASTEXITCODE -ne 0) {
    Write-Host "[LAUNCHER] PyInstaller not found in cached Python; installing..."
    & $python -m pip install --quiet --disable-pip-version-check pyinstaller
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[LAUNCHER] FAILED! pip install pyinstaller returned $LASTEXITCODE"
        [Environment]::Exit(1)
    }
}

$build = Join-Path $PSScriptRoot "build"
$dist  = Join-Path $PSScriptRoot "dist"
if (Test-Path $build) { Remove-Item $build -Recurse -Force }
if (Test-Path $dist)  { Remove-Item $dist  -Recurse -Force }

Write-Host "[LAUNCHER] Building launcher.exe ($Config)..."
& $python -m PyInstaller --noconfirm --clean --distpath $dist --workpath $build launcher.spec
if ($LASTEXITCODE -ne 0) {
    Write-Host "[LAUNCHER] FAILED! PyInstaller returned $LASTEXITCODE"
    [Environment]::Exit(1)
}

$built = Join-Path $dist "launcher.exe"
if (-not (Test-Path $built)) {
    Write-Host "[LAUNCHER] FAILED! Expected $built not produced."
    [Environment]::Exit(1)
}

$bundledDir = Join-Path $PSScriptRoot "..\bundled"
if (-not (Test-Path $bundledDir)) { New-Item -ItemType Directory -Path $bundledDir -Force | Out-Null }
$bundledExe = Join-Path $bundledDir "launcher.exe"
Copy-Item $built $bundledExe -Force

$exe = Get-Item $bundledExe
Write-Host "[LAUNCHER] OK: $($exe.FullName)"
Write-Host "[LAUNCHER] Size: $($exe.Length) bytes"
[Environment]::Exit(0)
