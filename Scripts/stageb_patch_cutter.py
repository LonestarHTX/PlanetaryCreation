"""Stage B SRTM exemplar patch extraction utility.

This script automates the workflow described in Docs/SRTM_StageB_Exemplar_Guide.md
by clipping repeatable exemplar windows from downloaded SRTM tiles. The catalog of
patches is supplied as a CSV with bounding boxes (UL/LR latitude & longitude) and a
comma- or semicolon-separated list of tile IDs.

Example usage:
    python Scripts/stageb_patch_cutter.py \
        --catalog Docs/StageB_SRTM_Exemplar_Catalog.csv \
        --tiles-dir StageB_SRTM90/raw \
        --out-dir StageB_SRTM90/cropped \
        --size 512 \
        --manifest StageB_SRTM90/metadata/stageb_manifest.json

The tool crops the requested bounding boxes, optionally resamples to a square grid,
computes per-patch elevation statistics, and writes both the raster and manifest
metadata.
"""

from __future__ import annotations

import argparse
import csv
import json
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Iterator, List, Optional

import numpy as np
import rasterio
from rasterio.enums import Resampling
from rasterio.io import MemoryFile
from rasterio.merge import merge
from rasterio.windows import Window, from_bounds
import rasterio.warp


@dataclass
class ExemplarRecord:
    """Represents a single exemplar entry parsed from the catalog CSV."""

    id: str
    region: str
    feature: str
    ul_lat: float
    ul_lon: float
    lr_lat: float
    lr_lon: float
    tiles: List[str]

    @property
    def bounds(self) -> tuple[float, float, float, float]:
        """Returns (left, bottom, right, top) suitable for rasterio windows."""

        left = min(self.ul_lon, self.lr_lon)
        right = max(self.ul_lon, self.lr_lon)
        top = max(self.ul_lat, self.lr_lat)
        bottom = min(self.ul_lat, self.lr_lat)
        return left, bottom, right, top


