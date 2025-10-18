# RunOceanicValidation.ps1
# Runs OceanicCrust test to generate CSV validation data for Phase 5

param(
    [int]$TimeoutSeconds = 120
)

$ProjectRoot = "C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation"
$ProjectFile = "$ProjectRoot\PlanetaryCreation.uproject"
$UnrealCmd = "C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"

Write-Host "═══════════════════════════════════════════════════════════" -ForegroundColor Cyan
Write-Host " Phase 5 Oceanic Crust Validation Test Runner" -ForegroundColor Cyan
Write-Host "═══════════════════════════════════════════════════════════" -ForegroundColor Cyan
Write-Host ""

# Run the test
Write-Host "[1/2] Running OceanicCrust test..." -ForegroundColor Yellow
& $UnrealCmd $ProjectFile `
    -ExecCmds="Automation RunTests PlanetaryCreation.Paper.OceanicCrust" `
    -TestExit="Automation Test Queue Empty" `
    -unattended -nop4 -nosplash

Write-Host ""
Write-Host "[2/2] Checking for CSV outputs..." -ForegroundColor Yellow

$ValidationDir = "$ProjectRoot\Docs\Automation\Validation\Phase5"
$ExpectedFiles = @(
    "oceanic_elevation_profile.csv",
    "ridge_directions.csv",
    "cross_boundary_transect.csv"
)

$FoundCount = 0
foreach ($file in $ExpectedFiles) {
    $fullPath = Join-Path $ValidationDir $file
    if (Test-Path $fullPath) {
        $size = (Get-Item $fullPath).Length
        Write-Host "  [OK] $file ($size bytes)" -ForegroundColor Green
        $FoundCount++
    } else {
        Write-Host "  [MISSING] $file" -ForegroundColor Red
    }
}

Write-Host ""
if ($FoundCount -eq $ExpectedFiles.Count) {
    Write-Host "SUCCESS: All $FoundCount CSV files generated!" -ForegroundColor Green
    exit 0
} else {
    Write-Host "PARTIAL: Only $FoundCount/$($ExpectedFiles.Count) CSV files found" -ForegroundColor Yellow
    Write-Host "Check logs at: $ProjectRoot\Saved\Logs\PlanetaryCreation.log" -ForegroundColor Cyan
    exit 1
}
