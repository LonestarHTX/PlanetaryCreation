param(
    [Parameter(Mandatory = $true)]
    [string]$TileId,

    [Parameter(Mandatory = $true)]
    [string]$StageCsv,

    [Parameter(Mandatory = $true)]
    [string]$MetricsCsv,

    [Parameter(Mandatory = $true)]
    [string]$ComparisonPng,

    [string]$ExemplarPng = "C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\Content\PlanetaryCreation\Exemplars\PNG16\O01.png",
    [string]$ExemplarLibrary = "C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\Content\PlanetaryCreation\Exemplars\ExemplarLibrary.json",

    [double]$MeanDiffThreshold = 50.0,
    [double]$InteriorDiffThreshold = 100.0,
    [double]$SpikeWarningThreshold = 750.0,
    [switch]$EnablePerimeterMask,
    [switch]$TraceBlend
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -Path $StageCsv)) {
    throw "Stage CSV not found: $StageCsv"
}

$metricsDirectory = Split-Path -Path $MetricsCsv -Parent
if (-not (Test-Path -Path $metricsDirectory)) {
    New-Item -ItemType Directory -Path $metricsDirectory -Force | Out-Null
}

$comparisonDirectory = Split-Path -Path $ComparisonPng -Parent
if (-not (Test-Path -Path $comparisonDirectory)) {
    New-Item -ItemType Directory -Path $comparisonDirectory -Force | Out-Null
}

$env:PLANETARY_STAGEB_ANALYZER_TILE_ID = $TileId
$env:PLANETARY_STAGEB_ANALYZER_STAGE_CSV = $StageCsv
$env:PLANETARY_STAGEB_ANALYZER_EXEMPLAR_PNG = $ExemplarPng
$env:PLANETARY_STAGEB_ANALYZER_EXEMPLAR_JSON = $ExemplarLibrary
$env:PLANETARY_STAGEB_ANALYZER_METRICS_CSV = $MetricsCsv
$env:PLANETARY_STAGEB_ANALYZER_COMPARISON_PNG = $ComparisonPng
$env:PLANETARY_STAGEB_ANALYZER_MEAN_THRESHOLD = "{0:F3}" -f $MeanDiffThreshold
$env:PLANETARY_STAGEB_ANALYZER_INTERIOR_THRESHOLD = "{0:F3}" -f $InteriorDiffThreshold
$env:PLANETARY_STAGEB_ANALYZER_SPIKE_THRESHOLD = "{0:F3}" -f $SpikeWarningThreshold
if ($EnablePerimeterMask.IsPresent) {
    $env:PLANETARY_STAGEB_ANALYZER_ENABLE_MASK = "1"
} else {
    $env:PLANETARY_STAGEB_ANALYZER_ENABLE_MASK = ""
}
if ($TraceBlend.IsPresent) {
    $env:PLANETARY_STAGEB_TRACE_CONTINENTAL_BLEND = "1"
    Write-Host ("[RunExemplarAnalyzer] Continental blend tracing enabled. Env value={0}" -f $env:PLANETARY_STAGEB_TRACE_CONTINENTAL_BLEND)
}

$uproject = "C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\PlanetaryCreation.uproject"
$pythonScript = "C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\Scripts\analyze_exemplar_fidelity.py"
$editorCmd = "C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"

$arguments = @(
    "`"$uproject`"",
    "-run=pythonscript",
    "`"-script=$pythonScript`"",
    "-NullRHI",
    "-unattended",
    "-nop4",
    "-nosplash",
    "-log"
)

if ($TraceBlend.IsPresent) {
    $arguments += '-LogCmds="LogPlanetaryCreation Log"'
}

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $editorCmd
$psi.Arguments = $arguments -join ' '
$psi.UseShellExecute = $false
$psi.CreateNoWindow = $true

# Pass environment variables explicitly (Start-Process doesn't inherit them)
$psi.EnvironmentVariables["PLANETARY_STAGEB_ANALYZER_TILE_ID"] = $TileId
$psi.EnvironmentVariables["PLANETARY_STAGEB_ANALYZER_STAGE_CSV"] = $StageCsv
$psi.EnvironmentVariables["PLANETARY_STAGEB_ANALYZER_EXEMPLAR_PNG"] = $ExemplarPng
$psi.EnvironmentVariables["PLANETARY_STAGEB_ANALYZER_EXEMPLAR_JSON"] = $ExemplarLibrary
$psi.EnvironmentVariables["PLANETARY_STAGEB_ANALYZER_METRICS_CSV"] = $MetricsCsv
$psi.EnvironmentVariables["PLANETARY_STAGEB_ANALYZER_COMPARISON_PNG"] = $ComparisonPng
$psi.EnvironmentVariables["PLANETARY_STAGEB_ANALYZER_MEAN_THRESHOLD"] = ("{0:F3}" -f $MeanDiffThreshold)
$psi.EnvironmentVariables["PLANETARY_STAGEB_ANALYZER_INTERIOR_THRESHOLD"] = ("{0:F3}" -f $InteriorDiffThreshold)
$psi.EnvironmentVariables["PLANETARY_STAGEB_ANALYZER_SPIKE_THRESHOLD"] = ("{0:F3}" -f $SpikeWarningThreshold)
if ($EnablePerimeterMask.IsPresent) {
    $psi.EnvironmentVariables["PLANETARY_STAGEB_ANALYZER_ENABLE_MASK"] = "1"
}
if ($TraceBlend.IsPresent) {
    $psi.EnvironmentVariables["PLANETARY_STAGEB_TRACE_CONTINENTAL_BLEND"] = "1"
    Write-Host ("[RunExemplarAnalyzer] PSI TraceBlend env={0}" -f $psi.EnvironmentVariables["PLANETARY_STAGEB_TRACE_CONTINENTAL_BLEND"])
}

$process = [System.Diagnostics.Process]::Start($psi)
$process.WaitForExit()
