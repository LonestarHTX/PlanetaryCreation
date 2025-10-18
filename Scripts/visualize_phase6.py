#!/usr/bin/env python3
"""
Phase 6: Continental Erosion Visualization

Generates validation plots from CSV artifacts:
1. Erosion Rate vs Elevation (correlation plot)
2. Erosion Rate Heatmap (spatial distribution)
3. Elevation Time Series (equilibrium validation)
4. Elevation Distribution (before/after histogram)
"""

import argparse
import sys
from pathlib import Path
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches

def plot_erosion_vs_elevation(df, output_path):
    """Plot erosion rate vs elevation to validate formula."""
    # Filter to continental crust only
    continental = df[df['crust_type'] == 'Continental'].copy()

    if len(continental) == 0:
        print("[WARNING] No continental vertices found")
        return

    fig, axes = plt.subplots(2, 2, figsize=(14, 10))

    # Plot 1: Erosion rate vs elevation (scatter)
    ax = axes[0, 0]
    scatter = ax.scatter(continental['elevation_m'], continental['erosion_rate_m_per_My'],
                        c=continental['stress_MPa'], cmap='plasma', alpha=0.6, s=10)
    ax.set_xlabel('Elevation (m)')
    ax.set_ylabel('Erosion Rate (m/My)')
    ax.set_title('Erosion Rate vs Elevation (colored by stress)')
    ax.grid(True, alpha=0.3)
    cbar = plt.colorbar(scatter, ax=ax)
    cbar.set_label('Stress (MPa)')

    # Plot 2: Erosion rate histogram
    ax = axes[0, 1]
    eroding = continental[continental['erosion_rate_m_per_My'] > 0.0]
    ax.hist(eroding['erosion_rate_m_per_My'], bins=50, color='coral', alpha=0.7, edgecolor='black')
    ax.set_xlabel('Erosion Rate (m/My)')
    ax.set_ylabel('Vertex Count')
    ax.set_title(f'Erosion Rate Distribution (n={len(eroding)})')
    ax.grid(True, alpha=0.3, axis='y')

    # Plot 3: Elevation histogram (continental vs oceanic)
    ax = axes[1, 0]
    cont_elev = df[df['crust_type'] == 'Continental']['elevation_m']
    ocean_elev = df[df['crust_type'] == 'Oceanic']['elevation_m']
    ax.hist([cont_elev, ocean_elev], bins=50, label=['Continental', 'Oceanic'],
           color=['brown', 'blue'], alpha=0.6, edgecolor='black')
    ax.axvline(0, color='red', linestyle='--', linewidth=2, label='Sea Level')
    ax.set_xlabel('Elevation (m)')
    ax.set_ylabel('Vertex Count')
    ax.set_title('Elevation Distribution by Crust Type')
    ax.legend()
    ax.grid(True, alpha=0.3, axis='y')

    # Plot 4: Stress vs erosion (validation check)
    ax = axes[1, 1]
    high_elev = continental[continental['elevation_m'] > 1000].copy()
    if len(high_elev) > 0:
        ax.scatter(high_elev['stress_MPa'], high_elev['erosion_rate_m_per_My'],
                  c=high_elev['elevation_m'], cmap='terrain', alpha=0.6, s=20)
        ax.set_xlabel('Stress (MPa)')
        ax.set_ylabel('Erosion Rate (m/My)')
        ax.set_title('Stress vs Erosion (elevation > 1000m)')
        ax.grid(True, alpha=0.3)

    plt.tight_layout()
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    print(f"[OK] Erosion vs elevation plot: {output_path}")
    plt.close()


