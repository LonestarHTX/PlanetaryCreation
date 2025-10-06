# Milestone 5 Plan – Production Readiness & Surface Weathering

**Status:** ✅ Completed (2025-10-04)
**Timeline:** 4 weeks (Weeks 13-16)
**Dependencies:** Milestone 4 complete ✅

---

## Mission

Transform the tectonic simulation from a research-aligned prototype into a **production-ready editor tool** with polished UX, comprehensive validation, and complete Section 4 paper parity (adding erosion/weathering). Deliver a shippable product that researchers and artists can use for real-world planetary generation.

---

## Outcome Summary

- Continuous playback, orbital camera, timeline scrubbing, and undo/redo UI are live in `PlanetaryCreationEditor` (see `FTectonicPlaybackController`, `SPTectonicToolPanel`).
- Surface weathering pass is in production: continental erosion, sediment transport, and oceanic dampening run every simulation step with tunable parameters.
- Automation suite expanded to cover playback, erosion, dampening, and regression scenarios (17/18 green; `RetessellationRegression` perf overage documented for M6 follow-up).
- Profiling captured with Unreal Insights shows 6.32 ms per step at L3, leaving ~17× budget headroom and no regressions against M4 (`Docs/Performance_M5.md`).
- User-facing documentation, presets, and release artifacts updated to reflect the production-ready tooling.

---

## Goals

1. **Production UX:** Continuous playback, orbital camera, undo/redo UI, parameter presets
2. **Validation & QA:** Expanded automation, performance profiling with Unreal Insights, stress testing
3. **Surface Weathering:** Deliver Section 4.5 foundations (continental erosion, oceanic dampening, sediment diffusion stage 0)
4. **Documentation:** User guide, API reference, troubleshooting docs
5. **Packaging:** Release-ready builds with CI/CD integration

---

## Phase Breakdown *(completed; details retained for reference)*

### Phase 1 – Editor UX Enhancements *(Weeks 13–14)*

**Goal:** Provide intuitive controls for simulation authoring and review workflows.

**Week 13:** Tasks 1.1 (Continuous Playback) + 1.3 (Undo/Redo UI)
**Week 14:** Task 1.2 (Orbital Inspection Camera)

#### Task 1.1: Continuous Playback System
**Owner:** Tools Engineer
**Deliverables:**
- Play/Pause/Stop controls in `SPTectonicToolPanel`
- Adjustable playback speed (0.5×, 1×, 2×, 5×, 10× speed)
- Step rate selector (1 step/sec → 10 steps/sec)
- Timeline scrubber with current time display (in millions of years)
- Progress bar showing simulation history (0 My → current My)

**Technical Notes:**
- Use `FTSTicker` for frame-based step cadence
- Integrate with existing rollback system for timeline scrubbing
- Pause on topology change events (splits/merges/retess) for review
- Keyboard shortcuts: Space (Play/Pause), Left/Right arrows (scrub)

**Validation:**
- ✅ Automation test: `ContinuousPlaybackTest`
  - Verify playback executes deterministic steps
  - Test pause/resume preserves state
  - Validate scrubber syncs with simulation time
- ✅ Manual check: 100-step playback at 5× speed completes smoothly
- ✅ Performance: <1ms overhead per frame when paused

**Acceptance:**
- User can press Play and watch simulation evolve continuously
- Timeline scrubber allows jumping to any previous step
- Playback respects determinism (same results as manual stepping)

---

#### Task 1.2: Orbital Inspection Camera
**Owner:** Tools Engineer, Rendering Engineer
**Deliverables:**
- Orbit controls: Left-drag to rotate, right-drag to pan, scroll to zoom
- Smooth camera interpolation (ease-in/ease-out)
- Focus controls: Double-click plate to center view
- Distance presets: Global (10× radius), Regional (3× radius), Surface (1.2× radius)
- Camera state persistence (saved per project)

**Technical Notes:**
- Implement as `FTectonicOrbitCamera` helper class
- Use `FMath::FInterpTo` for smooth transitions
- Store camera state in `UTectonicSimulationService` transient properties
- Collision detection with sphere surface (prevent clipping through terrain)

**Validation:**
- ✅ Automation test: `OrbitCameraTest`
  - Verify zoom limits (min/max distance)
  - Test focus transitions (smooth interpolation)
  - Validate state persistence across editor restarts
