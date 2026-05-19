param(
    [string]$Config = "Release"
)

Set-Location $PSScriptRoot

function Find-Python {
    foreach ($candidate in @("py", "python3", "python")) {
        $null = & $candidate --version 2>$null
        if ($LASTEXITCODE -eq 0) { return $candidate }
    }
    return $null
}

$python = Find-Python
if (-not $python) {
    Write-Host "[LAUNCHER] FAILED! Python 3.10+ not found on PATH."
    Write-Host "[LAUNCHER] Install from https://www.python.org/ or the Microsoft Store."
    [Environment]::Exit(1)
}

$null = & $python -c "import PyInstaller" 2>$null
if ($LASTEXITCODE -ne 0) {
    Write-Host "[LAUNCHER] PyInstaller not found; installing into the user site..."
    & $python -m pip install --user --quiet pyinstaller
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
