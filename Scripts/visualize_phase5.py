#!/usr/bin/env python3
"""
Phase 5 Oceanic Crust Validation Visualizer

Generates validation plots from OceanicVisualizationTest CSV outputs:
1. Elevation vs distance-to-ridge (ridge template validation)
2. Alpha heatmap (blending behavior)
3. Cross-boundary smoothness (interpolation validation)
4. Ridge direction field (tangent validation)

Usage:
    python visualize_phase5.py [--validation-dir PATH]
"""

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
from pathlib import Path
import argparse


def plot_elevation_profile(df, output_path):
    """Plot elevation vs distance-to-ridge to validate ridge template."""
    oceanic = df[df['oceanic'] == 1].copy()

    fig, axes = plt.subplots(2, 2, figsize=(14, 10))

    # 1. Elevation vs dGamma (main profile)
    ax = axes[0, 0]
    ax.scatter(oceanic['dGamma_km'], oceanic['elevation_m'],
               alpha=0.3, s=1, c='blue', label='Oceanic vertices')

    # Theoretical ridge template (quadratic)
    dg_theory = np.linspace(0, 1500, 100)
    t = np.clip(dg_theory / 1000.0, 0, 1)
    s = t * t
    z_theory = -1000 * (1 - s) + (-6000) * s
    ax.plot(dg_theory, z_theory, 'r-', linewidth=2, label='Paper template (quadratic)')

    ax.set_xlabel('Distance to Ridge (km)')
    ax.set_ylabel('Elevation (m)')
    ax.set_title('Ridge Template Validation')
    ax.legend()
    ax.grid(True, alpha=0.3)
    ax.set_xlim(0, 1500)
    ax.set_ylim(-7000, 0)

    # 2. Alpha distribution
    ax = axes[0, 1]
    ax.hist(oceanic['alpha'], bins=50, color='green', alpha=0.7, edgecolor='black')
    ax.set_xlabel('Alpha')
    ax.set_ylabel('Vertex Count')
    ax.set_title('Alpha Blending Distribution')
    ax.axvline(0.5, color='red', linestyle='--', label='α=0.5 (equal blend)')
    ax.legend()
    ax.grid(True, alpha=0.3)

    # 3. Alpha vs dGamma (spatial blending)
    ax = axes[1, 0]
    sc = ax.scatter(oceanic['dGamma_km'], oceanic['alpha'],
                    c=oceanic['elevation_m'], s=2, cmap='viridis', alpha=0.5)
    plt.colorbar(sc, ax=ax, label='Elevation (m)')
    ax.set_xlabel('Distance to Ridge (km)')
    ax.set_ylabel('Alpha')
    ax.set_title('Alpha Blending vs Distance')
    ax.grid(True, alpha=0.3)
    ax.set_xlim(0, 2000)

    # 4. Baseline vs final elevation comparison
    ax = axes[1, 1]
    near_ridge = oceanic[oceanic['dGamma_km'] < 200].copy()
    ax.scatter(near_ridge['baseline_m'], near_ridge['elevation_m'],
               alpha=0.5, s=5, c='purple')
    lims = [-7000, -4000]
    ax.plot(lims, lims, 'k--', alpha=0.5, label='No change')
    ax.set_xlabel('Baseline Elevation (m)')
    ax.set_ylabel('Final Elevation (m)')
    ax.set_title('Baseline vs Final (dGamma < 200km)')
    ax.legend()
    ax.grid(True, alpha=0.3)

    plt.tight_layout()
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    print(f"✓ Elevation profile plot: {output_path}")
    plt.close()


def plot_alpha_heatmap(df, output_path):
    """Generate alpha heatmap on sphere projection."""
    oceanic = df[df['oceanic'] == 1].copy()

    fig, axes = plt.subplots(1, 2, figsize=(14, 6))

    # 1. Alpha on lat/lon grid
    ax = axes[0]
    sc = ax.scatter(oceanic['lon_deg'], oceanic['lat_deg'],
                    c=oceanic['alpha'], s=5, cmap='RdYlBu_r',
                    vmin=0, vmax=1, alpha=0.8)
    plt.colorbar(sc, ax=ax, label='Alpha (0=ridge, 1=interior)')
    ax.set_xlabel('Longitude (deg)')
    ax.set_ylabel('Latitude (deg)')
    ax.set_title('Alpha Blending Heatmap')
    ax.grid(True, alpha=0.3)
    ax.set_xlim(-180, 180)
    ax.set_ylim(-90, 90)

    # 2. Elevation on lat/lon grid
    ax = axes[1]
    sc = ax.scatter(oceanic['lon_deg'], oceanic['lat_deg'],
                    c=oceanic['elevation_m'], s=5, cmap='terrain',
                    vmin=-6500, vmax=-500, alpha=0.8)
    plt.colorbar(sc, ax=ax, label='Elevation (m)')
    ax.set_xlabel('Longitude (deg)')
    ax.set_ylabel('Latitude (deg)')
    ax.set_title('Oceanic Elevation Heatmap')
    ax.grid(True, alpha=0.3)
    ax.set_xlim(-180, 180)
    ax.set_ylim(-90, 90)

    plt.tight_layout()
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    print(f"✓ Alpha heatmap: {output_path}")
    plt.close()


