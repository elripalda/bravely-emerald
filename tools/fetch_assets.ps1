# Windows entry point of tools/fetch_assets.sh (runs it in MSYS2). Usage:
#     powershell.exe -ExecutionPolicy Bypass -File .\tools\fetch_assets.ps1 -Rom "C:\path\to\emerald.gba"
param([Parameter(Mandatory = $true)][string]$Rom)
$ErrorActionPreference = "Stop"

# this script is in tools/; the repository root is its parent
$scriptDir = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $scriptDir

$bashExe = "C:\msys64\usr\bin\bash.exe"
if (-not (Test-Path $bashExe)) {
    Write-Error "MSYS2 not found (C:\msys64\usr\bin\bash.exe). See docs/WINDOWS_BUILD.md."
    exit 1
}
if (-not (Test-Path $Rom)) {
    Write-Error "ROM not found: $Rom"
    exit 1
}
$romFull = (Resolve-Path $Rom).Path

$drive = (Get-Item $scriptDir).PSDrive.Name.ToLower()
$unixDir = "/$drive/" + $scriptDir.Substring(3).Replace('\', '/')
$romDrive = $romFull.Substring(0, 1).ToLower()
$unixRom = "/$romDrive/" + $romFull.Substring(3).Replace('\', '/')

# Windows Python (with Pillow) is found by MSYS2 through the inherited PATH
& $bashExe -lc "export PATH=`"`$PATH:`$(cygpath -u -p '$env:PATH')`"; cd '$unixDir' && ./tools/fetch_assets.sh '$unixRom'"
exit $LASTEXITCODE
