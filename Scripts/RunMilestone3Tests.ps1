param(
    [string]$EngineRoot = "C:\\Program Files\\Epic Games\\UE_5.5",
    [string]$ProjectPath,
    [switch]$ArchiveLogs
)

function Resolve-ExistingPath {
    param([string]$Path)

    if (-not (Test-Path $Path))
    {
        throw "Path not found: $Path"
    }

    return (Resolve-Path $Path).Path
}

$EngineRoot = Resolve-ExistingPath -Path $EngineRoot

if ([string]::IsNullOrWhiteSpace($ProjectPath))
{
    $documentsPath = [Environment]::GetFolderPath("MyDocuments")
    if ([string]::IsNullOrWhiteSpace($documentsPath))
    {
        throw "Unable to resolve My Documents path for default project location."
    }

    $ProjectPath = Join-Path $documentsPath "Unreal Projects\\PlanetaryCreation\\PlanetaryCreation.uproject"
}
elseif ($ProjectPath.StartsWith("\\\\wsl$", [System.StringComparison]::OrdinalIgnoreCase))
{
    # Convert UNC (WSL) path to Windows drive path (requires wsl.exe)
    $converted = & wsl.exe wslpath -w $ProjectPath 2>$null
    if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($converted))
    {
        $ProjectPath = $converted.Trim()
    }
}

$ProjectPath = Resolve-ExistingPath -Path $ProjectPath
$ProjectDirectory = Split-Path $ProjectPath -Parent

function Invoke-Process {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$Description,
        [switch]$FailFast
    )

    Write-Host $Description -ForegroundColor Cyan

    $ProcessInfo = Start-Process -FilePath $FilePath -ArgumentList $Arguments -Wait -PassThru -NoNewWindow

    if ($ProcessInfo.ExitCode -ne 0)
    {
        if ($FailFast)
        {
            throw "'$Description' failed with exit code $($ProcessInfo.ExitCode)."
        }

        Write-Warning "'$Description' returned exit code $($ProcessInfo.ExitCode)."
    }

    return $ProcessInfo.ExitCode
}

$EditorCmd = Join-Path $EngineRoot "Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe"
if (-not (Test-Path $EditorCmd))
{
    throw "UnrealEditor-Cmd.exe not found at: $EditorCmd"
}

$BuildBat = Join-Path $EngineRoot "Engine\\Build\\BatchFiles\\Build.bat"
if (-not (Test-Path $BuildBat))
{
    throw "Build.bat not found at: $BuildBat"
}

$LogDirectory = Join-Path $ProjectDirectory "Saved\\Logs"
if (-not (Test-Path $LogDirectory))
{
    New-Item -ItemType Directory -Path $LogDirectory | Out-Null
}

$ActiveLogPath = Join-Path $LogDirectory "PlanetaryCreation.log"
$AutomationLogRoot = Join-Path $LogDirectory "Automation"
if ($ArchiveLogs -and -not (Test-Path $AutomationLogRoot))
{
    New-Item -ItemType Directory -Path $AutomationLogRoot | Out-Null
}

$TestNames = @(
    "PlanetaryCreation.Milestone3.IcosphereSubdivision",
    "PlanetaryCreation.Milestone3.VoronoiMapping",
    "PlanetaryCreation.Milestone3.KDTreePerformance",
    "PlanetaryCreation.QuantitativeMetrics.Export"
)

$Results = @()
$OverallExitCode = 0
$BuildSucceeded = $false
$LocationPushed = $false