- ✅ Manual check: Artist can navigate planet smoothly without frustration
- ✅ Performance: Camera update <0.5ms per frame

**Acceptance:**
- Intuitive mouse/keyboard controls for 3D navigation
- Smooth transitions between view distances
- No clipping through planet surface
- Camera state persists across editor sessions

---

#### Task 1.3: Undo/Redo UI Integration
**Owner:** Tools Engineer
**Deliverables:**
- Toolbar buttons: Undo/Redo with step count badges
- Keyboard shortcuts: `Ctrl+Z` (Undo), `Ctrl+Shift+Z` / `Ctrl+Y` (Redo)
- History panel showing last 20 steps with timestamps
- Visual indicator of current position in history
- Hotkey for "Undo to event" (jump to last split/merge/retess)

**Technical Notes:**
- Wire to existing `FTectonicSimulationController::RollbackToSnapshot()`
- Display plate count changes in history ("Step 42: 8 plates → 9 plates [Split]")
- Integrate with continuous playback (pause on undo/redo)
- Grey out Undo/Redo buttons when at history limits

**Validation:**
- ✅ Automation test: `UndoRedoUITest`
  - Verify button states update correctly
  - Test history panel shows accurate step data
  - Validate keyboard shortcuts trigger rollback
- ✅ Manual check: Undo 10 steps, modify params, redo 5 steps works correctly
- ✅ Performance: History panel updates <1ms

**Acceptance:**
- Standard undo/redo UX matches Unreal Editor conventions
- History panel provides clear context for each step
- Hotkeys work reliably without conflicts

> **Note:** Parameter preset library is deferred to Phase 4 (stretch goal) to keep Weeks 13–14 focused on core UX controls.

---

### Phase 2 – Surface Weathering Foundations *(Week 15)*

**Goal:** Deliver the first production-ready pass on paper Section 4.5 (continental erosion & oceanic dampening). Sediment transport is scoped as a diffusion-based stage zero; full hydraulic routing will be completed in Milestone 6 alongside Stage B amplification.

#### Task 2.1: Continental Erosion Model
**Owner:** Simulation Engineer
**Paper Reference:** Section 4.5 (Continental Erosion)
**Deliverables:**
- Erosion rate formula: `dh/dt = -k_erosion × |∇h| × (h - sea_level)⁺`
- Thermal weathering: Temperature-dependent rock breakdown
- Elevation smoothing over geological timescales

**Technical Notes:**
- Erosion constant: `k_erosion = 0.001 m/My` (tunable per paper)
- Apply per-vertex elevation reduction during `AdvanceSteps()`
- Couple to stress field: High-stress regions (mountains) erode faster
- Couple to thermal field: Hot regions weather faster (tropical climates)
- Store per-vertex erosion history for visualization

**Mathematical Details:**
```cpp
// Continental erosion formula (paper Section 4.5)
double ErosionRate = ErosionConstant * Slope * FMath::Max(0.0, Elevation - SeaLevel);
double ThermalFactor = 1.0 + 0.5 * (Temperature / MaxTemperature); // 1.0-1.5× multiplier
double StressFactor = 1.0 + 0.3 * (Stress / MaxStress); // 1.0-1.3× multiplier
double TotalErosion = ErosionRate * ThermalFactor * StressFactor * DeltaTime_My;
NewElevation = FMath::Max(SeaLevel, CurrentElevation - TotalErosion);
```

**Validation:**
- ✅ Automation test: `ContinentalErosionTest`
  - High mountains erode faster than low hills
  - Erosion rate proportional to slope
  - Sea-level terrain unaffected (no underwater erosion)
  - Deterministic: Same seed produces same erosion patterns
- ✅ Manual check: Run 1000 steps, verify mountain ranges smoothed
- ✅ CSV export: Add `ErosionRate_m_per_My`, `CumulativeErosion_m` columns
- ✅ Performance: <5ms per step (vectorized implementation)

**Acceptance:**
- Mountain ranges show realistic weathering over geological timescales
- High-elevation regions visibly smoothed after 100+ My
- Erosion CSV data available for analysis

---

