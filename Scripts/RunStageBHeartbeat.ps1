param(
  [switch]$UseGPU,
  [int]$ThrottleMs = 0
)

function Remove-HeartbeatLog {
  param(
    [string]$Path
  )

  if (-not (Test-Path $Path)) {
    return
  }

  $maxAttempts = 3
  for ($attempt = 1; $attempt -le $maxAttempts; $attempt++) {
    try {
      Remove-Item -Path $Path -Force -ErrorAction Stop
      return
    } catch {
      if ($attempt -eq $maxAttempts) {
        Write-Warning ("Unable to remove {0}: {1}" -f $Path, $_.Exception.Message)
      } else {
        Start-Sleep -Milliseconds 250
      }
    }
  }
}

$useGpuFlag = $UseGPU.IsPresent
$mode = if ($useGpuFlag) { 'enabled' } else { 'disabled' }
Write-Host ("Launching StageB heartbeat automation (GPU amplification {0})..." -f $mode)
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$projectRoot = 'C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation'
Set-Location $projectRoot
$uprojectPath = Join-Path $projectRoot 'PlanetaryCreation.uproject'
$outputPath = Join-Path $projectRoot 'Saved\Logs\StageBHeartbeatCmd.out.log'
$errorPath = Join-Path $projectRoot 'Saved\Logs\StageBHeartbeatCmd.err.log'
# CRITICAL: CVars must use -SetCVar, NOT -ExecCmds (they execute too late otherwise)
$setCVars = @()
if ($ThrottleMs -gt 0) {
  $setCVars += ("r.PlanetaryCreation.StageBThrottleMs={0}" -f $ThrottleMs)
}
$gpuValue = if ($useGpuFlag) { 1 } else { 0 }
$setCVars += ("r.PlanetaryCreation.UseGPUAmplification={0}" -f $gpuValue)
$setCVars += 'r.PlanetaryCreation.StageBProfiling=1'
$setCVarString = [string]::Join(',', $setCVars)

$arguments = @(
  ("`"{0}`"" -f $uprojectPath)
  ("-SetCVar={0}" -f $setCVarString)
  '-ExecCmds="Automation RunTests PlanetaryCreation.Heightmap.SampleInterpolation"'
  '-TestExit="Automation Test Queue Empty"'
  '-unattended'
  '-nop4'
  '-nosplash'
  '-NullRHI'
  '-log'
)
$editorCmd = 'C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
Write-Host ('Executing: {0} {1}' -f $editorCmd, ($arguments -join ' '))

Remove-HeartbeatLog -Path $outputPath
Remove-HeartbeatLog -Path $errorPath

$process = Start-Process -FilePath $editorCmd -ArgumentList $arguments -RedirectStandardOutput $outputPath -RedirectStandardError $errorPath -Wait -PassThru
$exitCode = $process.ExitCode
Write-Host ("UnrealEditor-Cmd.exe exited with code {0}" -f $exitCode)
if ($null -ne $process) {
  $process.Dispose()
}
if (Test-Path $outputPath) {
  Write-Host '--- StdOut ---'
  Get-Content $outputPath
}
if (Test-Path $errorPath) {
  Write-Host '--- StdErr ---'
  Get-Content $errorPath
}

$mainLogPath = Join-Path $projectRoot 'Saved\Logs\PlanetaryCreation.log'
if (Test-Path $mainLogPath) {
  Write-Host '--- StageB Heartbeat (tail) ---'
  Select-String -Path $mainLogPath -Pattern '\[StageBDiag\]|\[StageB\]\[Profile\]' | Select-Object -Last 20

  $archivePath = Join-Path $projectRoot ("Saved\Logs\StageBHeartbeat-{0}.log" -f $timestamp)
  Copy-Item $mainLogPath $archivePath -Force
  Write-Host ("Archived log to {0}" -f $archivePath)
} else {
  Write-Warning 'PlanetaryCreation.log not found; skipping StageB heartbeat tail capture.'
}
