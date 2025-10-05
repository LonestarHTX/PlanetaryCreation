"""Update ExemplarLibrary.json with Stage B SRTM exemplar metadata.

Usage:
    python Scripts/update_exemplar_library.py
"""

import json
from pathlib import Path
from typing import Dict, Any


def create_exemplar_entry(manifest_entry: Dict[str, Any], project_root: Path) -> Dict[str, Any]:
    """Convert manifest entry to exemplar library format."""
    ex_id = manifest_entry['id']
    stats = manifest_entry['statistics']
    bounds = manifest_entry['bounds']

    return {
        "id": ex_id,
        "name": f"{manifest_entry['region']} - {manifest_entry['feature']}",
        "region": manifest_entry['region'],
        "feature": manifest_entry['feature'],
        "cog_path": f"PlanetaryCreation/Exemplars/COG/{ex_id}.tif",
        "png16_path": f"PlanetaryCreation/Exemplars/PNG16/{ex_id}.png",
        "elevation_min_m": stats['min'],
        "elevation_max_m": stats['max'],
        "elevation_mean_m": stats['mean'],
        "elevation_stddev_m": stats['stddev'],
        "bounds": {
            "west": bounds['left'],
            "south": bounds['bottom'],
            "east": bounds['right'],
            "north": bounds['top']
        },
        "resolution": {
            "width_px": 512,
            "height_px": 512,
            "pixel_size_deg": manifest_entry['pixel_size']['x_deg']
        },
        "source_tiles": manifest_entry['tiles'],
        "data_source": "SRTM GL1 (NASA/USGS)",
        "attribution": "NASA Shuttle Radar Topography Mission (SRTM) Global 1 arc-second V003. DOI: 10.5067/MEaSUREs/SRTM/SRTMGL1.003"
    }


def main():
    project_root = Path(__file__).parent.parent
    manifest_path = project_root / "StageB_SRTM90" / "metadata" / "stageb_manifest.json"
    library_path = project_root / "Content" / "PlanetaryCreation" / "Exemplars" / "ExemplarLibrary.json"

    # Load Stage B manifest
    if not manifest_path.exists():
        print(f"ERROR: Manifest not found at {manifest_path}")
        return

    with open(manifest_path) as f:
        manifest = json.load(f)

    # Create or load existing library
    if library_path.exists():
        with open(library_path) as f:
            library = json.load(f)
        print(f"Loaded existing library with {len(library.get('exemplars', []))} entries")
    else:
        library = {
            "version": "1.0",
            "description": "Stage B exemplar terrain library for continental amplification",
            "exemplars": []
        }
        print("Creating new exemplar library")

    # Convert manifest entries to library format
    new_exemplars = []
    for entry in manifest['exemplars']:
        new_exemplars.append(create_exemplar_entry(entry, project_root))

    # Merge with existing (avoid duplicates by ID)
    existing_ids = {ex['id'] for ex in library['exemplars']}
    for ex in new_exemplars:
        if ex['id'] not in existing_ids:
            library['exemplars'].append(ex)
            print(f"  + Added {ex['id']}: {ex['name']}")
        else:
            # Update existing entry
            for i, existing in enumerate(library['exemplars']):
                if existing['id'] == ex['id']:
                    library['exemplars'][i] = ex
                    print(f"  * Updated {ex['id']}: {ex['name']}")
                    break

    # Sort by region then ID
    library['exemplars'].sort(key=lambda x: (x['region'], x['id']))

    # Write updated library
    library_path.parent.mkdir(parents=True, exist_ok=True)
    with open(library_path, 'w') as f:
        json.dump(library, f, indent=2)

    print(f"\n✅ Exemplar library updated: {library_path}")
    print(f"   Total exemplars: {len(library['exemplars'])}")
    print(f"   - Himalayan: {sum(1 for ex in library['exemplars'] if ex['region'] == 'Himalayan')}")
    print(f"   - Andean: {sum(1 for ex in library['exemplars'] if ex['region'] == 'Andean')}")
    print(f"   - Ancient: {sum(1 for ex in library['exemplars'] if ex['region'] == 'Ancient')}")


if __name__ == "__main__":
    main()
