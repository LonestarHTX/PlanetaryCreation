#!/usr/bin/env python3
"""
Validate PNG16 exemplar heightfield data integrity.

Verifies:
- PNG decodes correctly with expected dimensions
- Elevation range matches metadata
- Terrain variation exists (not flat/corrupt)
- Cross-sections show elevation distribution
"""

import argparse
import json
import sys
from pathlib import Path
import numpy as np
from PIL import Image


def load_exemplar_metadata(library_path: Path, exemplar_id: str) -> dict:
    """Load metadata for a specific exemplar from ExemplarLibrary.json"""
    with open(library_path, 'r') as f:
        library = json.load(f)
    
    for exemplar in library.get('exemplars', []):
        if exemplar.get('id') == exemplar_id:
            return exemplar
    
    raise ValueError(f"Exemplar ID '{exemplar_id}' not found in library")


def load_png16(png_path: Path) -> np.ndarray:
    """Load 16-bit grayscale PNG into numpy array"""
    img = Image.open(png_path)
    
    if img.mode not in ['I', 'I;16', 'I;16B', 'I;16L']:
        raise ValueError(f"Expected 16-bit image, got mode: {img.mode}")
    
    # Convert to numpy array (uint16)
    data = np.array(img, dtype=np.uint16)
    return data


def remap_to_elevation(raw_data: np.ndarray, elev_min: float, elev_max: float) -> np.ndarray:
    """Remap [0, 65535] to [elev_min, elev_max] in meters"""
    normalized = raw_data.astype(np.float64) / 65535.0
    elevation = elev_min + normalized * (elev_max - elev_min)
    return elevation


def print_statistics(elevation: np.ndarray, metadata: dict):
    """Print elevation statistics and compare to metadata"""
    print("\n=== Elevation Statistics ===")
    print(f"Shape: {elevation.shape[1]}×{elevation.shape[0]} pixels")
    print(f"Min: {elevation.min():.2f} m")
    print(f"Max: {elevation.max():.2f} m")
    print(f"Mean: {elevation.mean():.2f} m")
    print(f"StdDev: {elevation.std():.2f} m")
    print(f"Median: {np.median(elevation):.2f} m")
    
    print("\n=== Metadata Comparison ===")
    meta_min = metadata['elevation_min_m']
    meta_max = metadata['elevation_max_m']
    meta_mean = metadata['elevation_mean_m']
    meta_std = metadata['elevation_stddev_m']
    
    print(f"Expected Min: {meta_min:.2f} m (diff: {elevation.min() - meta_min:.2f} m)")
    print(f"Expected Max: {meta_max:.2f} m (diff: {elevation.max() - meta_max:.2f} m)")
    print(f"Expected Mean: {meta_mean:.2f} m (diff: {elevation.mean() - meta_mean:.2f} m)")
    print(f"Expected StdDev: {meta_std:.2f} m (diff: {elevation.std() - meta_std:.2f} m)")
    
    # Validation checks
    print("\n=== Validation Checks ===")
    min_close = abs(elevation.min() - meta_min) < 1.0
    max_close = abs(elevation.max() - meta_max) < 1.0
    mean_close = abs(elevation.mean() - meta_mean) < 10.0
    std_close = abs(elevation.std() - meta_std) < 50.0
    has_variation = elevation.std() > 100.0
    
    print(f"[OK] Min matches metadata: {min_close}")
    print(f"[OK] Max matches metadata: {max_close}")
    print(f"[OK] Mean matches metadata: {mean_close}")
    print(f"[OK] StdDev matches metadata: {std_close}")
    print(f"[OK] Has terrain variation (std > 100m): {has_variation}")
    
    all_valid = min_close and max_close and mean_close and std_close and has_variation
    return all_valid


