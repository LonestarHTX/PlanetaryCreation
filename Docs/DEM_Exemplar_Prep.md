### DEM & Noise Prep – Exemplars (Himalayan/Andean)

Status: ✅ Ready for Stage B usage (SRTM exemplar pipeline published)

Objectives
- Gather legally-usable DEM tiles for Himalayan and Andean exemplars (see [SRTM Stage B download recipe](SRTM_StageB_Exemplar_Guide.md))
- Reproject to EPSG:4326, crop to curated bounding boxes, resample to 512×512
- Normalize elevation to meters, save Cloud-Optimized GeoTIFF (COG) and 16-bit PNG
- Emit `ExemplarLibrary.json` for safe lookup by terrain amplification code

Recommended Sources (prefer modern, stable hosting)
- Copernicus DEM GLO-30 (30 m): reliable global coverage, Andes included; requires attribution "Contains modified Copernicus Sentinel data [year]". Download via Copernicus Data Space or mirrored Open Data COGs.
- NASA SRTM 1 Arc-Second Global (≈30 m): 60°N–56°S, includes Andes and most Himalayas. Public use (NASA/USGS); attribution recommended.
- Fallback: NASADEM Merged, ASTER GDEM (check voids/artifacts).

Licensing Notes (verify at download time)
- Copernicus DEM: Free to use; attribution required per Copernicus Data and Information Policy. Keep a `Docs/Licenses/Copernicus.txt` with the exact statement and product IDs.
- SRTM/NASADEM: Public domain; cite NASA/USGS and product page. Keep `Docs/Licenses/SRTM.txt` with links and date.

Curated Bounding Boxes (lat, lon in EPSG:4326)
- Himalayan sample A (Everest range):
  - UL: (28.35, 86.35), LR: (27.85, 86.95)  // approx 0.5° × 0.6°
- Andean sample A (Central Andes, Peru):
  - UL: (-13.00, -73.50), LR: (-13.60, -72.80)

Output Layout
- `Content/PlanetaryCreation/Exemplars/COG/*.tif` (COG, Float32 meters)
- `Content/PlanetaryCreation/Exemplars/PNG16/*.png` (uint16 normalized)
- `Content/PlanetaryCreation/Exemplars/ExemplarLibrary.json`

GDAL Workflow (conceptual)
*(Shortcut: run `python Scripts/stageb_patch_cutter.py --catalog Docs/StageB_SRTM_Exemplar_Catalog.csv ...` to crop the Stage B
SRTM exemplars automatically; see the Stage B guide for exact parameters.)*
1) Reproject to EPSG:4326 if needed:
   gdalwarp -t_srs EPSG:4326 -r cubicspline -multi -overwrite input.tif reproj.tif
2) Crop to bbox (projwin uses ULX ULY LRX LRY):
   gdal_translate -projwin <ULX> <ULY> <LRX> <LRY> -r bilinear reproj.tif crop.tif
3) Resample to 512×512 and set meters unit:
   gdalwarp -t_srs EPSG:4326 -ts 512 512 -r cubicspline -multi crop.tif resampled.tif
4) Write Cloud-Optimized GeoTIFF:
   gdal_translate -of COG -co COMPRESS=LZW -co RESAMPLING=CUBICSPLINE resampled.tif out.tif
5) Export 16-bit PNG (scaled 0–65535 using min/max of tile):
   gdal_translate -of PNG -ot UInt16 -scale <min> <max> 0 65535 out.tif out.png

Normalization Guidance
- Compute per-tile min/max elevation (meters). Store min/max in JSON for runtime denorm.
- Preserve nodata; fill small voids with `gdal_fillnodata.py` before scaling.

Exemplar JSON Schema (stored at `Content/PlanetaryCreation/Exemplars/ExemplarLibrary.json`)
{
  "version": 1,
  "exemplars": [
    {
      "id": "Himalayan_Everest_A",
      "region": "Himalayan",
      "source": "CopernicusGLO30 or SRTM1Arc",
      "bbox": {"ul": [28.35, 86.35], "lr": [27.85, 86.95]},
      "cog_path": "Content/PlanetaryCreation/Exemplars/COG/Himalayan_Everest_A.tif",
      "png16_path": "Content/PlanetaryCreation/Exemplars/PNG16/Himalayan_Everest_A.png",
      "elevation_min_m": <fill>,
      "elevation_max_m": <fill>,
      "average_fold_dir": [0.0, 1.0, 0.0],
      "terrain_type": "HimalayanMountains",
      "license": {
        "name": "Copernicus DEM GLO-30",
        "attribution": "Contains modified Copernicus Sentinel data",
        "source_url": "<link>",
        "retrieved": "YYYY-MM-DD"
      }
    }
  ]
}

Notes
- Keep consistent naming: `<Region>_<Site>_<Letter>`
- Store fold direction and elevation range for amplification alignment and scaling.
- Track provenance (download URL, product ID) in JSON for auditability.


