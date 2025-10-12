param(
    [switch]$UseNullRHI = $true
)

$projectRoot = 'C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation'
$uprojectPath = Join-Path $projectRoot 'PlanetaryCreation.uproject'
$editorCmd = 'C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$pythonScript = Join-Path $projectRoot 'Scripts\RunHeightmapTelemetryPass.py'

if (-not (Test-Path $editorCmd)) {
    throw "UnrealEditor-Cmd.exe not found at $editorCmd"
}
if (-not (Test-Path $uprojectPath)) {
    throw "Project file not found at $uprojectPath"
}
if (-not (Test-Path $pythonScript)) {
    throw "Python script not found at $pythonScript"
}

$arguments = @(
    ("`"{0}`"" -f $uprojectPath)
    '-unattended'
    '-nop4'
    '-nosplash'
    '-log'
    '-run=pythonscript'
    ("-script=`"{0}`"" -f $pythonScript)
)

if ($UseNullRHI.IsPresent) {
    $arguments += '-NullRHI'
}

Write-Host ("Executing UnrealEditor-Cmd.exe {0}" -f ($arguments -join ' '))
& $editorCmd @arguments
$exitCode = $LASTEXITCODE
if ($exitCode -ne 0) {
    throw "UnrealEditor-Cmd.exe exited with code $exitCode"
}