try
{
    Push-Location -Path $ProjectDirectory
    $LocationPushed = $true

    # --------------------------------------------------------------
    # Build step
    # --------------------------------------------------------------
    $BuildArgs = @(
        "PlanetaryCreationEditor",
        "Win64",
        "Development",
        "-project=`"$ProjectPath`"",
        "-WaitMutex",
        "-FromMsBuild"
    )

    Invoke-Process -FilePath $BuildBat -Arguments $BuildArgs -Description "Building PlanetaryCreationEditor (Development | Win64)" -FailFast
    $BuildSucceeded = $true

    # --------------------------------------------------------------
    # Automation tests
    # --------------------------------------------------------------
    $RunAutomationFilter = [string]::Join('+', $TestNames)

    $Arguments = @(
        ("`"{0}`"" -f $ProjectPath),
        "-SetCVar=r.PlanetaryCreation.PaperDefaults=0",
        ("-ExecCmds=`"Automation RunTests {0}`"" -f $RunAutomationFilter),
        '-TestExit="Automation Test Queue Empty"',
        "-unattended",
        "-nop4",
        "-nosplash",
        "-log"
    )

    $AutomationExitCode = Invoke-Process -FilePath $EditorCmd -Arguments $Arguments -Description "Running Milestone 3 + Quantitative Metrics automation"

    if ($AutomationExitCode -ne 0)
    {
        $OverallExitCode = 1
    }

    if (-not (Test-Path $ActiveLogPath))
    {
        Write-Warning "Log file not found after automation run."
        foreach ($Name in $TestNames)
        {
            $Results += [PSCustomObject]@{ Stage = "Automation"; Name = $Name; Result = "Missing Log"; Log = $null }
        }
        $OverallExitCode = 1
    }
    else
    {
        $LogCapturePath = $null
        if ($ArchiveLogs)
        {
            $Timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
            $LogCapturePath = Join-Path $AutomationLogRoot "${Timestamp}_Milestone3Suite.log"
            Copy-Item -Path $ActiveLogPath -Destination $LogCapturePath -Force
        }

        foreach ($Name in $TestNames)
        {
            $Pattern = "Path=\{$([Regex]::Escape($Name))\}"
            $ResultLine = Select-String -Path $ActiveLogPath -Pattern $Pattern | Select-Object -Last 1
            $ParsedResult = "Unknown"
            $AutomationExit = $AutomationExitCode

            if ($null -ne $ResultLine -and $ResultLine.Line -match "Result=\{(?<Result>[^}]*)\}")
            {
                $ParsedResult = $Matches.Result
                if ($ParsedResult -ne "Success")
                {
                    $OverallExitCode = 1
                }
            }
            else
            {
                Write-Warning ("Unable to parse result for {0} from {1}" -f $Name, $ActiveLogPath)
                $OverallExitCode = 1
            }

            $Results += [PSCustomObject]@{
                Stage = "Automation"
                Name  = $Name
                Result = $ParsedResult
                ExitCode = $AutomationExit
                Log = if ($LogCapturePath) { $LogCapturePath } else { $ActiveLogPath }
            }
        }

        try
        {
            $MetricsDirectory = Join-Path $ProjectDirectory "Saved\Metrics"
            if (Test-Path $MetricsDirectory)
            {
                $LatestMetrics = Get-ChildItem -Path $MetricsDirectory -Filter "heightmap_export_metrics_*.csv" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
                if ($null -ne $LatestMetrics)
                {
                    $DocsValidation = Join-Path $ProjectDirectory "Docs\Validation"
                    if (-not (Test-Path $DocsValidation))
                    {
                        New-Item -ItemType Directory -Path $DocsValidation | Out-Null
                    }

                    $DestinationPath = Join-Path $DocsValidation $LatestMetrics.Name
                    Copy-Item -Path $LatestMetrics.FullName -Destination $DestinationPath -Force
                }
            }
        }
        catch
        {
            Write-Warning ("Failed to mirror metrics CSV to Docs\Validation: {0}" -f $_.Exception.Message)
        }
    }
}
catch
{
    Write-Error $_.Exception.Message
    $OverallExitCode = 1
}
finally
{
    if ($LocationPushed)
    {
        try { Pop-Location -ErrorAction Stop | Out-Null } catch { }
    }
}

Write-Host ""  # spacer
Write-Host "Build & Automation Summary" -ForegroundColor Green

$Summary = @()
$Summary += [PSCustomObject]@{
    Stage = "Build"
    Name  = "PlanetaryCreationEditor (Win64 | Development)"
    Result = if ($BuildSucceeded) { "Success" } else { "Failure" }
    Log = "See console output"
}

if (-not $BuildSucceeded)
{
    $OverallExitCode = 1
}

foreach ($Entry in $Results)
{
    $Summary += [PSCustomObject]@{
        Stage = $Entry.Stage
        Name  = $Entry.Name
        Result = $Entry.Result
        Log = $Entry.Log
    }
}

$Summary | Format-Table -AutoSize

if ($OverallExitCode -ne 0)
{
    Write-Warning "One or more stages reported failures. Review logs above for details."
}
else
{
    Write-Host "Build and automation completed successfully." -ForegroundColor Green
}

exit $OverallExitCode