def plot_erosion_heatmap(df, output_path):
    """Plot spatial distribution of erosion rates."""
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 6))

    # Left: Erosion rate heatmap
    continental = df[df['crust_type'] == 'Continental']
    if len(continental) > 0:
        scatter1 = ax1.scatter(continental['lon_deg'], continental['lat_deg'],
                              c=continental['erosion_rate_m_per_My'], cmap='hot_r',
                              s=5, alpha=0.7, vmin=0)
        ax1.set_xlabel('Longitude (deg)')
        ax1.set_ylabel('Latitude (deg)')
        ax1.set_title('Erosion Rate Spatial Distribution')
        ax1.set_xlim(-180, 180)
        ax1.set_ylim(-90, 90)
        ax1.grid(True, alpha=0.3)
        cbar1 = plt.colorbar(scatter1, ax=ax1)
        cbar1.set_label('Erosion Rate (m/My)')

    # Right: Elevation heatmap
    scatter2 = ax2.scatter(df['lon_deg'], df['lat_deg'],
                          c=df['elevation_m'], cmap='terrain',
                          s=5, alpha=0.7, vmin=-7000, vmax=6000)
    ax2.set_xlabel('Longitude (deg)')
    ax2.set_ylabel('Latitude (deg)')
    ax2.set_title('Elevation After Erosion')
    ax2.set_xlim(-180, 180)
    ax2.set_ylim(-90, 90)
    ax2.grid(True, alpha=0.3)
    cbar2 = plt.colorbar(scatter2, ax=ax2)
    cbar2.set_label('Elevation (m)')

    plt.tight_layout()
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    print(f"[OK] Erosion heatmap: {output_path}")
    plt.close()


def plot_time_series(df, output_path):
    """Plot elevation evolution over time for sample vertices."""
    fig, axes = plt.subplots(2, 2, figsize=(14, 10))

    # Get unique vertices
    vertices = df['vertex_id'].unique()

    # Plot elevation time series for each sample vertex
    ax = axes[0, 0]
    for vid in vertices[:10]:  # Plot first 10 vertices
        vertex_data = df[df['vertex_id'] == vid]
        ax.plot(vertex_data['time_My'], vertex_data['elevation_m'],
               alpha=0.6, linewidth=1.5)
    ax.set_xlabel('Time (My)')
    ax.set_ylabel('Elevation (m)')
    ax.set_title('Elevation Evolution (Sample Vertices)')
    ax.grid(True, alpha=0.3)
    ax.axhline(0, color='blue', linestyle='--', alpha=0.5, label='Sea Level')

    # Plot erosion rate time series
    ax = axes[0, 1]
    for vid in vertices[:10]:
        vertex_data = df[df['vertex_id'] == vid]
        ax.plot(vertex_data['time_My'], vertex_data['erosion_rate_m_per_My'],
               alpha=0.6, linewidth=1.5)
    ax.set_xlabel('Time (My)')
    ax.set_ylabel('Erosion Rate (m/My)')
    ax.set_title('Erosion Rate Evolution')
    ax.grid(True, alpha=0.3)

    # Average elevation over time (equilibrium check)
    ax = axes[1, 0]
    avg_by_time = df.groupby('time_My').agg({
        'elevation_m': ['mean', 'std', 'min', 'max']
    }).reset_index()
    times = avg_by_time['time_My']
    mean_elev = avg_by_time['elevation_m']['mean']
    std_elev = avg_by_time['elevation_m']['std']

    ax.plot(times, mean_elev, color='darkgreen', linewidth=2, label='Mean')
    ax.fill_between(times, mean_elev - std_elev, mean_elev + std_elev,
                    alpha=0.3, color='green', label='±1 std')
    ax.set_xlabel('Time (My)')
    ax.set_ylabel('Mean Elevation (m)')
    ax.set_title('Average Continental Elevation (Equilibrium Check)')
    ax.legend()
    ax.grid(True, alpha=0.3)

    # Elevation range over time
    ax = axes[1, 1]
    min_elev = avg_by_time['elevation_m']['min']
    max_elev = avg_by_time['elevation_m']['max']
    ax.plot(times, max_elev, color='red', linewidth=2, label='Max')
    ax.plot(times, mean_elev, color='green', linewidth=2, label='Mean')
    ax.plot(times, min_elev, color='blue', linewidth=2, label='Min')
    ax.fill_between(times, min_elev, max_elev, alpha=0.2, color='gray')
    ax.set_xlabel('Time (My)')
    ax.set_ylabel('Elevation (m)')
    ax.set_title('Elevation Range Over Time')
    ax.legend()
    ax.grid(True, alpha=0.3)

    plt.tight_layout()
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    print(f"[OK] Time series plot: {output_path}")
    plt.close()


