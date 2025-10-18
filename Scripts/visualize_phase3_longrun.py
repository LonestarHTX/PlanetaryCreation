#!/usr/bin/env python3
"""
Phase 3 Long-Run Subduction Timeseries Viewer

Loads the long-duration subduction test CSV emitted by
PlanetaryCreation.Paper.SubductionLongDuration and visualizes:
 - Max elevation over time
 - Mean elevation in rc/rs bands over time
 - Uplifted percent over time

Default input: latest Docs/Automation/Validation/Phase3/LongRun/uplift_timeseries_*.csv
Usage:
  python Scripts/visualize_phase3_longrun.py [optional_path_to_csv]
"""

import sys
import glob
from pathlib import Path
import pandas as pd
import matplotlib.pyplot as plt


def find_latest_csv() -> Path:
    project_root = Path(__file__).resolve().parent.parent
    longrun_dir = project_root / "Docs/Automation/Validation/Phase3/LongRun"
    pattern = str(longrun_dir / "uplift_timeseries_*.csv")
    matches = sorted(glob.glob(pattern))
    return Path(matches[-1]) if matches else None


def main():
    # Resolve CSV path
    if len(sys.argv) > 1:
        csv_path = Path(sys.argv[1])
        if not csv_path.exists():
            print(f"ERROR: CSV not found: {csv_path}")
            return
    else:
        csv_path = find_latest_csv()
        if not csv_path:
            print("ERROR: No long-run CSV found.")
            print("Expected at: Docs/Automation/Validation/Phase3/LongRun/uplift_timeseries_<timestamp>.csv")
            print("Run the long test first:")
            print("  Automation RunTests PlanetaryCreation.Paper.SubductionLongDuration")
            print("  -SetCVar=\"r.PaperLong.Run=1,r.PaperLong.Steps=50,r.PaperBoundary.TestPointCount=50000\"")
            return

    print("=" * 80)
    print("PHASE 3 LONG-RUN SUBDUCTION TIMESERIES")
    print("=" * 80)
    print(f"Loading: {csv_path}")

    df = pd.read_csv(csv_path)
    required_cols = {
        "step",
        "sim_time_my",
        "max_elev_m",
        "mean_elev_band_rc_m",
        "mean_elev_band_rs_m",
        "uplifted_count",
        "uplifted_percent",
    }
    missing = required_cols - set(df.columns)
    if missing:
        print(f"ERROR: Missing columns in CSV: {sorted(missing)}")
        return

    # Summary
    final = df.iloc[-1]
    print("\n--- SUMMARY ---")
    print(f"Steps: {int(final['step'])}  |  Sim time: {final['sim_time_my']:.1f} My")
    print(f"Final max elevation: {final['max_elev_m']:.3f} m")
    print(f"Final mean elev (rc band): {final['mean_elev_band_rc_m']:.3f} m")
    print(f"Final mean elev (rs band): {final['mean_elev_band_rs_m']:.3f} m")
    print(f"Final uplifted percent: {final['uplifted_percent']:.3f} %")

    # Plot
    fig, axs = plt.subplots(3, 1, figsize=(12, 12), sharex=True)

    # Max elevation
    axs[0].plot(df["sim_time_my"], df["max_elev_m"], "-o", lw=2, ms=3, color="#3366cc")
    axs[0].set_ylabel("Max Elevation (m)")
    axs[0].set_title("Max Elevation Over Time")
    axs[0].grid(True, alpha=0.3)

    # Mean elevation bands
    axs[1].plot(
        df["sim_time_my"], df["mean_elev_band_rc_m"], "-o", lw=2, ms=3, label="Mean Elev (rc)", color="#22aa99"
    )
    axs[1].plot(
        df["sim_time_my"], df["mean_elev_band_rs_m"], "-o", lw=2, ms=3, label="Mean Elev (rs)", color="#dd7744"
    )
    axs[1].set_ylabel("Mean Elevation (m)")
    axs[1].set_title("Mean Elevation in Control/Full Bands")
    axs[1].legend()
    axs[1].grid(True, alpha=0.3)

    # Uplifted percent
    axs[2].plot(
        df["sim_time_my"], df["uplifted_percent"], "-o", lw=2, ms=3, color="#aa3377"
    )
    axs[2].set_xlabel("Simulation Time (My)")
    axs[2].set_ylabel("Uplifted (%)")
    axs[2].set_title("Uplifted Fraction Over Time")
    axs[2].grid(True, alpha=0.3)

    plt.tight_layout()

    out_dir = csv_path.parent
    out_png = out_dir / "phase3_longrun_timeseries.png"
    plt.savefig(out_png, dpi=150, bbox_inches="tight")
    print(f"\n✅ Saved visualization to: {out_png}")

    plt.show()


if __name__ == "__main__":
    main()

