param(
    [string]$ForceExemplar = "O01",
    [int]$RenderLOD = 7,
    [string]$RawExportPath,
    [int]$TimeoutSeconds = 0,
    [switch]$TraceTiles,
    [switch]$TraceBlend,
    [switch]$TraceSampler
)

$ErrorActionPreference = "Stop"

$env:PLANETARY_STAGEB_FORCE_CPU = "1"
$env:PLANETARY_STAGEB_FORCE_EXEMPLAR = $ForceExemplar
$env:PLANETARY_STAGEB_DISABLE_RANDOM_OFFSET = "1"
$env:PLANETARY_STAGEB_RENDER_LOD = $RenderLOD.ToString()
if ($TraceTiles) {
    $env:PLANETARY_STAGEB_TRACE_TILE_PROGRESS = "1"
} else {
    $env:PLANETARY_STAGEB_TRACE_TILE_PROGRESS = ""
}

if ($TraceBlend) {
    $env:PLANETARY_STAGEB_TRACE_CONTINENTAL_BLEND = "1"
} else {
    $env:PLANETARY_STAGEB_TRACE_CONTINENTAL_BLEND = ""
}

if ($TraceSampler) {
    $env:PLANETARY_TRACE_HEIGHTMAP_SAMPLER = "1"
} else {
    $env:PLANETARY_TRACE_HEIGHTMAP_SAMPLER = ""
}

if ([string]::IsNullOrWhiteSpace($RawExportPath)) {
    $defaultDirectory = Join-Path -Path $PSScriptRoot -ChildPath "..\Docs\Validation\ExemplarAudit"
    $resolvedPath = Resolve-Path -Path $defaultDirectory -ErrorAction SilentlyContinue
    if ($null -eq $resolvedPath) {
        New-Item -ItemType Directory -Path $defaultDirectory -Force | Out-Null
        $resolvedPath = Resolve-Path -Path $defaultDirectory
    }
    $resolvedDirectory = $resolvedPath.ProviderPath
    $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $RawExportPath = Join-Path -Path $resolvedDirectory -ChildPath "${ForceExemplar}_stageb_${stamp}.csv"
}
$env:PLANETARY_STAGEB_RAW_EXPORT = $RawExportPath

$uproject = "C:\\Users\\Michael\\Documents\\Unreal Projects\\PlanetaryCreation\\PlanetaryCreation.uproject"
$cmdExe = "C:\\Program Files\\Epic Games\\UE_5.5\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe"
$scriptPath = "C:\\Users\\Michael\\Documents\\Unreal Projects\\PlanetaryCreation\\Scripts\\ExportHeightmap512.py"

$arguments = @(
    '"' + $uproject + '"',
    '-SetCVar="r.PlanetaryCreation.PaperDefaults=0,r.PlanetaryCreation.UseGPUAmplification=0,r.PlanetaryCreation.SkipCPUAmplification=0"',
    '-run=pythonscript',
    '-script="' + $scriptPath + '"',
    '-NullRHI',
   '-unattended',
    '-nop4',
    '-nosplash',
    '-log'
)

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $cmdExe
$psi.Arguments = $arguments -join ' '
$psi.UseShellExecute = $false
$psi.CreateNoWindow = $true

# Pass environment variables explicitly (Start-Process doesn't inherit them)
$psi.EnvironmentVariables["PLANETARY_STAGEB_FORCE_CPU"] = "1"
$psi.EnvironmentVariables["PLANETARY_STAGEB_FORCE_EXEMPLAR"] = $ForceExemplar
$psi.EnvironmentVariables["PLANETARY_STAGEB_DISABLE_RANDOM_OFFSET"] = "1"
$psi.EnvironmentVariables["PLANETARY_STAGEB_RENDER_LOD"] = $RenderLOD.ToString()
$psi.EnvironmentVariables["PLANETARY_STAGEB_RAW_EXPORT"] = $RawExportPath

if ($TraceTiles) {
    $psi.EnvironmentVariables["PLANETARY_STAGEB_TRACE_TILE_PROGRESS"] = "1"
}
if ($TraceBlend) {
    $psi.EnvironmentVariables["PLANETARY_STAGEB_TRACE_CONTINENTAL_BLEND"] = "1"
}

$process = [System.Diagnostics.Process]::Start($psi)
try {
    if ($TimeoutSeconds -gt 0) {
        $timeoutMilliseconds = [Math]::Max(1, $TimeoutSeconds * 1000)
        if (-not $process.WaitForExit($timeoutMilliseconds)) {
            Write-Warning ("[RunExportHeightmap512] Timeout hit ({0}s). Terminating UnrealEditor-Cmd.exe (PID {1})." -f $TimeoutSeconds, $process.Id)
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            throw "UnrealEditor-Cmd.exe exceeded timeout and was terminated."
        }
    }
    else {
        $process.WaitForExit()
    }
}
finally {
    if (($process -ne $null) -and (-not $process.HasExited)) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    }
}
