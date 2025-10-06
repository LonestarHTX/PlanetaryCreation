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
    param([string]$code, [string]$label)
    $bbox = Parse-TileCode $code
    $url  = Get-OT-Url $bbox
    $outfile = Join-Path $OutDir ("{0}_{1}_{2}_{3}_{4}.tif" -f $code,$bbox.south,$bbox.north,$bbox.west,$bbox.east)

    Write-Host "[DL] $label -> $code  bbox=($($bbox.south),$($bbox.west))-($($bbox.north),$($bbox.east))"

    try {
        Invoke-WebRequest -Uri $url -OutFile $outfile -TimeoutSec 900 -UseBasicParsing

        # sanity check: reject HTML masquerading as tif
        if ((Get-Item $outfile).Length -lt 50000) {
            $head = Get-Content $outfile -TotalCount 1 -ErrorAction SilentlyContinue
            if ($head -match '<!DOCTYPE|<html') { throw "Server returned an HTML error page." }
        }
    } catch {
        if (Test-Path $outfile) { Remove-Item $outfile -Force }
        Write-Host "[ERROR] $($code): $($_.Exception.Message)"
        return
    }
}

# --- your tiles ---
$tiles = @(
    @{ Code="H01"; Name="Himalayan - Everest-Lhotse massif"; CodeStr="N27E086" }
    @{ Code="H02"; Name="Himalayan - Annapurna sanctuary";   CodeStr="N28E083" }
    @{ Code="H03"; Name="Himalayan - Kangchenjunga saddle";  CodeStr="N27E088" }
    @{ Code="H04"; Name="Himalayan - Baltoro glacier / K2";  CodeStr="N35E076" }
    @{ Code="H05"; Name="Himalayan - Nanga Parbat massif";   CodeStr="N35E074" }
    @{ Code="H06"; Name="Himalayan - Bhutan high ridge";     CodeStr="N27E090" }
    @{ Code="H07"; Name="Himalayan - Nyainqentanglha range"; CodeStr="N30E091" }

    @{ Code="A01"; Name="Andean - Cordillera Blanca";        CodeStr="S09W078" }
    @{ Code="A02"; Name="Andean - Huayhuash knot";           CodeStr="S10W077" }
    @{ Code="A03"; Name="Andean - Vilcabamba (Cusco)";       CodeStr="S13W074" }
    @{ Code="A04"; Name="Andean - Ausangate-Sibinacocha";    CodeStr="S14W071" }
    @{ Code="A05"; Name="Andean - Lake Titicaca escarpment"; CodeStr="S16W069" }
    @{ Code="A06"; Name="Andean - Nevado Sajama";            CodeStr="S19W069" }
    @{ Code="A07"; Name="Andean - Potosi cordillera";        CodeStr="S20W066" }
    @{ Code="A08"; Name="Andean - Atacama Domeyko";          CodeStr="S24W069" }
    @{ Code="A09"; Name="Andean - Aconcagua";                CodeStr="S33W070" }
    @{ Code="A10"; Name="Andean - Central Chilean Andes";    CodeStr="S35W071" }
    @{ Code="A11"; Name="Andean - Northern Patagonia icefield"; CodeStr="S47W074" }

    @{ Code="O01"; Name="Ancient - Great Smoky Mountains";   CodeStr="N35W084" }
    @{ Code="O02"; Name="Ancient - Blue Ridge (Virginia)";   CodeStr="N37W080" }
    @{ Code="O03"; Name="Ancient - Scottish Cairngorms";     CodeStr="N56W004" }
    @{ Code="O04"; Name="Ancient - Scandinavian Jotunheimen";CodeStr="N60E008" }
    @{ Code="O05"; Name="Ancient - Drakensberg escarpment";  CodeStr="S29E029" }
    @{ Code="O06"; Name="Ancient - Middle Urals";            CodeStr="N60E058" }
)

foreach ($t in $tiles) {
    Download-OT-Tile -code $t.CodeStr -label ("{0} ({1})" -f $t.Code, $t.Name)
}

Write-Host "All tiles processed. Files saved to $OutDir"
