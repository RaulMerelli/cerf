param(
    [string]$Config = "Release",
    [switch]$Vista
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

# launcher_vista.exe, for hosts older than the primary launcher's Windows 10
# floor. Both floors are properties of the shipped binaries, measured against the
# per-OS export tables in YY-Thunks' Config/x86 (see cerf/cerf.vcxproj):
#   - CPython 3.8+ (python3x.dll) imports kernel32!GetActiveProcessorCount, which
#     Windows 7 introduced -- so 3.7.9 is the newest CPython that loads on Vista.
#   - PyInstaller 6.x's bootloader imports kernel32!K32EnumProcessModules and
#     K32GetModuleFileNameExW (Windows 7); 5.13.2's bootloader is Vista-clean.
# python.org ships no portable 3.7 carrying tkinter (neither the embeddable zip
# nor the nuget package has it), so the installer is run in its quiet per-user
# mode into the same gitignored cache: files only, nothing on PATH.
$PY37_VERSION = "3.7.9"
$PY37_SHA256  = "769bb7c74ad1df6d7d74071cc16a984ff6182e4016e11b8949b93db487977220"
$PY37_URL     = "https://www.python.org/ftp/python/$PY37_VERSION/python-$PY37_VERSION.exe"
$PYINSTALLER_VISTA = "5.13.2"

function Get-VistaPython {
    $repoRoot  = Split-Path $PSScriptRoot -Parent
    $cacheRoot = Join-Path $repoRoot "references\python"
    $target    = Join-Path $cacheRoot "cpython-$PY37_VERSION-x86"
    $py        = Join-Path $target "python.exe"
    if (Test-Path $py) { return $py }

    New-Item -ItemType Directory -Force -Path $cacheRoot | Out-Null
    $installer = Join-Path $cacheRoot "python-$PY37_VERSION-x86.exe"
    $haveGood = (Test-Path $installer) -and
                ((Get-FileHash -Algorithm SHA256 -Path $installer).Hash.ToLower() -eq $PY37_SHA256)
    if (-not $haveGood) {
        Write-Host "[LAUNCHER] Downloading CPython $PY37_VERSION (x86, Vista-compatible) ..."
        $pp = $ProgressPreference; $ProgressPreference = "SilentlyContinue"
        Invoke-WebRequest -Uri $PY37_URL -OutFile $installer
        $ProgressPreference = $pp
        $got = (Get-FileHash -Algorithm SHA256 -Path $installer).Hash.ToLower()
        if ($got -ne $PY37_SHA256) {
            Write-Host "[LAUNCHER] FAILED! Python archive SHA256 mismatch (got $got, want $PY37_SHA256)."
            return $null
        }
    }
    Write-Host "[LAUNCHER] Extracting CPython $PY37_VERSION into references/python (per-user, not on PATH) ..."
    & $installer /quiet TargetDir=$target InstallAllUsers=0 PrependPath=0 `
        AssociateFiles=0 Shortcuts=0 Include_launcher=0 InstallLauncherAllUsers=0 `
        Include_test=0 Include_doc=0 | Out-Null
    if (-not (Test-Path $py)) {
        Write-Host "[LAUNCHER] FAILED! python.exe not present at $py after extract."
        return $null
    }
    return $py
}

function Get-UcrtRedistDir {
    $kits = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\Redist"
    if (-not (Test-Path $kits)) { return $null }
    $dirs = Get-ChildItem $kits -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending
    foreach ($d in $dirs) {
        $ucrt = Join-Path $d.FullName "ucrt\DLLs\x86"
        if (Test-Path $ucrt) { return $ucrt }
    }
    return $null
}

if ($Vista) {
    $python  = Get-VistaPython
    $pyiSpec = "pyinstaller==$PYINSTALLER_VISTA"
    $name    = "launcher_vista"
    $ucrt    = Get-UcrtRedistDir
    if (-not $ucrt) {
        Write-Host "[LAUNCHER] FAILED! UCRT redist (Windows Kits\10\Redist\<ver>\ucrt\DLLs\x86) not found; launcher_vista.exe would not run on a Vista box without KB2999226."
        [Environment]::Exit(1)
    }
    $env:CERF_LAUNCHER_UCRT = $ucrt
} else {
    $python  = Get-LauncherPython
    $pyiSpec = "pyinstaller"
    $name    = "launcher"
    $env:CERF_LAUNCHER_UCRT = ""
}
if (-not $python) { [Environment]::Exit(1) }
$env:CERF_LAUNCHER_NAME = $name

$null = & $python -c "import PyInstaller" 2>$null
if ($LASTEXITCODE -ne 0) {
    Write-Host "[LAUNCHER] PyInstaller not found in cached Python; installing $pyiSpec..."
    & $python -m pip install --quiet --disable-pip-version-check $pyiSpec
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[LAUNCHER] FAILED! pip install $pyiSpec returned $LASTEXITCODE"
        [Environment]::Exit(1)
    }
}

$build = Join-Path $PSScriptRoot "build"
$dist  = Join-Path $PSScriptRoot "dist"
if (Test-Path $build) { Remove-Item $build -Recurse -Force }
if (Test-Path $dist)  { Remove-Item $dist  -Recurse -Force }

Write-Host "[LAUNCHER] Building $name.exe ($Config)..."
& $python -m PyInstaller --noconfirm --clean --distpath $dist --workpath $build launcher.spec
if ($LASTEXITCODE -ne 0) {
    Write-Host "[LAUNCHER] FAILED! PyInstaller returned $LASTEXITCODE"
    [Environment]::Exit(1)
}

$built = Join-Path $dist "$name.exe"
if (-not (Test-Path $built)) {
    Write-Host "[LAUNCHER] FAILED! Expected $built not produced."
    [Environment]::Exit(1)
}

$bundledDir = Join-Path $PSScriptRoot "..\bundled"
if (-not (Test-Path $bundledDir)) { New-Item -ItemType Directory -Path $bundledDir -Force | Out-Null }
$bundledExe = Join-Path $bundledDir "$name.exe"
Copy-Item $built $bundledExe -Force

$exe = Get-Item $bundledExe
Write-Host "[LAUNCHER] OK: $($exe.FullName)"
Write-Host "[LAUNCHER] Size: $($exe.Length) bytes"
[Environment]::Exit(0)
