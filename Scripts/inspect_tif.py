#!/usr/bin/env python3
"""Quick TIF inspection utility"""
import sys
import rasterio
import numpy as np

tif_path = sys.argv[1]
with rasterio.open(tif_path) as src:
    data = src.read(1)
    print(f"Shape: {data.shape}")
    print(f"Min: {data.min():.2f}")
    print(f"Max: {data.max():.2f}")
    print(f"Mean: {data.mean():.2f}")
    print(f"StdDev: {data.std():.2f}")
    print(f"Dtype: {data.dtype}")
    print(f"Nodata: {src.nodata}")
    
    print("\nRow means at V=0.0, 0.25, 0.5, 0.75, 1.0:")
    for v in [0.0, 0.25, 0.5, 0.75, 1.0]:
        row_idx = int(v * (data.shape[0] - 1))
        row_mean = data[row_idx, :].mean()
        row_std = data[row_idx, :].std()
        print(f"  V={v:.2f} (row {row_idx}): mean={row_mean:.1f}m, std={row_std:.1f}m")

