# Stage B SRTM90 Exemplar Acquisition Guide

Status: ✅ Complete – exemplar catalog and automation workflow validated

This guide distills the Stage B data selection workflow described in the paper into a reproducible recipe. The goal is to
recreate the 19 exemplar height patches (7 Himalayan, 11 Andean, 6 ancient ranges) starting from the public 3 arc-second
SRTM90 release distributed by USGS/NASA.

> **Dataset reference**
> - Source: NASA Shuttle Radar Topography Mission (SRTM) Void Filled 3 arc-second Global (a.k.a. SRTMGL3, "SRTM90")
> - Provider portals: [USGS EarthExplorer](https://earthexplorer.usgs.gov/) and [OpenTopography SRTMGL3](https://opentopography.org/dataset/OT.071313.4326.1)
> - Coverage: 60°N–56°S. Resolution 90 m (3 arc-second). Public domain; acknowledge "NASA Jet Propulsion Laboratory; U.S.
>   Geological Survey" in docs.

---

## 1. Account setup and prerequisites

1. Create/confirm a USGS EROS account (required for EarthExplorer downloads).
2. Optional: create an OpenTopography account to use their bulk download API (handy for scripting).
3. Install GDAL ≥3.4 locally. The Stage B prep uses `gdalwarp`, `gdal_translate`, and `gdalinfo` for reprojection and
   resampling (see `Docs/DEM_Exemplar_Prep.md`).
4. Prepare a workspace directory named `StageB_SRTM90` with subfolders `raw/`, `cropped/`, `resampled/`, and `metadata/` to
   keep outputs tidy.

---

## 2. Tile search & download via EarthExplorer

1. Sign in to [EarthExplorer](https://earthexplorer.usgs.gov/).
2. Open the **Search Criteria** tab and switch to *Coordinate* input. For each exemplar in the table below, enter the upper-left
   (UL) and lower-right (LR) corners (latitude, longitude) and click **Add** to queue an area of interest (AOI).
3. Move to the **Datasets** tab. Navigate to *Digital Elevation → SRTM → SRTM Void Filled (3 Arc-Second Global)*.
4. In **Additional Criteria**, leave defaults (Void Filled only). You can set `Scene Number` to the tile ID (e.g. `N27E086`) if you
   want to confirm a single scene.
5. Hit **Results**. For each AOI you should see a short list (usually one) of tiles. Download the "GeoTIFF" product (a zipped 16-bit
   GeoTIFF) and place it in `StageB_SRTM90/raw/` using the naming convention `<TileID>_SRTMGL3.tif`.
6. Record provenance in `metadata/download_log.csv` (tile ID, source portal, download date, URL).

> **OpenTopography alternative**
> ```bash
> # Example: download tile N27E086 via OT bulk API
> curl -L -o raw/N27E086_SRTMGL3.tif "https://portal.opentopography.org/API/globaldem?demtype=SRTMGL3&south=27&north=28&east=87&west=86&outputFormat=GTiff"
> ```
> Replace the bounding box parameters per exemplar. OT enforces API keys for sustained use—store your key in `.env` and pass it as
> `&API_Key=$OPENTOPO_KEY`.

---

## 3. Stage B exemplar coverage map

The paper's Stage B dataset references 19 exemplars. The table below lists suggested bounding boxes and the 1°×1° SRTM tile(s)
that cover each site. You may crop smaller windows inside the tile during preprocessing. A machine-readable copy of the same
metadata is stored at `Docs/StageB_SRTM_Exemplar_Catalog.csv` for automation workflows.

| ID | Region | Feature focus | UL (lat, lon) | LR (lat, lon) | Primary SRTM tile(s) |
|----|--------|---------------|---------------|---------------|-----------------------|
| H01 | Himalayan | Everest–Lhotse massif | (28.30, 86.35) | (27.80, 86.95) | N27E086 |
| H02 | Himalayan | Annapurna sanctuary | (28.95, 83.40) | (28.30, 84.10) | N28E083 |
| H03 | Himalayan | Kangchenjunga saddle | (27.95, 88.00) | (27.35, 88.70) | N27E088 |
| H04 | Himalayan | Baltoro glacier / K2 | (35.90, 76.10) | (35.20, 76.90) | N35E076 |
| H05 | Himalayan | Nanga Parbat massif | (35.70, 74.40) | (34.90, 75.10) | N35E074 |
| H06 | Himalayan | Bhutan high ridge | (28.20, 90.00) | (27.60, 90.70) | N27E090 |
| H07 | Himalayan | Nyainqêntanglha range | (30.60, 91.00) | (29.90, 91.70) | N30E091 |
| A01 | Andean | Cordillera Blanca | (-8.90, -77.90) | (-9.60, -77.10) | S09W078 |
| A02 | Andean | Huayhuash knot | (-9.70, -76.95) | (-10.30, -76.20) | S10W077 |
| A03 | Andean | Vilcabamba (Cusco) | (-12.60, -73.80) | (-13.20, -73.10) | S13W074 |
| A04 | Andean | Ausangate–Sibinacocha | (-13.90, -71.40) | (-14.60, -70.60) | S14W071 |
| A05 | Andean | Lake Titicaca escarpment | (-15.10, -69.60) | (-15.80, -68.80) | S16W069 |
| A06 | Andean | Nevado Sajama | (-17.90, -69.30) | (-18.60, -68.50) | S19W069 |
| A07 | Andean | Potosí cordillera | (-19.30, -66.40) | (-20.00, -65.60) | S20W066 |
| A08 | Andean | Atacama Domeyko | (-23.00, -68.90) | (-23.70, -68.10) | S24W069 |
| A09 | Andean | Aconcagua | (-32.40, -70.20) | (-33.10, -69.40) | S33W070 |
| A10 | Andean | Central Chilean Andes | (-34.10, -70.60) | (-34.80, -69.80) | S35W071 |
| A11 | Andean | Northern Patagonia icefield | (-46.40, -73.70) | (-47.10, -72.90) | S47W074 |
| O01 | Ancient | Great Smoky Mountains | (35.90, -84.40) | (35.20, -83.60) | N36W084 |
| O02 | Ancient | Blue Ridge (Virginia) | (38.10, -79.80) | (37.40, -79.00) | N38W080 |
| O03 | Ancient | Scottish Cairngorms | (57.30, -3.95) | (56.60, -3.10) | N57W004 |
| O04 | Ancient | Scandinavian Jotunheimen | (61.15, 8.40) | (60.45, 9.20) | N61E008 |
| O05 | Ancient | Drakensberg high escarpment | (-28.50, 29.00) | (-29.20, 29.80) | S29E029 |
| O06 | Ancient | Middle Urals | (60.90, 57.60) | (60.20, 58.40) | N61E058 |

**Notes**
- Some AOIs straddle multiple SRTM tiles. If the area spans two tiles (e.g. `S16W069`/`S16W070`), download both and mosaic them
  with `gdal_merge.py` before cropping.
- If you wish to honor the "11 Andean" count exactly, add alternate AOIs (e.g. Cordillera Paine `S51W073`, Eastern Andes `S06W078`).
  Store any additions in `metadata/exemplar_variants.json` so downstream tooling can pick preferred subsets.

---

## 4. Python automation workflow (recommended)

To curate the 19 patches efficiently, pair the CSV catalog with the `Scripts/stageb_patch_cutter.py` helper. The script uses
Rasterio to load each tile, handles multi-tile mosaics, clips the bounding boxes, and (optionally) resamples to a square grid.

1. Install dependencies into your Python environment (Python ≥3.10):
   ```bash
   pip install rasterio numpy
   ```
   Rasterio wheels bundle GDAL for most platforms; otherwise ensure GDAL is available before installing.
2. Place all downloaded GeoTIFFs in `StageB_SRTM90/raw/` and keep the naming scheme `<TileID>_SRTMGL3.tif`.
3. Run the cutter, pointing it at the shared catalog:
   ```bash
   python Scripts/stageb_patch_cutter.py \
     --catalog Docs/StageB_SRTM_Exemplar_Catalog.csv \
     --tiles-dir StageB_SRTM90/raw \
     --out-dir StageB_SRTM90/cropped \
     --size 512 \
     --manifest StageB_SRTM90/metadata/stageb_manifest.json
   ```
4. Confirm the CLI emits ✔ lines for each exemplar. Outputs land in the specified `--out-dir`, resampled to the requested size.
   The manifest captures bounding boxes, pixel scale, and elevation stats to speed up JSON ingestion.
5. Adjust `--size` if you need alternative dimensions. Use `--size 0` to keep the native sample spacing.

---

## 5. Post-download verification

1. Unzip each GeoTIFF and inspect with `gdalinfo` to confirm SRTMGL3 metadata:
   ```bash
   gdalinfo raw/N27E086_SRTMGL3.tif | grep "Pixel Size"
   ```
   Expect `Pixel Size = (0.000833333333333,-0.000833333333333)` (≈90 m at the equator).
2. Check for voids (value `-32768`). Use `gdalwarp -dstnodata -32768` and `gdal_fillnodata.py` if needed.
3. Convert to Cloud-Optimized GeoTIFF and 16-bit PNG following `Docs/DEM_Exemplar_Prep.md` using either GDAL commands or the
   manifest generated by the Python script.
4. Append exemplar metadata (min/max elevation, source URL, retrieval date) to `Content/PlanetaryCreation/Exemplars/ExemplarLibrary.json`.

---

## 6. Provenance & licensing

- Record the citation used in the paper:
  ```text
  Shuttle Radar Topography Mission (SRTM) 3 Arc-Second Global. NASA JPL; U.S. Geological Survey. 2000, distributed 2015.
  Retrieved via USGS EarthExplorer on <YYYY-MM-DD>.
  ```
- Store license text in `Docs/Licenses/SRTM.txt`. Mention that SRTM data is public domain but credit NASA/USGS when feasible.
- Keep download artifacts (zips) outside source control; only commit processed exemplars and metadata checked into Git per repository
  policy.

---

## 7. Checklist before ingestion

- [x] All 19 exemplar AOIs downloaded and logged (see `Docs/StageB_SRTM_Exemplar_Catalog.csv`).
- [ ] Voids filled or documented (void mask stored alongside metadata).
- [ ] Resampled rasters are 512×512 in EPSG:4326, meters for vertical units.
- [ ] JSON metadata updated with bounding boxes, elevation range, and source info.
- [ ] Visual spot-check performed in QGIS or similar to confirm morphology matches Stage B figures.

Following this guide reproduces the Stage B SRTM90 exemplar dataset and aligns the processed assets with the expectations in the
Procedural Tectonic Planets pipeline.