def plot_cross_boundary_transect(df, output_path):
    """Plot elevation smoothness across divergent boundary."""
    fig, axes = plt.subplots(2, 1, figsize=(12, 8))

    # 1. Elevation vs distance from equator
    ax = axes[0]
    ax.plot(df['distance_from_equator_km'], df['elevation_m'],
            'b-', linewidth=1, alpha=0.7, label='Elevation')
    ax.axvline(0, color='red', linestyle='--', linewidth=2, label='Divergent boundary (equator)')
    ax.set_xlabel('Distance from Equator (km)')
    ax.set_ylabel('Elevation (m)')
    ax.set_title('Cross-Boundary Elevation Profile')
    ax.legend()
    ax.grid(True, alpha=0.3)

    # Highlight plate boundaries
    for i in range(1, len(df)):
        if df.iloc[i]['plate_id'] != df.iloc[i-1]['plate_id']:
            ax.axvline(df.iloc[i]['distance_from_equator_km'],
                      color='orange', linestyle=':', alpha=0.5)

    # 2. Alpha vs distance (blending behavior near boundary)
    ax = axes[1]
    ax.plot(df['distance_from_equator_km'], df['alpha'],
            'g-', linewidth=1, alpha=0.7, label='Alpha')
    ax.axvline(0, color='red', linestyle='--', linewidth=2, label='Divergent boundary')
    ax.axhline(0.5, color='black', linestyle=':', alpha=0.5, label='α=0.5')
    ax.set_xlabel('Distance from Equator (km)')
    ax.set_ylabel('Alpha')
    ax.set_title('Alpha Blending Across Boundary')
    ax.legend()
    ax.grid(True, alpha=0.3)
    ax.set_ylim(0, 1)

    plt.tight_layout()
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    print(f"✓ Cross-boundary transect: {output_path}")
    plt.close()


def plot_ridge_directions(df, output_path):
    """Plot ridge direction field to validate tangency."""
    fig, axes = plt.subplots(1, 2, figsize=(14, 6))

    # Sample subset for clarity
    sample = df.sample(min(500, len(df)))

    # 1. 3D ridge directions (projected to 2D)
    ax = axes[0]
    ax.quiver(sample['lon_deg'], sample['lat_deg'],
              sample['ridge_dir_x'], sample['ridge_dir_y'],
              alpha=0.6, color='blue', scale=20)
    ax.set_xlabel('Longitude (deg)')
    ax.set_ylabel('Latitude (deg)')
    ax.set_title('Ridge Direction Field (XY projection)')
    ax.grid(True, alpha=0.3)
    ax.set_aspect('equal')

    # 2. Ridge direction magnitude vs distance
    ax = axes[1]
    ax.scatter(df['dGamma_km'],
               np.sqrt(df['ridge_dir_x']**2 + df['ridge_dir_y']**2 + df['ridge_dir_z']**2),
               alpha=0.5, s=5, c='green')
    ax.set_xlabel('Distance to Ridge (km)')
    ax.set_ylabel('Ridge Direction Magnitude')
    ax.set_title('Ridge Direction Normalization Check')
    ax.axhline(1.0, color='red', linestyle='--', label='Expected (unit vector)')
    ax.legend()
    ax.grid(True, alpha=0.3)

    plt.tight_layout()
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    print(f"✓ Ridge direction field: {output_path}")
    plt.close()


