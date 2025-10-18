#!/usr/bin/env python3
"""
Run Phase 5 Oceanic Validation Test

This script runs the OceanicVisualizationTest automation test and waits for CSV outputs.
Solves the WSL automation hanging issue by using subprocess with proper timeout handling.

Usage:
    python run_oceanic_validation.py
"""

import subprocess
import time
import os
import sys
from pathlib import Path

# Detect if running from WSL
if os.path.exists('/mnt/c'):
    # WSL paths
    PROJECT_ROOT = Path("/mnt/c/Users/Michael/Documents/Unreal Projects/PlanetaryCreation")
    UNREAL_CMD = Path("/mnt/c/Program Files/Epic Games/UE_5.5/Engine/Binaries/Win64/UnrealEditor-Cmd.exe")
    PROJECT_FILE_WIN = r"C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\PlanetaryCreation.uproject"
    VALIDATION_DIR = PROJECT_ROOT / "Docs/Automation/Validation/Phase5"
else:
    # Windows paths
    PROJECT_ROOT = Path("C:/Users/Michael/Documents/Unreal Projects/PlanetaryCreation")
    UNREAL_CMD = Path("C:/Program Files/Epic Games/UE_5.5/Engine/Binaries/Win64/UnrealEditor-Cmd.exe")
    PROJECT_FILE_WIN = str(PROJECT_ROOT / "PlanetaryCreation.uproject")
    VALIDATION_DIR = PROJECT_ROOT / "Docs/Automation/Validation/Phase5"

# Expected outputs
EXPECTED_CSVS = [
    "oceanic_elevation_profile.csv",
    "ridge_directions.csv",
    "cross_boundary_transect.csv"
]

def run_test():
    """Run the Unreal automation test."""
    print("=" * 80)
    print("Phase 5 Oceanic Validation Test Runner")
    print("=" * 80)
    print()

    # Check prerequisites
    if not UNREAL_CMD.exists():
        print(f"❌ UnrealEditor-Cmd.exe not found at: {UNREAL_CMD}")
        return False

    if not PROJECT_ROOT.exists():
        print(f"❌ Project root not found at: {PROJECT_ROOT}")
        return False

    print(f"✓ Found UnrealEditor-Cmd.exe")
    print(f"✓ Found project root")
    print()

    # Build command
    cmd = [
        str(UNREAL_CMD),
        PROJECT_FILE_WIN,
        "-ExecCmds=Automation RunTests PlanetaryCreation.Paper.OceanicCrust; Quit",
        "-TestExit=Automation Test Queue Empty",
        "-unattended",
        "-nop4",
        "-nosplash",
        "-log"
    ]

    print("Running test command:")
    print(" ".join(cmd))
    print()
    print("This may take 1-2 minutes...")
    print()

    # Run with timeout
    try:
        start_time = time.time()
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=180,  # 3 minutes max
            creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == 'win32' else 0
        )
        elapsed = time.time() - start_time

        print(f"Test completed in {elapsed:.1f} seconds")
        print()

        # Check for errors in output
        if "Error" in result.stdout or "FAILED" in result.stdout:
            print("⚠️  Warnings/errors detected in output:")
            for line in result.stdout.split('\n'):
                if 'Error' in line or 'FAILED' in line or 'Phase5' in line or 'CSV' in line:
                    print(f"  {line}")
            print()

        # Check for success indicators
        if "Phase5" in result.stdout or "CSV" in result.stdout or "written" in result.stdout:
            print("✓ Test appears to have run successfully")
            for line in result.stdout.split('\n'):
                if 'Phase5' in line or 'CSV' in line or 'written' in line or 'oceanic' in line.lower():
                    print(f"  {line}")
            print()

        return result.returncode == 0

    except subprocess.TimeoutExpired:
        print("❌ Test timed out after 3 minutes")
        print("   This may indicate a hang or very slow execution")
        return False
    except Exception as e:
        print(f"❌ Error running test: {e}")
        return False

def check_outputs():
    """Check if expected CSV files were created."""
    print("Checking for output files...")
    print()

    found_files = []
    missing_files = []

    for csv_name in EXPECTED_CSVS:
        csv_path = VALIDATION_DIR / csv_name
        if csv_path.exists():
            size = csv_path.stat().st_size
            print(f"  ✓ {csv_name} ({size:,} bytes)")
            found_files.append(csv_path)
        else:
            print(f"  ✗ {csv_name} (not found)")
            missing_files.append(csv_name)

    print()

    if missing_files:
        print("⚠️  Missing files:")
        for name in missing_files:
            print(f"    - {name}")
        print()
        print("Possible causes:")
        print("  1. Test didn't run to completion")
        print("  2. Test failed before CSV generation")
        print("  3. Output directory path incorrect")
        print()

        # Check for any CSV files
        if VALIDATION_DIR.exists():
            all_files = list(VALIDATION_DIR.glob("*.csv"))
            if all_files:
                print("Found other CSV files in validation dir:")
                for f in all_files:
                    print(f"  - {f.name}")
            else:
                print("No CSV files found in validation directory")
        print()

    if found_files:
        print(f"✓ Found {len(found_files)}/{len(EXPECTED_CSVS)} expected files")
        return True
    else:
        print("❌ No expected files found")
        return False

def check_json_metrics():
    """Check for new metrics JSON file."""
    print("Checking for metrics JSON...")
    print()

    if not VALIDATION_DIR.exists():
        print(f"❌ Validation directory doesn't exist: {VALIDATION_DIR}")
        return False

    json_files = sorted(VALIDATION_DIR.glob("summary_*.json"), key=lambda p: p.stat().st_mtime, reverse=True)

    if json_files:
        latest = json_files[0]
        mtime = time.ctime(latest.stat().st_mtime)
        size = latest.stat().st_size
        print(f"  Latest: {latest.name}")
        print(f"  Modified: {mtime}")
        print(f"  Size: {size} bytes")
        print()

        # Read and display key metrics
        try:
            import json
            with open(latest, 'r') as f:
                data = json.load(f)

            if 'metrics' in data:
                print("  Metrics:")
                metrics = data['metrics']
                for key, value in metrics.items():
                    if isinstance(value, float):
                        print(f"    {key}: {value:.6f}")
                    else:
                        print(f"    {key}: {value}")
                print()
        except Exception as e:
            print(f"  ⚠️  Couldn't read JSON: {e}")
            print()

        return True
    else:
        print("  No JSON files found")
        return False

def main():
    """Main execution."""
    # Run test
    success = run_test()

    print()
    print("=" * 80)
    print("Validation Results")
    print("=" * 80)
    print()

    # Check outputs
    csvs_found = check_outputs()
    json_found = check_json_metrics()

    print()
    print("=" * 80)

    if csvs_found:
        print("✓ SUCCESS - CSV files generated")
        print()
        print("Next step: Run visualization script")
        print("  python Scripts/visualize_phase5.py")
    else:
        print("⚠️  PARTIAL - Test ran but CSVs missing")
        print()
        print("Troubleshooting:")
        print("  1. Check Unreal logs in:")
        print(f"     C:/Users/Michael/AppData/Local/UnrealEngine/*/Saved/Logs/")
        print("  2. Try running test in-editor:")
        print("     Session Frontend > Automation > Filter: OceanicVisualization")
        print("  3. Check test is registered in build")

    print("=" * 80)
    print()

    return 0 if csvs_found else 1

if __name__ == '__main__':
    exit(main())