#### Task 2.2: Sediment Transport (Stage 0 Diffusion)
**Owner:** Simulation Engineer
**Paper Reference:** Section 4.5 (sediment filling in trenches)
**Deliverables:**
- Diffusion-based sediment redistribution that conserves mass and reduces extreme erosion artifacts.
- Per-vertex `SedimentThickness_m` accumulation and `SedimentSource`/`SedimentSink` diagnostics.
- Overlay visualisation toggles highlighting erosion vs. deposition bands.

**Technical Notes:**
- Use existing adjacency lists for a single-ring neighbour pass; weight transfer by slope and inverse distance.
- Clamp per-iteration transfer to avoid oscillation; record global erosion/deposition sums to ensure mass parity.
- Tag convergent boundaries as preferred sinks (bonus weight) without requiring full hydraulic pathfinding.
- Document follow-up work for Milestone 6:
  * Downhill hydraulic routing (Dijkstra shortest-path or flow accumulation)
  * Basin-aware sediment accumulation
  * Dynamic erosion-deposition coupling with Stage B amplified terrain

**Validation:**
- ✅ Automation test: `SedimentTransportTest`
  - Mass conservation within 1% tolerance.
  - Deposition concentrates in low-elevation / convergent zones.
  - Deterministic behaviour per seed.
- ✅ Manual check: After 200+ steps, foothills display deposition bands; ocean trenches gain measurable sediment.
- ✅ CSV export: `SedimentThickness_m`, `SedimentSource`, `SedimentSink` columns populated.
- ✅ Performance: <4 ms per step (vectorised diffusion loop).

**Acceptance:**
- Diffusion pass visibly softens eroded terrain and highlights sediment sinks.
- Mass balance maintained; metrics ready for future hydraulic upgrade.
- Clear TODO logged for Milestone 6 hydraulic transport expansion.

---

#### Task 2.3: Oceanic Dampening
**Owner:** Simulation Engineer
**Paper Reference:** Section 4.5 (Oceanic Dampening)
**Deliverables:**
- Oceanic elevation smoothing: Reduce seafloor roughness over time
- Isostatic rebound: Adjust elevations to maintain lithospheric equilibrium
- Age-dependent subsidence: Older oceanic crust sits deeper

**Technical Notes:**
- Apply Gaussian smoothing to vertices below sea level
- Dampening rate: `k_dampen = 0.0005 m/My` (slower than erosion)
- Age-subsidence formula: `h_ocean = -2500 - 350 × sqrt(age_My)` (simplified)
- Couple to thermal field: Cooler crust subsides faster

**Mathematical Details:**
```cpp
// Oceanic dampening (Gaussian smoothing for seafloor)
if (Elevation < SeaLevel) {
    double SmoothedElevation = 0.0;
    double WeightSum = 0.0;
    for (int32 NeighborIdx : Neighbors) {
        double Distance = GetGeodesicDistance(VertexIdx, NeighborIdx);
        double Weight = FMath::Exp(-Distance * Distance / (2.0 * SmoothingRadius * SmoothingRadius));
        SmoothedElevation += Elevation[NeighborIdx] * Weight;
        WeightSum += Weight;
    }
    double DampenedElevation = SmoothedElevation / WeightSum;
    double DampenRate = 0.0005; // m/My
    NewElevation = FMath::Lerp(Elevation, DampenedElevation, DampenRate * DeltaTime_My);
}
```

**Validation:**
- ✅ Automation test: `OceanicDampeningTest`
  - Seafloor roughness decreases over time
  - Age-depth relationship matches empirical formula (within 10%)
  - Dampening only affects oceanic crust (elevation < sea level)
  - Deterministic behavior
- ✅ Manual check: Ocean trenches smooth over 1000 My
- ✅ Performance: <3ms per step

**Acceptance:**
- Oceanic crust shows realistic age-depth profile
- Seafloor smoother than continental terrain
- CSV export includes `CrustAge_My`, `SubsidenceDepth_m`

---

### Phase 3 – Validation & Performance Profiling *(Week 15)*

**Goal:** Establish production-grade quality assurance with comprehensive testing and performance baselines.

#### Task 3.1: Expanded Automation Suite
**Owner:** QA Engineer, Tools Engineer
**Deliverables:**
- 10 new tests covering M5 features (playback, camera, erosion, sediment)
- Regression suite: Verify M4 features still work after M5 changes
- Long-duration stress tests: 1000-step simulations at multiple LOD levels
- Determinism validation: Cross-platform reproducibility (Windows/Linux)