def print_cross_sections(elevation: np.ndarray, metadata: dict):
    """Print elevation cross-sections at multiple V-coordinates"""
    height, width = elevation.shape
    
    print("\n=== Cross-Sections (Horizontal Slices) ===")
    
    # Sample at V = 0.0, 0.25, 0.5, 0.75, 1.0 (top to bottom in image)
    v_samples = [0.0, 0.25, 0.5, 0.75, 1.0]
    
    for v in v_samples:
        row_idx = int(v * (height - 1))
        row_data = elevation[row_idx, :]
        
        # Sample 10 points across the row
        sample_indices = np.linspace(0, width - 1, 10, dtype=int)
        samples = row_data[sample_indices]
        
        print(f"\nV={v:.2f} (row {row_idx}):")
        print(f"  Range: {row_data.min():.1f} – {row_data.max():.1f} m")
        print(f"  Mean: {row_data.mean():.1f} m, StdDev: {row_data.std():.1f} m")
        print(f"  Samples (10 evenly spaced): {', '.join(f'{s:.0f}' for s in samples)}")


def print_histogram(elevation: np.ndarray, num_bins: int = 20):
    """Print ASCII histogram of elevation distribution"""
    print(f"\n=== Elevation Histogram ({num_bins} bins) ===")
    
    hist, bin_edges = np.histogram(elevation, bins=num_bins)
    max_count = hist.max()
    bar_width = 50  # characters
    
    for i in range(num_bins):
        count = hist[i]
        bar_len = int((count / max_count) * bar_width) if max_count > 0 else 0
        bar = '#' * bar_len
        bin_start = bin_edges[i]
        bin_end = bin_edges[i + 1]
        print(f"{bin_start:7.1f} - {bin_end:7.1f} m | {bar} {count}")


def main():
    parser = argparse.ArgumentParser(description='Validate PNG16 exemplar heightfield data')
    parser.add_argument('png_path', type=Path, help='Path to PNG16 file')
    parser.add_argument('--metadata-id', required=True, help='Exemplar ID (e.g., A01, O01)')
    parser.add_argument('--library', type=Path, required=True, help='Path to ExemplarLibrary.json')
    args = parser.parse_args()
    
    if not args.png_path.exists():
        print(f"Error: PNG file not found: {args.png_path}", file=sys.stderr)
        return 1
    
    if not args.library.exists():
        print(f"Error: Library file not found: {args.library}", file=sys.stderr)
        return 1
    
    print(f"=== Validating {args.metadata_id} ===")
    print(f"PNG: {args.png_path}")
    print(f"Library: {args.library}")
    
    # Load metadata
    try:
        metadata = load_exemplar_metadata(args.library, args.metadata_id)
    except Exception as e:
        print(f"Error loading metadata: {e}", file=sys.stderr)
        return 1
    
    print(f"\nExemplar: {metadata['name']}")
    print(f"Region: {metadata['region']}")
    print(f"Feature: {metadata['feature']}")
    
    # Load PNG16
    try:
        raw_data = load_png16(args.png_path)
    except Exception as e:
        print(f"Error loading PNG16: {e}", file=sys.stderr)
        return 1
    
    # Verify dimensions
    expected_width = metadata['resolution']['width_px']
    expected_height = metadata['resolution']['height_px']
    actual_height, actual_width = raw_data.shape
    
    if actual_width != expected_width or actual_height != expected_height:
        print(f"Error: Dimension mismatch!", file=sys.stderr)
        print(f"  Expected: {expected_width}×{expected_height}", file=sys.stderr)
        print(f"  Actual: {actual_width}×{actual_height}", file=sys.stderr)
        return 1
    
    # Remap to elevation
    elev_min = metadata['elevation_min_m']
    elev_max = metadata['elevation_max_m']
    elevation = remap_to_elevation(raw_data, elev_min, elev_max)
    
    # Print statistics
    all_valid = print_statistics(elevation, metadata)
    
    # Print cross-sections
    print_cross_sections(elevation, metadata)
    
    # Print histogram
    print_histogram(elevation)
    
    # Final verdict
    print("\n" + "=" * 60)
    if all_valid:
        print("[PASS] VALIDATION PASSED: PNG16 data is valid")
        return 0
    else:
        print("[FAIL] VALIDATION FAILED: Check discrepancies above")
        return 1


if __name__ == '__main__':
    sys.exit(main())