def plot_elevation_histogram(df, output_path):
    """Plot before/after elevation distribution."""
    # Filter continental only
    continental = df[df['crust_type'] == 'Continental']

    if len(continental) == 0:
        print("[WARNING] No continental vertices for histogram")
        return

    fig, axes = plt.subplots(2, 2, figsize=(14, 10))

    # Histogram: Initial elevation
    ax = axes[0, 0]
    ax.hist(continental['initial_elevation_m'], bins=50, color='lightcoral',
           alpha=0.7, edgecolor='black')
    ax.axvline(0, color='red', linestyle='--', linewidth=2, label='Sea Level')
    ax.set_xlabel('Elevation (m)')
    ax.set_ylabel('Vertex Count')
    ax.set_title(f'Initial Elevation Distribution (t=10 My)')
    ax.legend()
    ax.grid(True, alpha=0.3, axis='y')

    # Histogram: Final elevation
    ax = axes[0, 1]
    ax.hist(continental['final_elevation_m'], bins=50, color='lightgreen',
           alpha=0.7, edgecolor='black')
    ax.axvline(0, color='red', linestyle='--', linewidth=2, label='Sea Level')
    ax.set_xlabel('Elevation (m)')
    ax.set_ylabel('Vertex Count')
    ax.set_title(f'Final Elevation Distribution (t=40 My)')
    ax.legend()
    ax.grid(True, alpha=0.3, axis='y')

    # Scatter: Before vs After
    ax = axes[1, 0]
    ax.scatter(continental['initial_elevation_m'], continental['final_elevation_m'],
              alpha=0.5, s=10, color='purple')

    # Add diagonal (no change line)
    min_val = min(continental['initial_elevation_m'].min(), continental['final_elevation_m'].min())
    max_val = max(continental['initial_elevation_m'].max(), continental['final_elevation_m'].max())
    ax.plot([min_val, max_val], [min_val, max_val], 'r--', linewidth=2, label='No change')

    ax.set_xlabel('Initial Elevation (m)')
    ax.set_ylabel('Final Elevation (m)')
    ax.set_title('Elevation Change (Initial vs Final)')
    ax.legend()
    ax.grid(True, alpha=0.3)
    ax.axis('equal')

    # Histogram: Elevation change
    ax = axes[1, 1]
    changes = continental['elevation_change_m']
    ax.hist(changes, bins=50, color='orange', alpha=0.7, edgecolor='black')
    ax.axvline(0, color='red', linestyle='--', linewidth=2, label='No change')
    ax.set_xlabel('Elevation Change (m)')
    ax.set_ylabel('Vertex Count')
    ax.set_title('Elevation Change Distribution')

    # Stats annotation
    mean_change = changes.mean()
    median_change = changes.median()
    ax.axvline(mean_change, color='blue', linestyle=':', linewidth=2, label=f'Mean: {mean_change:.1f}m')
    ax.legend()
    ax.grid(True, alpha=0.3, axis='y')

    plt.tight_layout()
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    print(f"[OK] Elevation histogram: {output_path}")
    plt.close()