**New Tests:**
1. `ContinuousPlaybackTest` - Playback controls, timeline scrubbing
2. `OrbitCameraTest` - Camera navigation, focus transitions
3. `UndoRedoUITest` - History panel, keyboard shortcuts
4. `ContinentalErosionTest` - Erosion rates, elevation smoothing
5. `SedimentTransportTest` - Mass conservation, downhill flow
6. `OceanicDampeningTest` - Seafloor smoothing, age-depth profiles
7. `LongDurationStressTest` - 1000-step simulations without crashes
8. `CrossPlatformDeterminismTest` - Windows vs Linux identical results
9. `PerformanceRegressionTest` - M5 overhead vs M4 baseline
10. *(Optional)* `PresetSystemTest` - Preset loading, custom save/load (Task 4.4 stretch goal)

**Validation:**
- ✅ All 27 tests pass (18 from M4 + 9 new)
- ✅ 1000-step stress test completes without memory leaks
- ✅ Cross-platform determinism: Identical CSV exports on Windows/Linux
- ✅ Performance regression: M5 features add <10ms per step
- ✅ CI integration: Tests run on every commit

**Acceptance:**
- Test suite covers all major features (M0-M5)
- Long-duration simulations stable (no crashes, no leaks)
- Determinism guaranteed across platforms
- CI pipeline catches regressions automatically

---

#### Task 3.2: Unreal Insights Profiling
**Owner:** Performance Engineer
**Deliverables:**
- Deep profiling session using Unreal Insights
- CPU hot-path analysis: Identify top 10 time sinks
- GPU bottleneck analysis: Mesh update costs
- Memory profiling: Heap usage, allocation patterns
- Frame time breakdown: Per-subsystem timings

**Profiling Targets:**
- Level 3 (ship-critical): <100ms per step (stretch goal from 101.94ms)
- Level 5 (high-detail): <120ms per step (maintain current 119.40ms)
- Level 6 (ultra-detail): Document baseline for M6 optimization (currently 171ms)
- Memory footprint: <5 MB total (including M5 features)

**Deliverables:**
- `Docs/Performance_M5.md` with Unreal Insights captures
- Hot-path analysis: Top 10 functions by CPU time
- Optimization roadmap: Quick wins (<1 day) vs deferred (M6)
- Memory budget breakdown: Simulation, LOD cache, UI state

**Validation:**
- ✅ Insights session captures 1000-step run at Level 5
- ✅ Hot-path report identifies optimization opportunities
- ✅ Memory profiling shows no leaks over long runs
- ✅ Performance targets documented for M6

**Acceptance:**
- Comprehensive performance baseline established
- Optimization roadmap prioritized by impact vs effort
- Memory budget allocated per subsystem
- Insights methodology documented for future milestones

---

#### Task 3.3: Stress Testing & Edge Cases
**Owner:** QA Engineer
**Deliverables:**
- Extreme parameter testing: 100 plates, 10 km/My velocities, 1° retess threshold
- Boundary condition validation: Single plate, all-ocean planet, all-continental planet
- Topology event storms: Trigger 10 splits in 10 steps, rapid plate growth/shrinkage
- Memory stress: Run 5000 steps with full history retained
- Error recovery: Graceful handling of invalid states

**Test Scenarios:**
1. **Extreme Plate Count:** 100 plates, verify O(N²) algorithms don't explode
2. **High Velocity:** 10 km/My, verify stability of velocity field
3. **Rapid Retessellation:** 1° threshold, verify performance under frequent rebuilds
4. **Single Plate Planet:** 1 plate, verify no divide-by-zero errors
5. **All-Ocean Planet:** No continental crust, verify erosion skips gracefully
6. **All-Continental Planet:** No oceanic crust, verify subduction still works
7. **Topology Event Storm:** Force 10 splits in 10 steps, verify no crashes
8. **Memory Stress:** 5000 steps with full rollback history (stress heap allocator)

**Validation:**
- ✅ All edge cases handled gracefully (no crashes)
- ✅ Error messages informative ("Cannot split single-plate planet")
- ✅ Performance degrades predictably under stress (no sudden spikes)
- ✅ Memory usage capped at 200 MB even with 5000-step history

**Acceptance:**
- Tool handles extreme parameters without crashing
- Edge cases documented in user guide ("Known Limitations")
- Error recovery mechanisms robust
- Performance degradation curve documented

