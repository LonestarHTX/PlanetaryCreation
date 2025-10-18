#!/usr/bin/env python3
"""
Phase 3 Subduction Visual Validation
Compares uplift profile to paper Figure 5 (page 5)
"""

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
from pathlib import Path

# Paths
PROJECT_ROOT = Path(__file__).parent.parent
CSV_PATH = PROJECT_ROOT / "Docs/Automation/Validation/Phase3/uplift_heatmap.csv"
OUTPUT_DIR = PROJECT_ROOT / "Docs/Automation/Validation/Phase3"

# Paper constants (from Appendix A, page 11)
SUBDUCTION_CONTROL_DISTANCE_KM = 1200.0  # rc: peak uplift distance
SUBDUCTION_DISTANCE_KM = 1800.0          # rs: max uplift distance

def main():
    print("=" * 80)
    print("PHASE 3 SUBDUCTION VISUAL VALIDATION")
    print("=" * 80)

    # Load data
    if not CSV_PATH.exists():
        print(f"ERROR: CSV not found at {CSV_PATH}")
        print("Run the automation test first:")
        print('  Automation RunTests PlanetaryCreation.Paper.Subduction.Visualization')
        return

    df = pd.read_csv(CSV_PATH)
    print(f"\nLoaded {len(df)} vertices from: {CSV_PATH}")

    # Statistics
    uplifted = df[df['Elevation_m'] > 0]
    print(f"\n--- UPLIFT STATISTICS ---")
    print(f"Uplifted vertices: {len(uplifted)} / {len(df)} ({100.0 * len(uplifted) / len(df):.1f}%)")
    print(f"Max elevation: {df['Elevation_m'].max():.1f} m")
    print(f"Mean elevation (uplifted only): {uplifted['Elevation_m'].mean():.1f} m")
    print(f"Std dev (uplifted): {uplifted['Elevation_m'].std():.1f} m")

    # Create figure with 3 subplots
    fig = plt.figure(figsize=(16, 10))

    # --- Subplot 1: Global heatmap ---
    ax1 = plt.subplot(2, 2, 1)
    scatter1 = ax1.scatter(df['Longitude'], df['Latitude'],
                          c=df['Elevation_m'], cmap='RdYlBu_r',
                          s=3, vmin=0, vmax=df['Elevation_m'].max(),
                          edgecolors='none', alpha=0.8)
    ax1.set_xlabel('Longitude (°)', fontsize=11)
    ax1.set_ylabel('Latitude (°)', fontsize=11)
    ax1.set_title('Subduction Uplift - Global View (20 My)', fontsize=12, fontweight='bold')
    ax1.axhline(0, color='black', linewidth=1.5, linestyle='--',
                label='Equator (convergent front)', alpha=0.7)
    ax1.grid(True, alpha=0.2)
    ax1.legend(loc='upper right', fontsize=9)
    plt.colorbar(scatter1, ax=ax1, label='Uplift (m)', pad=0.02)

    # --- Subplot 2: Distance field ---
    ax2 = plt.subplot(2, 2, 2)
    scatter2 = ax2.scatter(df['Longitude'], df['Latitude'],
                          c=df['DistToFront_km'], cmap='viridis',
                          s=3, vmin=0, vmax=2500,
                          edgecolors='none', alpha=0.8)
    ax2.set_xlabel('Longitude (°)', fontsize=11)
    ax2.set_ylabel('Latitude (°)', fontsize=11)
    ax2.set_title('Distance to Convergent Front', fontsize=12, fontweight='bold')
    ax2.axhline(0, color='white', linewidth=1.5, linestyle='--', alpha=0.7)
    ax2.grid(True, alpha=0.2)
    plt.colorbar(scatter2, ax=ax2, label='Distance (km)', pad=0.02)

    # --- Subplot 3: Uplift Profile (PAPER COMPARISON) ---
    ax3 = plt.subplot(2, 1, 2)

    # Filter to near-front vertices and bin by distance
    df_near = df[df['DistToFront_km'] < 2500].copy()

    # Create distance bins (50 km intervals)
    bins = np.arange(0, 2500, 50)
    bin_centers = bins[:-1] + 25

    # Bin and compute statistics
    df_near['DistBin'] = pd.cut(df_near['DistToFront_km'], bins, include_lowest=True)
    profile_mean = df_near.groupby('DistBin')['Elevation_m'].mean()
    profile_max = df_near.groupby('DistBin')['Elevation_m'].max()
    profile_std = df_near.groupby('DistBin')['Elevation_m'].std()

    # Plot profile with error band
    ax3.plot(bin_centers, profile_mean, 'b-', linewidth=2.5,
             label='Mean Uplift (Your Implementation)', zorder=3)
    ax3.fill_between(bin_centers,
                     profile_mean - profile_std,
                     profile_mean + profile_std,
                     alpha=0.2, color='blue', label='±1 Std Dev', zorder=2)
    ax3.plot(bin_centers, profile_max, 'b--', linewidth=1, alpha=0.5,
             label='Max in Bin', zorder=2)

    # Mark paper constants
    ax3.axvline(SUBDUCTION_CONTROL_DISTANCE_KM, color='green', linewidth=2,
               linestyle='--', label=f'rc = {SUBDUCTION_CONTROL_DISTANCE_KM:.0f} km (control distance)',
               alpha=0.7, zorder=1)
    ax3.axvline(SUBDUCTION_DISTANCE_KM, color='red', linewidth=2,
               linestyle='--', label=f'rs = {SUBDUCTION_DISTANCE_KM:.0f} km (max distance)',
               alpha=0.7, zorder=1)

    # Shaded region where uplift should occur (paper prediction)
    ax3.axvspan(0, SUBDUCTION_DISTANCE_KM, alpha=0.1, color='yellow',
                label='Paper: Uplift Region [0, rs]', zorder=0)

    ax3.set_xlabel('Distance from Convergent Front (km)', fontsize=12, fontweight='bold')
    ax3.set_ylabel('Elevation (m)', fontsize=12, fontweight='bold')
    ax3.set_title('Subduction Uplift Profile - Paper Comparison (Figure 5)',
                  fontsize=13, fontweight='bold')
    ax3.grid(True, alpha=0.3, zorder=0)
    ax3.legend(loc='upper right', fontsize=9, framealpha=0.9)
    ax3.set_xlim(0, 2500)
    ax3.set_ylim(bottom=0)

    # Add annotations
    peak_idx = profile_mean.idxmax()
    peak_dist = bin_centers[profile_mean.index.get_loc(peak_idx)]
    peak_elev = profile_mean.max()
    ax3.annotate(f'Peak: {peak_elev:.0f} m\nat {peak_dist:.0f} km',
                xy=(peak_dist, peak_elev),
                xytext=(peak_dist + 300, peak_elev * 0.7),
                fontsize=10, fontweight='bold',
                bbox=dict(boxstyle='round,pad=0.5', facecolor='yellow', alpha=0.7),
                arrowprops=dict(arrowstyle='->', lw=1.5, color='black'))

    plt.tight_layout()

    # Save figure
    output_path = OUTPUT_DIR / "phase3_validation.png"
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    print(f"\n✅ Saved visualization to: {output_path}")

    # --- VALIDATION CHECKS ---
    print("\n" + "=" * 80)
    print("PAPER COMPLIANCE CHECKS")
    print("=" * 80)

    checks_passed = 0
    checks_total = 0

    # Check 1: Uplift exists
    checks_total += 1
    if len(uplifted) > 0:
        print("✅ CHECK 1: Uplift occurs (vertices with elevation > 0)")
        checks_passed += 1
    else:
        print("❌ CHECK 1: NO UPLIFT DETECTED")

    # Check 2: Uplift confined to [0, rs]
    checks_total += 1
    far_vertices = df[df['DistToFront_km'] > SUBDUCTION_DISTANCE_KM]
    far_uplifted = far_vertices[far_vertices['Elevation_m'] > 1.0]  # tolerance: 1m
    if len(far_uplifted) == 0:
        print(f"✅ CHECK 2: Uplift confined to [0, {SUBDUCTION_DISTANCE_KM:.0f}] km (zero beyond rs)")
        checks_passed += 1
    else:
        print(f"⚠️  CHECK 2: {len(far_uplifted)} vertices uplifted beyond rs (may be numerical noise)")

    # Check 3: Peak near rc
    checks_total += 1
    if 800 <= peak_dist <= 1600:  # Tolerance: ±400 km around rc=1200
        print(f"✅ CHECK 3: Peak uplift at {peak_dist:.0f} km (near rc = {SUBDUCTION_CONTROL_DISTANCE_KM:.0f} km)")
        checks_passed += 1
    else:
        print(f"❌ CHECK 3: Peak at {peak_dist:.0f} km (expected near {SUBDUCTION_CONTROL_DISTANCE_KM:.0f} km)")

    # Check 4: Smooth profile (no discontinuities)
    checks_total += 1
    profile_diff = np.abs(np.diff(profile_mean.values))
    max_jump = np.nanmax(profile_diff)
    if max_jump < peak_elev * 0.3:  # Max jump < 30% of peak
        print(f"✅ CHECK 4: Smooth profile (max jump: {max_jump:.1f} m < 30% of peak)")
        checks_passed += 1
    else:
        print(f"⚠️  CHECK 4: Discontinuity detected (max jump: {max_jump:.1f} m)")

    # Check 5: Reasonable uplift magnitude (10-5000 m for 20 My)
    checks_total += 1
    if 10.0 <= peak_elev <= 5000.0:
        print(f"✅ CHECK 5: Uplift magnitude reasonable ({peak_elev:.1f} m after 20 My)")
        checks_passed += 1
    else:
        print(f"⚠️  CHECK 5: Uplift magnitude unusual ({peak_elev:.1f} m)")

    # Check 6: Symmetric across equator
    checks_total += 1
    north = df[df['Latitude'] > 5]
    south = df[df['Latitude'] < -5]
    north_mean = north[north['Elevation_m'] > 0]['Elevation_m'].mean()
    south_mean = south[south['Elevation_m'] > 0]['Elevation_m'].mean()
    if pd.notna(north_mean) and pd.notna(south_mean):
        asymmetry = abs(north_mean - south_mean) / max(north_mean, south_mean)
        if asymmetry < 0.2:  # < 20% difference
            print(f"✅ CHECK 6: Symmetric uplift (North: {north_mean:.1f} m, South: {south_mean:.1f} m, Δ={asymmetry*100:.1f}%)")
            checks_passed += 1
        else:
            print(f"⚠️  CHECK 6: Asymmetric (North: {north_mean:.1f} m, South: {south_mean:.1f} m, Δ={asymmetry*100:.1f}%)")

    print("\n" + "=" * 80)
    print(f"RESULT: {checks_passed}/{checks_total} checks passed")
    if checks_passed == checks_total:
        print("✅ PHASE 3 IS PAPER-COMPLIANT!")
    elif checks_passed >= checks_total * 0.8:
        print("⚠️  Phase 3 mostly compliant (minor issues)")
    else:
        print("❌ Phase 3 needs review")
    print("=" * 80)

    plt.show()

if __name__ == "__main__":
    main()
