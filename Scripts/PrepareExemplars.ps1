Param(
    [Parameter(Mandatory=$true)][string]$InputRaster, # Source DEM GeoTIFF/COG
    [Parameter(Mandatory=$true)][string]$OutDir,       # Output root directory
    [Parameter(Mandatory=$true)][string]$Region,       # Himalayan | Andean
    [Parameter(Mandatory=$true)][string]$Site,         # Everest_A | CentralAndes_A | ...
    [Parameter(Mandatory=$true)][double]$ULat,         # Upper-left latitude
    [Parameter(Mandatory=$true)][double]$ULon,         # Upper-left longitude
    [Parameter(Mandatory=$true)][double]$LLat,         # Lower-right latitude
    [Parameter(Mandatory=$true)][double]$LLon,         # Lower-right longitude
    [int]$SizePx = 512,
    [string]$LicenseName = "",
    [string]$LicenseAttribution = "",
    [string]$SourceUrl = ""
)

$ErrorActionPreference = "Stop"

function Ensure-Dir([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Path $Path | Out-Null
    }
}

function Require-Tool([string]$Tool) {
    $cmd = Get-Command $Tool -ErrorAction SilentlyContinue
    if (-not $cmd) { throw "Required tool '$Tool' not found in PATH." }
}

Require-Tool gdalinfo
Require-Tool gdalwarp
Require-Tool gdal_translate

$cogDir = Join-Path $OutDir "Content/PlanetaryCreation/Exemplars/COG"
$pngDir = Join-Path $OutDir "Content/PlanetaryCreation/Exemplars/PNG16"
$libPath = Join-Path $OutDir "Content/PlanetaryCreation/Exemplars/ExemplarLibrary.json"

Ensure-Dir $cogDir
Ensure-Dir $pngDir

$id = "{0}_{1}" -f $Region,$Site
$tmp = Join-Path $env:TEMP ("exemplar_" + [System.Guid]::NewGuid().ToString("N"))
Ensure-Dir $tmp

try {
    $reproj = Join-Path $tmp "reproj.tif"
    $crop = Join-Path $tmp "crop.tif"
    $resampled = Join-Path $tmp "resampled.tif"
    $outCog = Join-Path $cogDir ("{0}.tif" -f $id)
    $outPng = Join-Path $pngDir ("{0}.png" -f $id)

    # Reproject to EPSG:4326
    & gdalwarp -t_srs EPSG:4326 -r cubicspline -multi -overwrite --config GDAL_NUM_THREADS ALL_CPUS `
        "$InputRaster" "$reproj" | Write-Verbose

    # Crop using projwin ULX ULY LRX LRY
    $ulx = $ULon; $uly = $ULat; $lrx = $LLon; $lry = $LLat
    & gdal_translate -projwin $ulx $uly $lrx $lry -r bilinear "$reproj" "$crop" | Write-Verbose

    # Fill nodata (best-effort)
    $fillScript = Get-Command gdal_fillnodata.py -ErrorAction SilentlyContinue
    if ($fillScript) {
        & $fillScript "$crop" "$crop" | Write-Verbose
    }

    # Resample exact size
    & gdalwarp -t_srs EPSG:4326 -ts $SizePx $SizePx -r cubicspline -multi --config GDAL_NUM_THREADS ALL_CPUS `
        "$crop" "$resampled" | Write-Verbose

    # Compute min/max
    $stats = & gdalinfo -stats "$resampled"
    $minMatch = ($stats | Select-String -Pattern "Minimum=(?<min>[-0-9\.]+), Maximum=(?<max>[-0-9\.]+)")
    if (-not $minMatch) { throw "Failed to extract min/max from gdalinfo." }
    $minVal = [double]$minMatch.Matches[0].Groups["min"].Value
    $maxVal = [double]$minMatch.Matches[0].Groups["max"].Value

    # Write COG
    & gdal_translate -of COG -co COMPRESS=LZW -co NUM_THREADS=ALL_CPUS -co RESAMPLING=CUBICSPLINE `
        "$resampled" "$outCog" | Write-Verbose

    # 16-bit PNG scaled
    & gdal_translate -of PNG -ot UInt16 -scale $minVal $maxVal 0 65535 "$resampled" "$outPng" | Write-Verbose

    # Update library JSON
    $entry = [ordered]@{
        id = $id
        region = $Region
        source = (Split-Path -Leaf $InputRaster)
        bbox = [ordered]@{ ul = @($ULat,$ULon); lr = @($LLat,$LLon) }
        cog_path = (Resolve-Path -LiteralPath $outCog).Path
        png16_path = (Resolve-Path -LiteralPath $outPng).Path
        elevation_min_m = $minVal
        elevation_max_m = $maxVal
        average_fold_dir = @(0.0,1.0,0.0)
        terrain_type = if ($Region -like "Himalay*") { "HimalayanMountains" } elseif ($Region -like "Ande*") { "AndeanMountains" } else { "Unknown" }
        license = [ordered]@{
            name = $LicenseName
            attribution = $LicenseAttribution
            source_url = $SourceUrl
            retrieved = (Get-Date).ToString("yyyy-MM-dd")
        }
    }

    if (Test-Path -LiteralPath $libPath) {
        $json = Get-Content -LiteralPath $libPath -Raw | ConvertFrom-Json
        if (-not $json) { $json = @{ version = 1; exemplars = @() } }
        $json.exemplars = @($json.exemplars | Where-Object { $_.id -ne $id }) + ($entry)
        $json | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $libPath -Encoding UTF8
    } else {
        $lib = @{ version = 1; exemplars = @($entry) }
        $lib | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $libPath -Encoding UTF8
    }

    Write-Host "Exemplar prepared: $id"
    Write-Host "COG:  $outCog"
    Write-Host "PNG:  $outPng"
    Write-Host "JSON: $libPath"
}
finally {
    if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue }
}


