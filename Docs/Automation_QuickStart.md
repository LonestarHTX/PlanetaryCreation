# Automation Quick Start

## Paper Ready preset
- Open the **Tectonic Tool** tab (Editor toolbar → Planetary Creation → Tectonic Tool).
- Click **Paper Ready** to load the paper-authentic configuration (seed 42, LOD 5 render mesh, GPU Stage B, erosion/dampening enabled).
- The preset clears the STG‑04 hydraulic latch, forces a Stage B rebuild, blocks on GPU readbacks, and pre-warms neighbouring LODs.
- Watch the output log for `[PaperReady] Applied … StageBReady=true`; automation can grep for this marker before continuing.
- After the button finishes the Stage B panel should show _Ready_ and the preview mesh will refresh with amplified detail.
- The button also disables any dev-only GPU replay overrides so production runs respect the normal hash checks.

> Tip: No extra console work is required—the button takes care of `r.PlanetaryCreation.PaperDefaults`, GPU amplification, profiling CVars, and surface-process toggles.
