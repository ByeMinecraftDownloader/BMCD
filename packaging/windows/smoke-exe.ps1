# Smoke a staged or installed Windows executable.
#
# GUI-subsystem executables do not set $LASTEXITCODE when invoked with `&`
# from PowerShell, so always wait for the process and inspect ExitCode.
param(
    [Parameter(Mandatory = $true)]
    [string]$Exe
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $Exe)) {
    throw "executable not found: $Exe"
}

foreach ($arg in @("--version", "--i18n-check")) {
    $proc = Start-Process -FilePath $Exe -ArgumentList @($arg) -NoNewWindow -Wait -PassThru
    Write-Host "$Exe $arg exit code: $($proc.ExitCode)"
    if ($proc.ExitCode -ne 0) {
        throw "$Exe $arg failed with $($proc.ExitCode)"
    }
}
