param(
    [ValidateSet("menuconfig", "build", "flash-monitor")]
    [string]$Action = "build",
    [string]$Port = ""
)

$projectDirectory = $PSScriptRoot
$buildDirectory = Join-Path $projectDirectory "build-control"
$sdkconfigPath = Join-Path $projectDirectory "sdkconfig.control"
$defaultsPath = "sdkconfig.defaults;sdkconfig.control.defaults"

$idfArguments = @(
    "-B", $buildDirectory,
    "-D", "SDKCONFIG=$sdkconfigPath",
    "-D", "SDKCONFIG_DEFAULTS=$defaultsPath"
)

Push-Location $projectDirectory
try {
    if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
        throw "idf.py was not found. Run this script from an ESP-IDF PowerShell."
    }

    if ($Action -eq "flash-monitor") {
        if ([string]::IsNullOrWhiteSpace($Port)) {
            throw "flash-monitor requires -Port COMx"
        }
        & idf.py @idfArguments -p $Port flash monitor
    } elseif ($Action -eq "build") {
        & idf.py @idfArguments reconfigure
        if ((-not $?) -or ($LASTEXITCODE -ne 0)) {
            exit $LASTEXITCODE
        }
        & idf.py @idfArguments build
    } else {
        & idf.py @idfArguments $Action
    }

    if (-not $?) {
        throw "idf.py could not be executed. Check the ESP-IDF PowerShell environment."
    }
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
} finally {
    Pop-Location
}