def compute_validation_metrics(profile_df, transect_df, ridge_df):
    """Compute quantitative validation metrics."""
    oceanic = profile_df[profile_df['oceanic'] == 1].copy()

    # Metric 1: Ridge template RMSE
    near_ridge = oceanic[oceanic['dGamma_km'] <= 1000].copy()
    t = np.clip(near_ridge['dGamma_km'] / 1000.0, 0, 1)
    s = t * t
    z_theory = -1000 * (1 - s) + (-6000) * s
    rmse = np.sqrt(np.mean((near_ridge['elevation_m'].values - z_theory)**2))

    # Metric 2: Alpha range coverage
    alpha_min = oceanic['alpha'].min()
    alpha_max = oceanic['alpha'].max()
    alpha_mean = oceanic['alpha'].mean()

    # Metric 3: Cross-boundary gradient (max elevation jump)
    max_jump = 0
    for i in range(1, len(transect_df)):
        if transect_df.iloc[i]['plate_id'] != transect_df.iloc[i-1]['plate_id']:
            jump = abs(transect_df.iloc[i]['elevation_m'] - transect_df.iloc[i-1]['elevation_m'])
            max_jump = max(max_jump, jump)

    # Metric 4: Ridge direction tangency
    ridge_dots = np.abs(
        ridge_df['ridge_dir_x'] * ridge_df['pos_x'] +
        ridge_df['ridge_dir_y'] * ridge_df['pos_y'] +
        ridge_df['ridge_dir_z'] * ridge_df['pos_z']
    )
    tangent_pct = (ridge_dots < 0.01).sum() / len(ridge_dots) * 100

    print("\n" + "="*60)
    print("PHASE 5 VALIDATION METRICS")
    print("="*60)
    print(f"Ridge Template RMSE:         {rmse:.1f} m")
    print(f"  (Expected: <500m for good match)")
    print(f"\nAlpha Range:                 [{alpha_min:.3f}, {alpha_max:.3f}]")
    print(f"  (Expected: ~[0.0, 1.0])")
    print(f"Alpha Mean:                  {alpha_mean:.3f}")
    print(f"  (Expected: ~0.5 for balanced blending)")
    print(f"\nMax Cross-Boundary Jump:     {max_jump:.1f} m")
    print(f"  (Expected: <200m for smooth interpolation)")
    print(f"\nRidge Direction Tangency:    {tangent_pct:.1f}% tangent (dot < 0.01)")
    print(f"  (Expected: >95%)")
    print("="*60 + "\n")

    # Pass/fail assessment
    passed = []
    failed = []

    if rmse < 500:
        passed.append("✓ Ridge template RMSE")
    else:
        failed.append(f"✗ Ridge template RMSE ({rmse:.1f}m > 500m)")

    if alpha_min < 0.1 and alpha_max > 0.9:
        passed.append("✓ Alpha range coverage")
    else:
        failed.append(f"✗ Alpha range ({alpha_min:.2f}, {alpha_max:.2f})")

    if max_jump < 200:
        passed.append("✓ Cross-boundary smoothness")
    else:
        failed.append(f"✗ Cross-boundary jump ({max_jump:.1f}m > 200m)")

    if tangent_pct > 95:
        passed.append("✓ Ridge direction tangency")
    else:
        failed.append(f"✗ Ridge tangency ({tangent_pct:.1f}% < 95%)")

    print("VALIDATION RESULTS:")
    for p in passed:
        print(f"  {p}")
    for f in failed:
        print(f"  {f}")

    if len(failed) == 0:
        print("\n🎉 ALL VALIDATION CHECKS PASSED")
        return True
    else:
        print(f"\n⚠️  {len(failed)}/{len(passed)+len(failed)} checks failed")
        return False


def main():
    parser = argparse.ArgumentParser(description='Phase 5 Oceanic Crust Validation Visualizer')
    parser.add_argument('--validation-dir', type=str,
                       default='Docs/Automation/Validation/Phase5',
                       help='Path to validation directory')
    args = parser.parse_args()

    # Resolve paths
    base_dir = Path(args.validation_dir)
    profile_csv = base_dir / 'oceanic_elevation_profile.csv'
    ridge_csv = base_dir / 'ridge_directions.csv'
    transect_csv = base_dir / 'cross_boundary_transect.csv'

    # Check files exist
    for f in [profile_csv, ridge_csv, transect_csv]:
        if not f.exists():
            print(f"✗ Missing: {f}")
            print(f"\nRun OceanicVisualizationTest first to generate CSV artifacts.")
            return 1

    # Load data
    print("Loading CSV artifacts...")
    profile_df = pd.read_csv(profile_csv)
    ridge_df = pd.read_csv(ridge_csv)
    transect_df = pd.read_csv(transect_csv)

    print(f"  Profile vertices: {len(profile_df)}")
    print(f"  Ridge vertices: {len(ridge_df)}")
    print(f"  Transect samples: {len(transect_df)}\n")

    # Generate plots
    print("Generating validation plots...")
    plot_elevation_profile(profile_df, base_dir / 'phase5_elevation_profile.png')
    plot_alpha_heatmap(profile_df, base_dir / 'phase5_alpha_heatmap.png')
    plot_cross_boundary_transect(transect_df, base_dir / 'phase5_cross_boundary.png')
    plot_ridge_directions(ridge_df, base_dir / 'phase5_ridge_directions.png')

    # Compute metrics
    success = compute_validation_metrics(profile_df, transect_df, ridge_df)

    return 0 if success else 1


if __name__ == '__main__':
    exit(main())
