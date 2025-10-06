#!/usr/bin/env python3
import json, math, sys, numpy as np
import rasterio
from scipy.ndimage import gaussian_filter

def dominant_orientation_deg(z: np.ndarray, sigma_px=3.0):
    # Simple central differences (works fine for 512² tiles)
    gy, gx = np.gradient(z.astype(np.float32))
    # Structure tensor elements
    Jxx = gaussian_filter(gx*gx, sigma_px)
    Jyy = gaussian_filter(gy*gy, sigma_px)
    Jxy = gaussian_filter(gx*gy, sigma_px)
    # Principal direction of largest eigenvalue (in image coords)
    # angle = 0.5 * atan2(2Jxy, Jxx - Jyy)  (radians)
    ang = 0.5 * np.arctan2(2.0*Jxy, (Jxx - Jyy + 1e-12))
    # Convert to degrees in [0,180)
    ang_deg = (np.degrees(ang) + 180.0) % 180.0
    # Weight by gradient magnitude so flat areas don’t dominate
    w = np.hypot(gx, gy)
    if w.sum() < 1e-6:
        return 0.0, 0.0  # trivially flat
    # Circular mean on doubled angles to resolve 180° ambiguity
    theta = np.deg2rad(ang_deg*2.0)
    c = (w*np.cos(theta)).sum() / (w.sum()+1e-12)
    s = (w*np.sin(theta)).sum() / (w.sum()+1e-12)
    mean2 = math.atan2(s, c)
    mean = (math.degrees(mean2)/2.0) % 180.0
    # Strength = resultant length in [0,1]
    strength = float(np.hypot(c, s))
    return float(mean), strength

def main(in_tif, out_json=None):
    with rasterio.open(in_tif) as ds:
        z = ds.read(1, masked=True).filled(np.nan)
    z = np.where(np.isfinite(z), z, np.nan)
    # Focus on valid area
    valid = np.isfinite(z)
    if valid.mean() < 0.05:
        print(json.dumps({"orientation_deg": 0.0, "strength": 0.0, "note":"too_much_nodata"}))
        return
    zv = z.copy(); zv[~valid] = np.nanmedian(z[valid])
    odeg, stren = dominant_orientation_deg(zv, sigma_px=3.0)
    out = {"orientation_deg": round(odeg,2), "strength": round(stren,4)}
    if out_json:
        with open(out_json, "w") as f: json.dump(out, f, indent=2)
    print(json.dumps(out, indent=2))

if __name__ == "__main__":
    # Usage: python exemplar_orientation.py A04.tif A04.orientation.json
    in_tif = sys.argv[1]
    out_json = sys.argv[2] if len(sys.argv) > 2 else None
    main(in_tif, out_json)