def compute_validation_metrics(erosion_df, timeseries_df, histogram_df):
    """Compute validation metrics for Phase 6."""
    continental = erosion_df[erosion_df['crust_type'] == 'Continental']

    # Metric 1: Erosion only above sea level
    above_sea = continental[continental['elevation_m'] > 0]
    below_sea = continental[continental['elevation_m'] <= 0]
    eroding_above = (above_sea['erosion_rate_m_per_My'] > 0).sum()
    eroding_below = (below_sea['erosion_rate_m_per_My'] > 0).sum()

    # Metric 2: Equilibrium check (elevation stable in final 10 My)
    if len(timeseries_df) > 0:
        final_steps = timeseries_df[timeseries_df['time_My'] >= 30.0]
        if len(final_steps) > 0:
            avg_by_step = final_steps.groupby('time_My')['elevation_m'].mean()
            if len(avg_by_step) > 1:
                elevation_variance = avg_by_step.std()
            else:
                elevation_variance = 0.0
        else:
            elevation_variance = 0.0
    else:
        elevation_variance = 0.0

    # Metric 3: Mean elevation change
    cont_hist = histogram_df[histogram_df['crust_type'] == 'Continental']
    mean_change = cont_hist['elevation_change_m'].mean() if len(cont_hist) > 0 else 0.0

    # Metric 4: Erosion-elevation correlation
    if len(above_sea) > 10:
        correlation = np.corrcoef(above_sea['elevation_m'], above_sea['erosion_rate_m_per_My'])[0, 1]
    else:
        correlation = 0.0

    print("\n" + "="*60)
    print("PHASE 6 VALIDATION METRICS")
    print("="*60)
    print(f"Continental vertices:        {len(continental)}")
    print(f"  Above sea level:           {len(above_sea)} (eroding: {eroding_above})")
    print(f"  Below sea level:           {len(below_sea)} (eroding: {eroding_below})")
    print(f"\nEquilibrium variance:        {elevation_variance:.1f} m")
    print(f"  (Expected: <200m for stable equilibrium)")
    print(f"\nMean elevation change:       {mean_change:.1f} m")
    print(f"  (Expected: near 0 for uplift/erosion balance)")
    print(f"\nErosion-Elevation correlation: {correlation:.3f}")
    print(f"  (Expected: >0.3 for formula correctness)")
    print("="*60 + "\n")

    # Pass/fail assessment
    passed = []
    failed = []

    if eroding_below == 0:
        passed.append("[PASS] No erosion below sea level")
    else:
        failed.append(f"[FAIL] Erosion below sea level ({eroding_below} vertices)")

    if eroding_above > 0:
        passed.append("[PASS] Erosion active above sea level")
    else:
        failed.append("[FAIL] No erosion above sea level")

    if elevation_variance < 200:
        passed.append("[PASS] Equilibrium reached (variance < 200m)")
    else:
        failed.append(f"[FAIL] No equilibrium ({elevation_variance:.1f}m variance)")

    if correlation > 0.3:
        passed.append("[PASS] Erosion-elevation correlation")
    else:
        failed.append(f"[FAIL] Weak correlation ({correlation:.3f})")

    print("VALIDATION RESULTS:")
    for p in passed:
        print(f"  {p}")
    for f in failed:
        print(f"  {f}")

    if len(failed) == 0:
        print("\n[SUCCESS] ALL VALIDATION CHECKS PASSED")
        return True
    else:
        print(f"\n[WARNING] {len(failed)}/{len(passed)+len(failed)} checks failed")
        return False


def main():
    parser = argparse.ArgumentParser(description='Phase 6 Continental Erosion Visualizer')
    parser.add_argument('--base-dir', type=Path,
                       default=Path('Docs/Automation/Validation/Phase6'),
                       help='Base directory for CSV files')
    args = parser.parse_args()

    base_dir = args.base_dir

    # Expected CSV files
    erosion_csv = base_dir / 'erosion_profile.csv'
    timeseries_csv = base_dir / 'erosion_timeseries.csv'
    histogram_csv = base_dir / 'elevation_histogram.csv'

    # Check files exist
    for f in [erosion_csv, timeseries_csv, histogram_csv]:
        if not f.exists():
            print(f"[MISSING] {f}")
            print(f"\nRun ContinentalErosionVisualizationTest first to generate CSV artifacts.")
            return 1

    # Load data
    print("Loading CSV artifacts...")
    erosion_df = pd.read_csv(erosion_csv)
    timeseries_df = pd.read_csv(timeseries_csv)
    histogram_df = pd.read_csv(histogram_csv)

    print(f"  Erosion profile vertices: {len(erosion_df)}")
    print(f"  Time series samples: {timeseries_df['vertex_id'].nunique()} vertices x {timeseries_df['step'].nunique()} steps")
    print(f"  Histogram vertices: {len(histogram_df)}")

    # Generate plots
    print("\nGenerating validation plots...")
    plot_erosion_vs_elevation(erosion_df, base_dir / 'phase6_erosion_vs_elevation.png')
    plot_erosion_heatmap(erosion_df, base_dir / 'phase6_erosion_heatmap.png')
    plot_time_series(timeseries_df, base_dir / 'phase6_time_series.png')
    plot_elevation_histogram(histogram_df, base_dir / 'phase6_elevation_histogram.png')

    # Validation metrics
    success = compute_validation_metrics(erosion_df, timeseries_df, histogram_df)

    return 0 if success else 1


if __name__ == '__main__':
    exit(main())