---

### Phase 4 – Documentation & Packaging *(Week 16)*

**Goal:** Deliver production-ready documentation and release artifacts. *(Stretch:* implement parameter preset library if time remains.)

#### Task 4.1: User Guide
**Owner:** Technical Writer (or Tools Engineer)
**Deliverables:**
- `Docs/UserGuide.md` (30-50 pages, expandable post-release)
  - **Getting Started:** Opening the tool, running first simulation
  - **UI Reference:** Toolbar buttons, keyboard shortcuts, panel layout
  - **Parameter Guide:** Explanation of each simulation parameter with sensible ranges
  - **Workflow Examples:** 5 step-by-step tutorials (Earth-like, high activity, etc.)
  - **CSV Export Reference:** Column definitions, units, data interpretation
  - **Troubleshooting:** Common issues, error messages, performance tuning
  - **Known Limitations:** Feature gaps, deferred work, paper deviations

**Sections:**
1. **Introduction:** Project goals, paper alignment, intended audience
2. **Installation:** UE 5.5.4 setup, RealtimeMeshComponent plugin, module rebuild
3. **Quick Start:** Load preset, press Play, observe 100-step evolution
4. **UI Reference:** Toolbar, panels, overlays, keyboard shortcuts
5. **Parameter Reference:** Plate count, velocities, retess threshold, erosion rates
6. **CSV Export Guide:** Column definitions, units, example analysis workflows
7. **Troubleshooting:** Performance issues, editor crashes, test failures
8. **Advanced Topics:** Custom presets, deterministic scripting, automation testing
9. **Known Limitations:** LOD Level 6 performance, terrane handling (deferred M6), Stage B amplification (deferred M6)
10. **Roadmap:** M6 features (terranes, Stage B), M7 polish, M8 climate coupling

**Validation:**
- ✅ External reviewer (non-developer) completes Quick Start tutorial successfully
- ✅ All parameter ranges validated against code defaults
- ✅ CSV column definitions match actual export format
- ✅ Troubleshooting section covers 90% of known issues

**Acceptance:**
- New user can run simulation without external help
- Parameter guide enables informed experimentation
- CSV export reference sufficient for data analysis
- Troubleshooting section reduces support burden

---

#### Task 4.2: API Reference
**Owner:** Tools Engineer
**Deliverables:**
- `Docs/APIReference.md` documenting public interfaces
  - `UTectonicSimulationService` API (initialization, stepping, rollback, export)
  - `FTectonicSimulationController` API (mesh updates, UI integration)
  - `SPTectonicToolPanel` API (UI customization, event hooks)
  - Example code snippets for common tasks

**Public API Surface:**
```cpp
// UTectonicSimulationService - Core simulation state
class UTectonicSimulationService : public UEditorSubsystem {
public:
    // Initialization
    void InitializeSimulation(int32 Seed, int32 PlateCount, double InitialVelocity_km_My);
    void ResetSimulation();

    // Stepping
    void AdvanceSteps(int32 NumSteps);
    bool RollbackToStep(int32 TargetStep);

    // State queries
    int32 GetCurrentStepCount() const;
    double GetSimulationTime_My() const;
    TArray<FTectonicPlate> GetPlates() const;

    // Export
    bool ExportToCSV(const FString& FilePath);
    FTectonicSnapshot CaptureSnapshot() const;
};

// FTectonicSimulationController - Mesh visualization
class FTectonicSimulationController {
public:
    // Mesh updates
    void UpdateMeshFromSimulation();
    void SetLODLevel(int32 Level);
    void ToggleOverlay(ETectonicOverlay OverlayType, bool bEnabled);

    // Camera control
    void SetCameraTarget(FVector WorldPosition);
    void SetCameraDistance(double Distance_km);
};
```

**Validation:**
- ✅ All public APIs documented with parameter descriptions
- ✅ Example code snippets compile and run
- ✅ API reference matches actual header files (no drift)

**Acceptance:**
- Advanced users can extend tool via documented APIs
- Example snippets demonstrate common integration patterns
- API reference stays in sync with code (via CI checks)

---

