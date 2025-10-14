# Scripts\DownloadStageB_Tiles.ps1
# Download 1x1-degree SRTMGL1 tiles via OpenTopography by parsing codes like N27E086

$ErrorActionPreference = 'Stop'

# --- config/output ---
$OutDir = Join-Path $PSScriptRoot "..\StageB_SRTM90\raw" | Resolve-Path -ErrorAction SilentlyContinue
if (-not $OutDir) { $OutDir = Join-Path $PSScriptRoot "..\StageB_SRTM90\raw" }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# --- API key ---
$KeyFile = Join-Path $PSScriptRoot "..\opentopo_key.txt"
if (-not (Test-Path $KeyFile)) {
    throw "API key file not found: $KeyFile`nCreate opentopo_key.txt in project root with your OpenTopography API key."
}
$RawKey = (Get-Content $KeyFile -Raw).Trim()
if ([string]::IsNullOrWhiteSpace($RawKey)) {
    throw "API key file is empty: $KeyFile"
}
$ApiKey = [uri]::EscapeDataString($RawKey)
Write-Host "[INFO] Using API key from: $KeyFile"

function Parse-TileCode {
    param([string]$code)
    if ($code -notmatch '^(?<NS>[NS])(?<LAT>\d{2})(?<EW>[EW])(?<LON>\d{3})$') {
        throw "Bad tile code: $code"
    }
    $lat = [int]$Matches['LAT']
    $lon = [int]$Matches['LON']
    if ($Matches['NS'] -eq 'S') { $lat = -$lat }
    if ($Matches['EW'] -eq 'W') { $lon = -$lon }
    # Integer 1x1 degree bbox
    @{ south=$lat; north=$lat+1; west=$lon; east=$lon+1 }
}

function Get-OT-Url {
    param($bbox)
    $qs = "demtype=SRTMGL1&south={0}&north={1}&west={2}&east={3}&outputFormat=GTiff&API_Key={4}" -f `
          $bbox.south, $bbox.north, $bbox.west, $bbox.east, $ApiKey
    "https://portal.opentopography.org/API/globaldem?$qs"
}

function Download-OT-Tile {
    param(
        [string]$code,
        [string]$label
    )
    $bbox = Parse-TileCode $code
    $url  = Get-OT-Url $bbox
    $outfile = Join-Path $OutDir ("{0}_{1}_{2}_{3}_{4}.tif" -f $code,$bbox.south,$bbox.north,$bbox.west,$bbox.east)

    if (Test-Path $outfile) {
        Write-Host "[SKIP] $label -> $code (already exists)"
        return
    }

    Write-Host "[DL] $label -> $code  bbox=($($bbox.south),$($bbox.west))-($($bbox.north),$($bbox.east))"

    try {
        Invoke-WebRequest -Uri $url -OutFile $outfile -TimeoutSec 900 -UseBasicParsing

        # sanity check: reject HTML masquerading as tif
        if ((Get-Item $outfile).Length -lt 50000) {
            $head = Get-Content $outfile -TotalCount 1 -ErrorAction SilentlyContinue
            if ($head -match '<!DOCTYPE|<html') { throw "Server returned an HTML error page." }
        }
    } catch {
        # Remove only if we just created an empty/partial file
        if (Test-Path $outfile) {
            try {
                Remove-Item $outfile -Force -ErrorAction Stop
            } catch {
                Write-Host ("[WARN] Failed to clean partial file for {0}: {1}" -f $code, $_.Exception.Message)
            }
        }
        Write-Host "[ERROR] $($code): $($_.Exception.Message)"
        return
    }
}

# --- derive tile codes from catalog ---
$catalogPath = Join-Path $PSScriptRoot "..\Docs\StageB_SRTM_Exemplar_Catalog.csv"
if (-not (Test-Path $catalogPath)) {
    throw "Catalog not found: $catalogPath"
}

$tileMap = @{}  # code -> list of exemplar labels

Import-Csv -Path $catalogPath | ForEach-Object {
    $id = ($_.id).Trim()
    if (-not $id -or $id.StartsWith("#")) { return }

    $feature = ($_.feature).Trim()
    $region = ($_.region).Trim()
    $label = if ($feature) { "$id ($region - $feature)" } else { "$id ($region)" }

    $codes = ($_.tiles -split '[;,]') | ForEach-Object { $_.Trim().ToUpper() } | Where-Object { $_ }
    foreach ($code in $codes) {
        if (-not $tileMap.ContainsKey($code)) {
            $tileMap[$code] = New-Object System.Collections.Generic.List[string]
        }
        $tileMap[$code].Add($label)
    }
}

if ($tileMap.Count -eq 0) {
    throw "No tile codes resolved from catalog: $catalogPath"
}

foreach ($code in ($tileMap.Keys | Sort-Object)) {
    $labels = $tileMap[$code] | Sort-Object
    $labelText = $labels -join ", "
    Download-OT-Tile -code $code -label $labelText
}

Write-Host "All tiles processed. Files saved to $OutDir"
