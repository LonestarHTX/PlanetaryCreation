"""Master script to process Stage B exemplars after patch extraction.

This script runs the complete post-processing pipeline:
1. Convert cropped GeoTIFFs to COG format
2. Convert to 16-bit PNG for UE textures
3. Update exemplar library JSON

Usage:
    python Scripts/process_stageb_exemplars.py
"""

import subprocess
import sys
from pathlib import Path


def run_script(script_name: str, description: str) -> bool:
    """Run a Python script and return success status."""
    script_path = Path(__file__).parent / script_name

    print(f"\n{'='*70}")
    print(f"Step: {description}")
    print(f"{'='*70}\n")

    result = subprocess.run([sys.executable, str(script_path)], capture_output=False)

    if result.returncode == 0:
        print(f"\n✅ {description} completed successfully")
        return True
    else:
        print(f"\n❌ {description} failed with exit code {result.returncode}")
        return False


def main():
    print("""
╔══════════════════════════════════════════════════════════════════╗
║         Stage B Exemplar Post-Processing Pipeline                ║
║         Milestone 6 Task 2.2 - Continental Amplification         ║
╚══════════════════════════════════════════════════════════════════╝
    """)

    # Step 1: Convert to COG and PNG16
    if not run_script("convert_to_cog_png16.py", "Convert to COG and PNG16"):
        print("\n⚠ Conversion failed. Check errors above.")
        sys.exit(1)

    # Step 2: Update exemplar library
    if not run_script("update_exemplar_library.py", "Update exemplar library"):
        print("\n⚠ Library update failed. Check errors above.")
        sys.exit(1)

    # Success summary
    project_root = Path(__file__).parent.parent
    print(f"""
╔══════════════════════════════════════════════════════════════════╗
║                     ✅ PROCESSING COMPLETE                       ║
╚══════════════════════════════════════════════════════════════════╝

Your Stage B exemplar data is ready!

📁 Output locations:
   • COG files:      {project_root}/Content/PlanetaryCreation/Exemplars/COG/
   • PNG16 files:    {project_root}/Content/PlanetaryCreation/Exemplars/PNG16/
   • Exemplar JSON:  {project_root}/Content/PlanetaryCreation/Exemplars/ExemplarLibrary.json
   • Source manifest: {project_root}/StageB_SRTM90/metadata/stageb_manifest.json

📜 License & attribution:
   • See Docs/Licenses/SRTM.txt for citation requirements

🎯 Next steps:
   1. Commit the exemplar library JSON and license files to git
   2. Consider adding COG/PNG16 files to .gitignore (they're large)
   3. Implement Stage B continental amplification in C++ (Milestone 6 Task 2.2)
   4. Wire up exemplar loading in TectonicSimulationService

Happy terrain synthesis! 🏔️
    """)


if __name__ == "__main__":
    main()
