"""Convert cropped exemplar GeoTIFFs to COG and PNG16 formats for UE Stage B.

Usage:
    python Scripts/convert_to_cog_png16.py
"""

import json
from pathlib import Path
import numpy as np
import rasterio
from rasterio.io import MemoryFile


def convert_to_cog(src_path: Path, dst_path: Path) -> None:
    """Convert GeoTIFF to Cloud-Optimized GeoTIFF with LZW compression."""
    dst_path.parent.mkdir(parents=True, exist_ok=True)

    with rasterio.open(src_path) as src:
        profile = src.profile.copy()
        profile.update({
            'driver': 'GTiff',
            'compress': 'lzw',
            'tiled': True,
            'blockxsize': 512,
            'blockysize': 512,
        })

        with rasterio.open(dst_path, 'w', **profile) as dst:
            dst.write(src.read())

    print(f"[OK] COG: {dst_path.name}")


def convert_to_png16(src_path: Path, dst_path: Path, min_elev: float, max_elev: float) -> None:
    """Convert GeoTIFF to 16-bit PNG scaled from elevation range to 0-65535.
    
    Handles nodata values by masking them out before scaling. Nodata pixels
    are set to 0 (minimum elevation) in the output PNG16.
    """
    dst_path.parent.mkdir(parents=True, exist_ok=True)

    with rasterio.open(src_path) as src:
        data = src.read(1)
        nodata_value = src.nodata

        # Create mask for valid data
        if nodata_value is not None:
            valid_mask = data != nodata_value
            num_nodata = (~valid_mask).sum()
            total_pixels = data.size
            nodata_pct = (num_nodata / total_pixels) * 100.0
            
            if num_nodata > 0:
                print(f"[WARNING] {num_nodata}/{total_pixels} pixels ({nodata_pct:.1f}%) are nodata (value={nodata_value})")
                
                if nodata_pct > 10.0:
                    print(f"   [ERROR] Nodata percentage exceeds 10% threshold.")
                    print(f"   [ERROR] This exemplar should NOT be used - corrupt source data!")
                    print(f"   [ERROR] Download fresh SRTM tiles or use alternative exemplar.")
                
                # Replace nodata with min_elev for scaling
                # This is a fallback - ideally nodata should be interpolated or exemplar rejected
                data = np.where(valid_mask, data, min_elev)
        else:
            nodata_pct = 0.0

        # Scale from [min_elev, max_elev] to [0, 65535]
        if max_elev > min_elev:
            scaled = ((data - min_elev) / (max_elev - min_elev) * 65535.0)
            scaled = np.clip(scaled, 0, 65535).astype(np.uint16)
        else:
            scaled = np.zeros_like(data, dtype=np.uint16)

        # Write as PNG (no georeferencing needed for UE textures)
        profile = {
            'driver': 'PNG',
            'dtype': 'uint16',
            'width': data.shape[1],
            'height': data.shape[0],
            'count': 1,
        }

        with rasterio.open(dst_path, 'w', **profile) as dst:
            dst.write(scaled, 1)

    if nodata_pct > 0:
        print(f"[OK] PNG16: {dst_path.name} (scaled {min_elev:.1f}m - {max_elev:.1f}m, {nodata_pct:.1f}% nodata)")
    else:
        print(f"[OK] PNG16: {dst_path.name} (scaled {min_elev:.1f}m - {max_elev:.1f}m)")


def main():
    # Paths
    project_root = Path(__file__).parent.parent
    manifest_path = project_root / "StageB_SRTM90" / "metadata" / "stageb_manifest.json"
    cropped_dir = project_root / "StageB_SRTM90" / "cropped"
    cog_dir = project_root / "Content" / "PlanetaryCreation" / "Exemplars" / "COG"
    png16_dir = project_root / "Content" / "PlanetaryCreation" / "Exemplars" / "PNG16"

    # Load manifest
    if not manifest_path.exists():
        print(f"ERROR: Manifest not found at {manifest_path}")
        print("Run stageb_patch_cutter.py first!")
        return

    with open(manifest_path) as f:
        manifest = json.load(f)

    print(f"Converting {len(manifest['exemplars'])} exemplars...\n")

    for exemplar in manifest['exemplars']:
        ex_id = exemplar['id']
        src_path = cropped_dir / f"{ex_id}.tif"

        if not src_path.exists():
            print(f"⚠ Skipping {ex_id}: source file not found")
            continue

        # Convert to COG
        cog_path = cog_dir / f"{ex_id}.tif"
        convert_to_cog(src_path, cog_path)

        # Convert to PNG16 using elevation range from manifest
        png_path = png16_dir / f"{ex_id}.png"
        stats = exemplar['statistics']
        convert_to_png16(src_path, png_path, stats['min'], stats['max'])

        print()  # Blank line between exemplars

    print(f"\n[COMPLETE] Conversion complete!")
    print(f"   COG files: {cog_dir}")
    print(f"   PNG16 files: {png16_dir}")


if __name__ == "__main__":
    main()