def parse_catalog(path: Path) -> List[ExemplarRecord]:
    records: List[ExemplarRecord] = []
    with path.open("r", newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        required = {"id", "region", "feature", "ul_lat", "ul_lon", "lr_lat", "lr_lon", "tiles"}
        missing = required.difference(reader.fieldnames or [])
        if missing:
            raise ValueError(f"Catalog is missing required columns: {sorted(missing)}")
        for row in reader:
            # Skip comment lines
            if row["id"].strip().startswith("#"):
                continue
            tiles_field = row["tiles"].strip()
            if not tiles_field:
                raise ValueError(f"Catalog row {row['id']} does not define any tiles")
            tiles = [token.strip() for token in tiles_field.replace(";", ",").split(",") if token.strip()]
            if not tiles:
                raise ValueError(f"Catalog row {row['id']} produced no valid tiles from '{tiles_field}'")
            records.append(
                ExemplarRecord(
                    id=row["id"].strip(),
                    region=row["region"].strip(),
                    feature=row["feature"].strip(),
                    ul_lat=float(row["ul_lat"]),
                    ul_lon=float(row["ul_lon"]),
                    lr_lat=float(row["lr_lat"]),
                    lr_lon=float(row["lr_lon"]),
                    tiles=tiles,
                )
            )
    return records


def find_tile_path(tile_id: str, tiles_dir: Path) -> Path:
    """Locate a GeoTIFF for the provided tile ID within the tiles directory."""

    candidates = list(tiles_dir.glob(f"{tile_id}*.tif"))
    if not candidates:
        raise FileNotFoundError(f"No GeoTIFF found for tile '{tile_id}' in {tiles_dir}")
    if len(candidates) > 1:
        raise FileExistsError(
            f"Multiple GeoTIFFs found for tile '{tile_id}' in {tiles_dir}: {', '.join(map(str, candidates))}"
        )
    return candidates[0]


@contextmanager
def _open_tiles(tile_ids: Iterable[str], tiles_dir: Path) -> Iterator[rasterio.io.DatasetReader]:
    """Yield a dataset that covers every requested tile, merging when necessary."""

    paths = [find_tile_path(tile_id, tiles_dir) for tile_id in tile_ids]
    if not paths:
        raise ValueError("No tile paths resolved")

    if len(paths) == 1:
        with rasterio.open(paths[0]) as dataset:
            yield dataset
        return

    sources = [rasterio.open(path) for path in paths]
    try:
        mosaic, transform = merge(sources)
        meta = sources[0].meta.copy()
        meta.update(
            {
                "height": mosaic.shape[1],
                "width": mosaic.shape[2],
                "transform": transform,
            }
        )
        with MemoryFile() as memfile:
            with memfile.open(**meta) as dataset:
                dataset.write(mosaic)
            with memfile.open() as dataset:
                yield dataset
    finally:
        for src in sources:
            src.close()


def extract_window(dataset: rasterio.io.DatasetReader, record: ExemplarRecord) -> tuple[np.ndarray, Window]:
    left, bottom, right, top = record.bounds
    window = from_bounds(left, bottom, right, top, transform=dataset.transform)
    window = window.round_lengths().round_offsets()
    data = dataset.read(1, window=window)
    return data, window


def resample_patch(
    array: np.ndarray,
    window: Window,
    dataset: rasterio.io.DatasetReader,
    size: int,
    resampling: Resampling,
) -> tuple[np.ndarray, rasterio.Affine]:
    # Validate input array dimensions
    if array.size == 0 or array.shape[0] == 0 or array.shape[1] == 0:
        raise ValueError(f"Invalid array dimensions: {array.shape} (height={array.shape[0]}, width={array.shape[1]})")

    window_transform = rasterio.windows.transform(window, dataset.transform)
    bounds = rasterio.windows.bounds(window, dataset.transform)
    # bounds is a tuple: (left, bottom, right, top)
    left, bottom, right, top = bounds

    # Validate bounds
    if top <= bottom or right <= left:
        raise ValueError(f"Invalid bounds: left={left}, bottom={bottom}, right={right}, top={top}")

    pixel_width = (right - left) / size
    pixel_height = (top - bottom) / size
    dest_transform = rasterio.Affine(pixel_width, 0.0, left, 0.0, -pixel_height, top)
    dest = np.empty((1, size, size), dtype=np.float32)
    rasterio.warp.reproject(
        source=array[np.newaxis, :, :],
        destination=dest,
        src_transform=window_transform,
        src_crs=dataset.crs,
        src_nodata=dataset.nodata,
        dst_transform=dest_transform,
        dst_crs=dataset.crs,
        dst_nodata=dataset.nodata,
        resampling=resampling,
    )
    return dest.astype(np.float32), dest_transform


def write_patch(
    data: np.ndarray,
    transform: rasterio.Affine,
    dataset: rasterio.io.DatasetReader,
    out_path: Path,
    dtype: np.dtype | type = np.float32,
) -> None:
    # Ensure data is 2D (height, width) for single-band output
    if data.ndim == 3:
        data = data[0]  # Take first band

    profile = dataset.profile.copy()
    profile.update(
        {
            "driver": "GTiff",
            "height": data.shape[0],
            "width": data.shape[1],
            "transform": transform,
            "count": 1,
            "dtype": dtype,
            "compress": "deflate",
            "predictor": 2,
        }
    )
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with rasterio.open(out_path, "w", **profile) as dst:
        dst.write(data.astype(dtype), 1)


def compute_statistics(data: np.ndarray, nodata: Optional[float]) -> dict[str, float]:
    band = data.astype(np.float64)
    if nodata is not None:
        mask = band == nodata
        band = np.ma.array(band, mask=mask)
    else:
        band = np.ma.array(band)
    stats = {
        "min": float(band.min()),
        "max": float(band.max()),
        "mean": float(band.mean()),
        "stddev": float(band.std()),
    }
    return stats


def process_record(
    record: ExemplarRecord,
    tiles_dir: Path,
    out_dir: Path,
    size: Optional[int],
    resampling: Resampling,
) -> dict:
    with _open_tiles(record.tiles, tiles_dir) as dataset:  # type: ignore[attr-defined]
        try:
            array, window = extract_window(dataset, record)
            print(f"[DEBUG] {record.id}: Extracted array shape={array.shape}, window={window}")
        except Exception as e:
            raise ValueError(f"Failed to extract window for {record.id}: {e}") from e

        if size is not None:
            resampled, transform = resample_patch(array, window, dataset, size=size, resampling=resampling)
            data = resampled[0]
        else:
            transform = rasterio.windows.transform(window, dataset.transform)
            data = array
        out_path = out_dir / f"{record.id}.tif"
        write_patch(data, transform, dataset, out_path, dtype=data.dtype)
        bounds = rasterio.windows.bounds(window, dataset.transform)
        # bounds is a tuple: (left, bottom, right, top)
        bounds_left, bounds_bottom, bounds_right, bounds_top = bounds
        stats = compute_statistics(data, dataset.nodata)
        result = {
            "id": record.id,
            "region": record.region,
            "feature": record.feature,
            "tiles": record.tiles,
            "bounds": {
                "left": bounds_left,
                "bottom": bounds_bottom,
                "right": bounds_right,
                "top": bounds_top,
            },
            "pixel_size": {
                "x_deg": transform.a,
                "y_deg": transform.e,
            },
            "output_path": str(out_path.resolve()),
            "statistics": stats,
        }
        return result


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Extract Stage B exemplar patches from SRTM tiles.")
    parser.add_argument("--catalog", type=Path, required=True, help="CSV catalog describing exemplar bounding boxes")
    parser.add_argument("--tiles-dir", type=Path, required=True, help="Directory containing downloaded SRTM GeoTIFF tiles")
    parser.add_argument("--out-dir", type=Path, required=True, help="Directory where cropped patches will be written")
    parser.add_argument(
        "--size",
        type=int,
        default=512,
        help="Optional output size in pixels (width=height). Use 0 to keep native resolution.",
    )
    parser.add_argument(
        "--resampling",
        default="bilinear",
        choices=[name for name in Resampling.__members__ if name not in {"gauss", "cubic_spline"}],
        help="Resampling kernel when resizing patches (default: bilinear).",
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        help="Optional path to write JSON manifest summarizing extracted exemplars.",
    )
    return parser


def main() -> None:
    parser = build_argument_parser()
    args = parser.parse_args()

    if args.size < 0:
        parser.error("--size must be >= 0")

    size = None if args.size == 0 else args.size
    resampling = Resampling[args.resampling]

    records = parse_catalog(args.catalog)
    tiles_dir = args.tiles_dir
    out_dir = args.out_dir

    manifest: List[dict] = []
    for record in records:
        result = process_record(record, tiles_dir, out_dir, size=size, resampling=resampling)
        manifest.append(result)
        print(f"✔ Extracted {record.id} → {result['output_path']}")

    if args.manifest:
        args.manifest.parent.mkdir(parents=True, exist_ok=True)
        with args.manifest.open("w", encoding="utf-8") as handle:
            json.dump({"exemplars": manifest}, handle, indent=2)
        print(f"Manifest written to {args.manifest}")


if __name__ == "__main__":
    main()
