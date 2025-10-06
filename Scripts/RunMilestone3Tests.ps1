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

$Tests = @(
    "PlanetaryCreation.Milestone3.IcosphereSubdivision",
    "PlanetaryCreation.Milestone3.VoronoiMapping",
    "PlanetaryCreation.Milestone3.KDTreePerformance"
)

$Results = @()
$OverallExitCode = 0
$BuildSucceeded = $false

try
{
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
    foreach ($TestName in $Tests)
    {
        $Arguments = @(
            $ProjectPath,
            "-ExecCmds=Automation RunTests $TestName; Quit",
            "-unattended",
            "-nop4",
            "-nosplash",
            "-log"
        )

        $ExitCode = Invoke-Process -FilePath $EditorCmd -Arguments $Arguments -Description "Running $TestName"

        if ($ExitCode -ne 0)
        {
            $OverallExitCode = 1
        }

        if (-not (Test-Path $ActiveLogPath))
        {
            Write-Warning "Log file not found after running $TestName."
            $Results += [PSCustomObject]@{ Stage = "Automation"; Name = $TestName; Result = "Missing Log"; Log = $null }
            $OverallExitCode = 1
            continue
        }

        $ResultLine = Select-String -Path $ActiveLogPath -Pattern "Test Completed\. Result=" | Select-Object -Last 1
        $ParsedResult = "Unknown"

        if ($null -ne $ResultLine -and $ResultLine.Line -match "Result=\{(?<Result>[^}]*)\} Name=\{(?<Name>[^}]*)\}")
        {
            $ParsedResult = $Matches.Result
        }
        else
        {
            Write-Warning "Unable to parse result for $TestName from $ActiveLogPath."
            $OverallExitCode = 1
        }

        if ($ParsedResult -ne "Success")
        {
            $OverallExitCode = 1
        }

        $ExitLine = Select-String -Path $ActiveLogPath -Pattern "\*\*\*\* TEST COMPLETE\. EXIT CODE" | Select-Object -Last 1
        $AutomationExit = $null
        if ($null -ne $ExitLine -and $ExitLine.Line -match "EXIT CODE: (?<Code>\d+)")
        {
            $AutomationExit = [int]$Matches.Code
            if ($AutomationExit -ne 0)
            {
                $OverallExitCode = 1
            }
        }

        $LogCapturePath = $null
        if ($ArchiveLogs)
        {
            $Timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
            $SafeName = $TestName -replace '[^A-Za-z0-9_-]', '_'
            $LogCapturePath = Join-Path $AutomationLogRoot "${Timestamp}_${SafeName}.log"
            Copy-Item -Path $ActiveLogPath -Destination $LogCapturePath -Force
        }

        $Results += [PSCustomObject]@{
            Stage = "Automation"
            Name  = $TestName
            Result = $ParsedResult
            ExitCode = if ($AutomationExit -ne $null) { $AutomationExit } else { $ExitCode }
            Log = if ($LogCapturePath) { $LogCapturePath } else { $ActiveLogPath }
        }
    }
}
catch
{
    Write-Error $_.Exception.Message
    $OverallExitCode = 1
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