#### Task 4.3: Release Packaging
**Owner:** Build Engineer, Tools Engineer
**Deliverables:**
- Release build configuration (`Shipping` profile)
- Plugin packaging: Bundle RealtimeMeshComponent with project
- CI/CD pipeline: Automated builds on commit
- Release artifacts:
  - `PlanetaryCreation_M5.zip` (standalone UE project)
  - `PlanetaryCreation_M5_Editor.exe` (Windows binary, if applicable)
  - `Docs/ReleaseNotes_M5.md`
  - `Docs/UserGuide.md`
  - `Docs/APIReference.md`

**CI/CD Steps:**
1. Clone repository
2. Build `PlanetaryCreationEditor` module (Development config)
3. Run automation tests (`UnrealEditor-Cmd.exe ... -ExecCmds="Automation RunTests PlanetaryCreation"`)
4. Generate API docs (Doxygen or similar)
5. Package artifacts into ZIP
6. Upload to release repository

**Validation:**
- ✅ CI pipeline builds on every commit
- ✅ Test suite runs automatically; failures block merge
- ✅ Release artifacts download and extract correctly
- ✅ Standalone project opens in UE 5.5.4 without errors

**Acceptance:**
- One-click build and test via CI
- Release artifacts ready for distribution
- Zero-config setup for end users (plugin bundled)
- Automated testing prevents regressions

---

#### Task 4.4: Parameter Presets (Stretch Goal)
**Owner:** Tools Engineer
**Status:** Optional - only if Weeks 13-15 finish ahead of schedule
**Deliverables:**
- Preset dropdown in `SPTectonicToolPanel`: "Earth-like", "High Activity", "Stable Cratons", "Custom"
- 5 curated scenario configurations with descriptions:
  * `EarthLike`: 12 plates, moderate velocity (1.5 km/My), realistic continental/oceanic ratios
  * `HighActivity`: 20 plates, high velocity (3.0 km/My), frequent splits/merges
  * `StableCratons`: 6 large plates, low velocity (0.8 km/My), minimal rifting
  * `SupercontinentCycle`: 8 plates, high convergence, staged breakup simulation
  * `Custom`: User-defined parameters (default)
- Save/load custom presets to JSON (`Content/PlanetaryCreation/Presets/`)
- Auto-generate preset descriptions from parameter values

**Technical Notes:**
- Implement `FTectonicPreset` struct with full parameter set (seed, plate count, velocities, thresholds)
- Store presets in `UDataAsset` subclass `UTectonicPresetLibrary`
- "Reset to Preset" button to restore original preset values after manual tweaking
- Preset dropdown updates when custom preset files added to directory

**Validation:**
- ✅ Automation test: `PresetSystemTest` (optional - only if feature implemented)
  - Verify preset loading restores correct parameters
  - Test custom preset save/load roundtrip
  - Validate scenario descriptions match parameter values
- ✅ Manual check: Load "Earth-like" preset, run 100 steps, results match expectations
- ✅ Performance: Preset load <10ms

**Acceptance:**
- New users can start with "Earth-like" preset and get sensible results
- Researchers can save/share custom parameter combinations
- Preset system is extensible for future scenarios

---

## Paper Alignment

### Implemented in M5 (Section 4.5 Foundations)
- ✅ **Section 4.5:** Continental erosion (slope/stress/thermal coupled)
- ✅ **Section 4.5:** Sediment transport stage 0 (mass-conserving diffusion, trench deposition)
- ✅ **Section 4.5:** Oceanic dampening (age-subsidence, seafloor smoothing)

### Section 4 Parity – Core Behaviour ✅
With M5, the **core mechanics of Section 4** from the paper are implemented; hydraulic sediment routing refinements continue in Milestone 6 alongside Stage B amplification:
- ✅ 4.1: Subduction (M3/M4)
- ✅ 4.2: Continental collision (M4)
- ✅ 4.3: Oceanic crust generation (M4)
- ✅ 4.4: Plate rifting (M4)
- ✅ 4.5: Continental erosion & oceanic dampening (M5) ← **NEW**

### Deferred to M6 (Section 5: Amplification)
- ⚠️ **Stage A:** Adaptive meshing for high-resolution amplification (partial LOD system in M4)
- ❌ **Stage B:** Procedural noise + exemplar blending for ~100 m relief
- ❌ **Terrane handling:** Extraction/reattachment at rifts/subduction zones

---

## Acceptance Criteria

