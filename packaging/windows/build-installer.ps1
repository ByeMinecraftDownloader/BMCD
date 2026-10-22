# Build the BMCD Windows installer from the staged portable build.
#
#   pwsh packaging/windows/build-installer.ps1 -Version 1.0.0
#
# Run packaging/build-windows.ps1 first; this script only compiles the Inno
# Setup project and appends the installer hash to dist/SHA256SUMS.txt.
param(
    [Parameter(Mandatory = $true)]
    [string]$Version,
    [string]$StageDir = "",
    [string]$OutDir = "dist"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if ([string]::IsNullOrWhiteSpace($StageDir)) {
    $StageDir = Join-Path $Root "dist\BMCD"
}
$OutDir = Join-Path $Root $OutDir
$Iss = Join-Path $PSScriptRoot "bmcd.iss"
$Sums = Join-Path $OutDir "SHA256SUMS.txt"
$Setup = Join-Path $OutDir "BMCD-Setup-$Version-x64.exe"

if ($Version -notmatch '^\d+\.\d+\.\d+$') {
    throw "Version must be X.Y.Z, got '$Version'"
}
if (-not (Test-Path (Join-Path $StageDir "bmcd.exe"))) {
    throw "staged bmcd.exe not found under $StageDir; run build-windows.ps1 first"
}
if (-not (Test-Path $Iss)) {
    throw "Inno Setup project not found: $Iss"
}

$Iscc = Get-Command iscc.exe -ErrorAction SilentlyContinue
if (-not $Iscc) {
    $candidates = @(
        (Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe"),
        (Join-Path $env:ProgramFiles "Inno Setup 6\ISCC.exe")
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            $Iscc = Get-Item $candidate
            break
        }
    }
}
if (-not $Iscc) {
    throw "iscc.exe not found; install Inno Setup 6 (choco install innosetup -y) or add it to PATH"
}
$IsccPath = if ($Iscc.PSObject.Properties.Name -contains "Source") {
    $Iscc.Source
} else {
    $Iscc.FullName
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Remove-Item $Setup -Force -ErrorAction SilentlyContinue
& $IsccPath "/DMyAppVersion=$Version" "/DStageDir=$StageDir" "/DOutputDir=$OutDir" $Iss
if ($LASTEXITCODE -ne 0) {
    throw "Inno Setup failed with exit code $LASTEXITCODE"
}
if (-not (Test-Path $Setup)) {
    throw "installer was not produced at $Setup"
}

$Hash = (Get-FileHash -Algorithm SHA256 $Setup).Hash.ToLowerInvariant()
$lines = @()
if (Test-Path $Sums) {
    $lines = @(Get-Content $Sums | Where-Object { $_ -notmatch 'BMCD-Setup-' })
}
$lines += "$Hash  $(Split-Path -Leaf $Setup)"
$lines | Set-Content -Encoding ascii $Sums
Write-Host "built installer: $Setup"
Get-Content $Sums
