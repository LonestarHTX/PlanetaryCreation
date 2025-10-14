#!/usr/bin/env python3
"""Regenerate PNG16 for specific exemplars"""
import sys
import json
from pathlib import Path
import numpy as np
import rasterio

# Import convert function from main script
sys.path.insert(0, str(Path(__file__).parent))
from convert_to_cog_png16 import convert_to_png16

def main():
    project_root = Path(__file__).parent.parent
    manifest_path = project_root / "StageB_SRTM90" / "metadata" / "stageb_manifest.json"
    cropped_dir = project_root / "StageB_SRTM90" / "cropped"
    png16_dir = project_root / "Content" / "PlanetaryCreation" / "Exemplars" / "PNG16"

    if len(sys.argv) < 2:
        print("Usage: python regenerate_exemplar_png16.py <EXEMPLAR_ID> [<EXEMPLAR_ID2> ...]")
        sys.exit(1)

    exemplar_ids = sys.argv[1:]

    # Load manifest
    with open(manifest_path) as f:
        manifest = json.load(f)

    for ex_id in exemplar_ids:
        # Find exemplar in manifest
        exemplar = next((e for e in manifest['exemplars'] if e['id'] == ex_id), None)
        if not exemplar:
            print(f"⚠ Exemplar {ex_id} not found in manifest")
            continue

        src_path = cropped_dir / f"{ex_id}.tif"
        if not src_path.exists():
            print(f"⚠ Source file not found: {src_path}")
            continue

        png_path = png16_dir / f"{ex_id}.png"
        stats = exemplar['statistics']
        
        print(f"\n=== Regenerating {ex_id} ===")
        convert_to_png16(src_path, png_path, stats['min'], stats['max'])
        print()

    print("[COMPLETE] Regeneration complete!")

if __name__ == "__main__":
    main()