### UX & Workflow ✅
- [ ] User can press Play and watch simulation evolve continuously
- [ ] Timeline scrubber allows jumping to any previous step
- [ ] Orbital camera enables intuitive 3D navigation
- [ ] Undo/Redo UI matches Unreal Editor conventions
- [ ] Parameter presets documented as Phase 4 stretch goal (optional)

### Surface Weathering ✅
- [ ] Mountain ranges show realistic erosion over 100+ My
- [ ] Sediment accumulates in ocean trenches and basins
- [ ] Mass conservation: Total erosion = total deposition (within 1%)
- [ ] Oceanic crust shows age-depth relationship per empirical formula

### Validation ✅
- [ ] 27 automation tests pass (18 M4 + 9 M5)
- [ ] 1000-step stress test completes without crashes
- [ ] Cross-platform determinism: Windows/Linux produce identical results
- [ ] Performance targets met: L3 ≤110 ms, L5 <120 ms

### Documentation ✅
- [ ] User guide enables new users to run simulations independently
- [ ] API reference documents all public interfaces
- [ ] CSV export reference explains all data columns
- [ ] Troubleshooting guide covers common issues

### Packaging ✅
- [ ] CI/CD pipeline builds and tests on every commit
- [ ] Release artifacts ready for distribution
- [ ] Standalone project opens in UE 5.5.4 without errors

---

## Performance Targets

### Step Time Budget (per step)
| LOD   | Vertices | M4 Baseline | M5 Target | Status |
|-------|----------|-------------|-----------|--------|
| L3 ✅ | 642      | 101.94 ms   | ≤110 ms   | Primary goal |
| L5 ✅ | 10,242   | 119.40 ms   | <120 ms   | Maintain |
| L6 ⚠️  | 40,962   | 171.00 ms   | <180 ms   | Document baseline |

### M5 Feature Overhead
| Feature                  | Target      | Notes |
|--------------------------|-------------|-------|
| Continuous playback      | <1 ms/frame | Frame ticker overhead |
| Orbital camera           | <0.5 ms     | Camera update |
| Undo/Redo UI             | <1 ms       | History panel refresh |
| Continental erosion      | <5 ms       | Per-vertex elevation update |
| Sediment diffusion       | <4 ms       | Single-ring diffusion pass |
| Oceanic dampening        | <3 ms       | Gaussian smoothing |
| **Total M5 Overhead**    | **<14 ms**  | Keeps L3 well under 110 ms target |

### Memory Budget
| Component                | M4 Baseline | M5 Target | Notes |
|--------------------------|-------------|-----------|-------|
| Simulation state         | 1.2 MB      | 1.6 MB    | +erosion history buffers |
| LOD cache                | 0.75 MB     | 0.75 MB   | No change |
| Undo history (20 steps)  | 0 MB        | 0.5 MB    | New UI feature |
| Camera state             | 0 MB        | 0.01 MB   | Negligible |
| **Total Memory**         | **1.95 MB** | **≤2.9 MB** | Limit increase to <1 MB total growth |

---

## Risk Register

### High Priority
1. **Performance Regression:** M5 features add ~14 ms overhead → May push L3 above 110 ms target
   - **Mitigation:** Profile early, gate optional UI niceties behind feature flags, vectorise erosion/diffusion loops
   - **Contingency:** Ship with erosion diff pass only; move oceanic dampening tweaks to Milestone 6 if needed

2. **Erosion Model Complexity:** Stage 0 diffusion may be too subtle/too strong without hydraulic routing
   - **Mitigation:** Expose tuning constants, add debug overlays to calibrate visually
   - **Contingency:** Cap erosion/diffusion strength and document need for Stage 1 upgrade in Milestone 6

### Medium Priority
3. **Cross-Platform Determinism:** Floating-point differences between Windows/Linux
   - **Mitigation:** Use double-precision math, avoid platform-specific libraries
   - **Contingency:** Document platform-specific tolerances, require per-platform validation

4. **CI/CD Integration:** Automation tests may flake on build servers
   - **Mitigation:** Run tests 3× before declaring failure, isolate timing-sensitive tests
   - **Contingency:** Manual testing for release builds if CI unreliable

### Low Priority
5. **User Guide Scope Creep:** 50+ page documentation takes longer than planned
   - **Mitigation:** Focus on Quick Start + Parameter Reference first, defer advanced topics
   - **Contingency:** Publish iterative docs (v1.0 with core sections, v1.1 with advanced topics)

