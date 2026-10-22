# Build the BMCD Windows portable package and SHA256SUMS.
#
#   pwsh packaging/build-windows.ps1
#   pwsh packaging/build-windows.ps1 -Sign -CertificateThumbprint <thumbprint>
#
# Run on a Windows runner after `cmake --build build --config Release`.
# windeployqt must be on PATH (the Qt installer or install-qt-action provides it).
# The default artifact is intentionally unsigned; -Sign requires signtool and a
# code-signing certificate in the current user's certificate store.
param(
    [string]$BuildDir = "build",
    [string]$OutDir = "dist",
    [switch]$Sign,
    [string]$CertificateThumbprint = ""
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$OutDir = Join-Path $Root $OutDir
$Stage = Join-Path $OutDir "BMCD"
$Zip = Join-Path $OutDir "BMCD-windows-x64.zip"
$Sums = Join-Path $OutDir "SHA256SUMS.txt"

function Find-Exe {
    param([string]$Dir)
    $candidates = @(
        (Join-Path $Dir "Release\bmcd.exe"),
        (Join-Path $Dir "bmcd.exe"),
        (Join-Path $Dir "RelWithDebInfo\bmcd.exe")
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) { return $candidate }
    }
    throw "bmcd.exe not found under $Dir (looked for Release, root and RelWithDebInfo)"
}

$Exe = Find-Exe (Join-Path $Root $BuildDir)
$Windeploy = Get-Command windeployqt -ErrorAction SilentlyContinue
if (-not $Windeploy) {
    throw "windeployqt is not on PATH; install Qt or run the install-qt-action step first"
}

Remove-Item $Stage -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item $Zip -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
New-Item -ItemType Directory -Force -Path $Stage | Out-Null
Copy-Item $Exe (Join-Path $Stage "bmcd.exe")
Copy-Item (Join-Path $Root "LICENSE") (Join-Path $Stage "LICENSE.txt")
Copy-Item (Join-Path $Root "README.md") (Join-Path $Stage "README.md")

# --compiler-runtime deploys the VC++ runtime DLLs next to the executable, so a
# clean Windows machine does not need a separate redistributable install.
& $Windeploy.Source --release --no-translations --compiler-runtime --dir $Stage (Join-Path $Stage "bmcd.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE" }

foreach ($required in @("platforms\qwindows.dll", "Qt6Core.dll", "Qt6Quick.dll")) {
    if (-not (Test-Path (Join-Path $Stage $required))) {
        throw "windeployqt output is missing $required"
    }
}
foreach ($runtime in @("vcruntime140.dll", "msvcp140.dll")) {
    if (-not (Test-Path (Join-Path $Stage $runtime))) {
        throw "windeployqt --compiler-runtime did not copy $runtime"
    }
}

if ($Sign) {
    $SignTool = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if (-not $SignTool) {
        $KitsBin = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin"
        if (Test-Path $KitsBin) {
            $SignTool = Get-ChildItem (Join-Path $KitsBin "*\x64\signtool.exe") -ErrorAction SilentlyContinue |
                Sort-Object FullName -Descending | Select-Object -First 1
        }
    }
    if (-not $SignTool) {
        throw "-Sign requested but signtool.exe was not found"
    }
    if ([string]::IsNullOrWhiteSpace($CertificateThumbprint)) {
        throw "-Sign requires -CertificateThumbprint"
    }
    $SignToolPath = if ($SignTool.PSObject.Properties.Name -contains "Source") {
        $SignTool.Source
    } else {
        $SignTool.FullName
    }
    & $SignToolPath sign /sha1 $CertificateThumbprint /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 (Join-Path $Stage "bmcd.exe")
    if ($LASTEXITCODE -ne 0) { throw "signtool failed with exit code $LASTEXITCODE" }
    & $SignToolPath verify /pa /v (Join-Path $Stage "bmcd.exe")
    if ($LASTEXITCODE -ne 0) { throw "signtool verification failed" }
} else {
    @"
BMCD Windows portable build
This executable is UNSIGNED. Windows SmartScreen may warn on first launch.
Verify the SHA256SUMS.txt value before running it.
"@ | Set-Content -Encoding ascii (Join-Path $Stage "UNSIGNED.txt")
}

# Portable zip: deployed files at the archive root, no installer or registry
# changes. Paths containing spaces and non-ASCII names extract unchanged.
Compress-Archive -Path (Join-Path $Stage "*") -DestinationPath $Zip -CompressionLevel Optimal
$Hash = (Get-FileHash -Algorithm SHA256 $Zip).Hash.ToLowerInvariant()
"$Hash  $(Split-Path -Leaf $Zip)" | Set-Content -Encoding ascii $Sums
Write-Host "built: $Zip"
Get-Content $Sums

if (-not $Sign) {
    Write-Host "signing: skipped (UNSIGNED.txt is inside the zip)"
}