---

## Dependencies

### External
- **UE 5.5.4:** Editor automation framework, Unreal Insights profiling
- **RealtimeMeshComponent v5:** Mesh update APIs (no changes expected)
- **CI/CD Infrastructure:** Build server, artifact repository (configure if not available)

### Internal (Milestone 4 Complete ✅)
- ✅ Dynamic re-tessellation engine (rollback foundation for undo/redo)
- ✅ LOD system (performance baseline for M5 profiling)
- ✅ CSV export v3.0 (extend with erosion columns)
- ✅ Automation framework (expand with M5 tests)

---

## Deliverables Checklist

### Code
- [ ] Continuous playback system (`FTectonicPlaybackController`)
- [ ] Orbital camera (`FTectonicOrbitCamera`)
- [ ] Undo/Redo UI integration (`SPTectonicHistoryPanel`)
- [ ] Continental erosion (`ApplyContinentalErosion()`)
- [ ] Sediment diffusion stage 0 (`ApplySedimentDiffusion()`)
- [ ] Oceanic dampening (`ApplyOceanicDampening()`)
- [ ] *(Stretch)* Parameter preset system (`UTectonicPresetLibrary`) – optional Phase 4 scope

### Tests
- [ ] 9 new automation tests (playback, camera, erosion, sediment, stress) + 1 optional (presets)
- [ ] Regression suite (verify M4 tests still pass)
- [ ] Long-duration stress test (1000 steps)
- [ ] Cross-platform determinism test

### Documentation
- [ ] `Docs/UserGuide.md` (30-50 pages)
- [ ] `Docs/APIReference.md`
- [ ] `Docs/Performance_M5.md` (Unreal Insights profiling)
- [ ] `Docs/ReleaseNotes_M5.md`
- [ ] `Docs/Milestone5_CompletionSummary.md` (end-of-milestone report)

### Release Artifacts
- [ ] `PlanetaryCreation_M5.zip` (standalone project)
- [ ] CI/CD pipeline configuration (`.github/workflows/` or equivalent)
- [ ] Updated `CLAUDE.md` with M5 features and workflows

---

## Success Metrics

### Quantitative
- ✅ **Test Coverage:** 27/27 tests passing (100%)
- ✅ **Performance:** L3 ≤110 ms, L5 <120 ms (feature overhead ≤14 ms)
- ✅ **Memory:** Total footprint ≤3 MB (≤+1 MB vs M4)
- ✅ **Determinism:** 100% reproducibility across platforms
- ✅ **Build Time:** CI pipeline <10 minutes per commit

### Qualitative
- ✅ **User Feedback:** External reviewer completes Quick Start tutorial without help
- ✅ **Code Quality:** No critical bugs in release candidate
- ✅ **Documentation:** User guide addresses 90% of common questions
- ✅ **Paper Parity:** Section 4 fully implemented (4.1-4.5)

---

## Post-Milestone Review

### Retrospective Questions
1. Did M5 features add acceptable overhead (≤14 ms)?
2. Were erosion/sediment models realistic per paper?
3. Did CI/CD pipeline reduce manual testing burden?
4. Was user guide sufficient for non-developers?
5. Are we ready to tackle M6 (Stage B amplification + terranes)?

### Lessons Learned
- Document in `Docs/Milestone5_CompletionSummary.md`
- Capture optimization opportunities missed (low-hanging fruit for M6)
- Note API design decisions (playback scheduling, camera state persistence, sediment diffusion tuning)

---

## Next Milestone Preview: M6 – Stage B Amplification & Terranes

**Timeline:** 3-4 weeks
**Focus Areas:**
1. **Terrane Handling:** Extraction from rifts, reattachment at subduction zones
2. **Stage B Amplification:** Procedural noise + exemplar blending (~100 m relief)
3. **Performance Optimization:** SIMD stress interpolation, GPU thermal/stress fields
4. **Level 7 Profiling:** 163,842 vertices, Table 2 parity verification
5. **Advanced Erosion:** Couple erosion to amplified terrain (high-res detail weathering)

**Goal:** Complete Section 5 paper parity (amplification) + terrane mechanics + production optimization (<90ms @ L3).

---

**End of Milestone 5 Plan**
