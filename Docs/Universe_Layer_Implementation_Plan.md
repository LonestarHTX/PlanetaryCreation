# Universe Layer Implementation Plan (Revised)

## Document Status

**Version**: 2.0 (Critical Review Integrated)  
**Status**: Draft for stakeholder review  
**Prerequisites**: Milestone 3 (tectonic stress/elevation) must be **complete and validated** before starting Milestone 0.  
**Estimated Duration**: 22-28 weeks (5.5-7 months) for 1 engineer, 16-20 weeks (4-5 months) for 2 engineers  
**Risk Level**: High — depends on runtime tectonic refactor (core simulation logic)

---

## Executive Summary

We can build a Galaxia-inspired universe exploration layer on top of the tectonic tooling, but **only after addressing critical architectural blockers**. The current tectonic simulation is **editor-only** and cannot run at runtime without a major refactor. The original feasibility analysis underestimated scope by ~3x and misunderstood the RMC Spatial streaming API.

This revised plan:
- Adds **Milestone 0** (prerequisite runtime refactor, 3-4 weeks)
- Corrects streaming approach to use `URealtimeMeshSpatialComponent` properly
- Guards coordinate conversion with mandatory precision limits
- Provides realistic effort estimates (22-28 weeks total)
- Defines hard content/performance budgets
- Expands testing to include load/soak/precision scenarios

**Key Decision Required**: Do we refactor tectonic simulation for runtime (enables procedural planets in-game) OR accept pre-baked planet assets (limits dynamism but faster)? This decision gates all subsequent milestones.

---

## 1. Current Baseline and Blockers

### 1.1 What We Have

- **Editor-only tectonic pipeline**:
  - `UTectonicSimulationService : public UUnrealEditorSubsystem` (Source/PlanetaryCreationEditor/Public/TectonicSimulationService.h:124)
  - Holds double-precision plate simulation, Voronoi mapping, stress/velocity fields
  - `FTectonicSimulationController` converts to float and updates RMC preview mesh
  - `SPTectonicToolPanel` provides Slate UI

- **No runtime analogue**: `UUnrealEditorSubsystem` doesn't exist in packaged builds; `GEditor` is `nullptr` at runtime
- **No origin rebasing or World Partition**: Project uses standard world coordinates
- **RMC Spatial streaming available**: Plugin provides `URealtimeMeshSpatialComponent` and `URealtimeMeshSpatialStreamingSourceComponent`, but we haven't integrated them

### 1.2 Critical Blockers

1. **Runtime tectonic simulation doesn't exist** ⚠️
   - Current code path: `GEditor->GetEditorSubsystem<UTectonicSimulationService>()` (TectonicSimulationController.cpp:248)
   - This is `nullptr` at runtime → no planet surface generation in packaged builds
   - **Must fix before Milestone 1**

2. **Streaming API misunderstanding**
   - Cannot create standalone `IRealtimeMeshSpatialStreamingSourceProvider` and "register it"
   - Must use component-based registration via `URealtimeMeshSpatialStreamingSourceComponent`
   - Must use `URealtimeMeshSpatialComponent` (not base `URealtimeMeshComponent`) for streaming meshes

3. **No precision budget defined**
   - Current tectonic sim uses unit sphere (radius=1) in doubles → converts to km-scale floats
   - Universe scale (millions of km) will exceed float precision without guards

---

## 2. Principles from Galaxia (Validated)

These remain sound and are reinforced by the critical review:

- **Hierarchical coordinates**: (Sector, LocalOffset) keeps math small and accurate
  - Citation: [Galaxia: Coordinates](https://dexyfex.com/2016/07/11/galaxia-the-basics-coordinates/)
- **Multi-scale procedural generation**: Instantiate only what's near the player
  - Citation: [Galaxia in UE4](https://dexyfex.com/2018/07/23/galaxia-now-in-ue4/)
- **LOD transitions**: Cross-fade or morph to hide pops
  - Citation: [Voxel LOD](https://dexyfex.com/2015/11/17/voxels-and-seamless-lod-transitions/)
- **Origin rebasing**: Mandatory for scales >100 sectors (not optional)

---

## 3. Revised Architecture

### 3.1 Coordinate System (Corrected)

```cpp
// Universe coordinate with bounded local offset
struct FUniverseCoordinate
{
    FIntVector Sector = FIntVector::ZeroValue;   // Coarse grid index
    FVector3d  LocalKm = FVector3d::ZeroVector;  // MUST stay within ±(SectorSizeKm/2)
};

// Global constant
constexpr double SectorSizeKm = 10000.0; // Tune based on content density

// Normalize coordinate to keep LocalKm bounded
FUniverseCoordinate NormalizeCoordinate(const FUniverseCoordinate& Coord)
{
    FUniverseCoordinate Result = Coord;
    
    // Wrap LocalKm into adjacent sectors when it exceeds bounds
    if (FMath::Abs(Result.LocalKm.X) > SectorSizeKm / 2.0)
    {
        const int32 SectorShift = FMath::RoundToInt(Result.LocalKm.X / SectorSizeKm);
        Result.Sector.X += SectorShift;
        Result.LocalKm.X -= SectorShift * SectorSizeKm;
    }
    // Repeat for Y, Z
    // ... (full implementation in Milestone A)
    
    return Result;
}

// Convert to render-space transform (ONLY for nearby objects)
TOptional<FTransform> MakeLocalRenderTransform(
    const FUniverseCoordinate& WorldCoord,
    const FIntVector& CameraSector,
    double UnitsPerKm = 1.0)
{
    const FIntVector SectorDelta = WorldCoord.Sector - CameraSector;
    
    // CRITICAL: Reject distant objects; use impostor instead
    if (FMath::Abs(SectorDelta.X) > 1 || 
        FMath::Abs(SectorDelta.Y) > 1 || 
        FMath::Abs(SectorDelta.Z) > 1)
    {
        return {}; // No transform; caller must use impostor/billboard
    }
    
    // Now safe: magnitude ≤ 1.5 * SectorSizeKm (~15,000 km max)
    const FVector3d LocalOffsetKm = WorldCoord.LocalKm + FVector3d(SectorDelta) * SectorSizeKm;
    const FVector LocalOffsetUnits = FVector(LocalOffsetKm * UnitsPerKm); // Down-convert to float
    
    return FTransform(FQuat::Identity, LocalOffsetUnits, FVector::OneVector);
}
```

**Precision budget**:
- Max render distance: 1.5 sectors ≈ 15,000 km = 1.5×10^9 cm
- Float precision at 10^9: ~100 cm (acceptable for distant impostors)
- Float precision at 10^7 (planet surface): ~1 cm (acceptable)
- **Mandatory origin rebasing** when camera crosses >100 sectors from spawn

### 3.2 Streaming Integration (Corrected)

**How RMC Spatial Streaming Actually Works**:

1. **Register a streaming source** (usually attached to player camera):
```cpp
// In player pawn or camera manager
void AMyPlayerPawn::BeginPlay()
{
    Super::BeginPlay();
    
    // Add streaming source component to track camera
    URealtimeMeshSpatialStreamingSourceComponent* StreamingSource = 
        NewObject<URealtimeMeshSpatialStreamingSourceComponent>(this);
    StreamingSource->Radius = 100000.0f; // 100 km in Unreal units (1 unit = 1 km)
    StreamingSource->Priority = ERealtimeMeshStreamingSourcePriority::High;
    StreamingSource->RegisterComponent();
    
    // The component auto-registers with URealtimeMeshSpatialStreamingSubsystem on Register()
}
```

2. **Use URealtimeMeshSpatialComponent for streaming meshes** (NOT base component):
```cpp
// For planet actors that need LOD streaming
AMyPlanetActor::AMyPlanetActor()
{
    // MUST use URealtimeMeshSpatialComponent for streaming support
    SpatialMeshComponent = CreateDefaultSubobject<URealtimeMeshSpatialComponent>(TEXT("PlanetMesh"));
    SetRootComponent(SpatialMeshComponent);
}

void AMyPlanetActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    
    URealtimeMeshSimple* Mesh = SpatialMeshComponent->InitializeRealtimeMesh<URealtimeMeshSimple>();
    
    // Configure LODs with screen size thresholds
    Mesh->UpdateLODConfig(0, FRealtimeMeshLODConfig(0.3f));  // LOD0: >30% screen height
    Mesh->AddLOD(FRealtimeMeshLODConfig(0.1f));              // LOD1: 10-30%
    Mesh->AddLOD(FRealtimeMeshLODConfig(0.03f));             // LOD2: 3-10%
    
    // Create geometry for each LOD...
}
```

3. **Subsystem automatically updates LODs** based on streaming source distance:
   - `URealtimeMeshSpatialStreamingSubsystem` (a `UWorldSubsystem`) ticks and queries all registered sources
   - `URealtimeMeshSpatialComponent` actors compute distance to nearest source
   - Mesh sections are loaded/unloaded based on distance and LOD thresholds

**No custom provider needed** — the plugin handles everything if you use the right components.

### 3.3 Runtime Tectonic Simulation (New Requirement)

**Options**:

**Option A: Full Refactor (enables procedural planets at runtime)**
- Extract simulation logic from `UTectonicSimulationService` into non-UObject helper classes
- Create `URuntimeTectonicSubsystem : public UGameInstanceSubsystem` that wraps the helpers
- Keep editor service as a thin wrapper calling the same helpers
- **Effort**: 3-4 weeks (Milestone 0)
- **Risk**: Medium-high (touches core simulation, risk of regressions)

**Option B: Pre-Baked Assets (faster but less dynamic)**
- Generate planet meshes in editor, serialize as `UStaticMesh` or RMC asset
- Store seed/parameters as metadata for regeneration
- Runtime loads assets instead of computing
- **Effort**: 1-2 weeks (simpler asset pipeline)
- **Limitation**: Cannot dynamically evolve planets in-game; limits gameplay

**Recommendation**: **Option A** if gameplay requires dynamic tectonic simulation (e.g., terraforming, geological time-lapse). **Option B** if planets are static backdrops.

**Decision Required**: Stakeholders must choose before Milestone 0 starts.

---

## 4. Content and Performance Budgets

### 4.1 Hard Limits

| Resource | Limit | Rationale |
|----------|-------|-----------|
| Concurrent star systems | 100 | Actor count / draw calls |
| `URealtimeMeshSpatialComponent` instances | 20 | Streaming subsystem tick cost |
| Planet mesh memory (LOD0) | 50 MB | Typical mid-range GPU budget |
| Sector cache size | 27 (3×3×3 cube) | Camera + adjacent sectors |
| Impostor draw calls | 1000 | Batched billboards/particles |
| Galaxy seed generation time | <100 ms | Must not hitch during sector transition |

### 4.2 Validation Tests

Each milestone must include automation tests that **fail** if budgets are exceeded:
- `FUniverseContentBudgetTest`: Spawn max star systems, measure frame time <16ms, memory <target
- `FUniverseSectorCacheTest`: Fill 27-sector cache, verify GC reclaims old sectors
- `FUniverseImpostorBatchTest`: Render 1000 impostors, validate single draw call

---

## 5. Milestones (Revised with Honest Estimates)

### Prerequisite: Complete Milestone 3 (Current Work)

**Before starting universe layer**, finish and validate:
- Milestone 3 Task 2.4: Elevation displacement and visualization
- Milestone 3 Task 3.2: Boundary overlays
- All automation tests passing
- Performance budgets met (stat unit <16ms for high-density mesh)

**Estimate**: 2-4 weeks (depends on current progress)

---

### Milestone 0: Runtime Tectonic Refactor (PREREQUISITE) ⚠️

**Goal**: Make tectonic simulation available at runtime OR establish pre-baked asset pipeline.

**Tasks**:
1. Extract simulation logic from `UTectonicSimulationService` into helper classes:
   - `FTectonicPlateGenerator` (icosphere, Lloyd, Voronoi)
   - `FTectonicBoundaryClassifier` (ridge/trench detection)
   - `FTectonicStressComputer` (velocity, stress fields)
2. Create `URuntimeTectonicSubsystem : public UGameInstanceSubsystem`
3. Update `UTectonicSimulationService` to delegate to helpers (keep editor tool working)
4. Add `PlanetaryCreation.Build.cs` dependencies (was editor-only)
5. Validate editor tool still works (regression check)
6. Add runtime spawn test: instantiate subsystem in PIE, generate planet, verify mesh

**Deliverables**:
- `Source/PlanetaryCreation/Private/TectonicPlateGenerator.cpp` (runtime-safe)
- `Source/PlanetaryCreation/Public/RuntimeTectonicSubsystem.h` (UGameInstanceSubsystem)
- `Source/PlanetaryCreationEditor/Private/TectonicSimulationService.cpp` (refactored to use helpers)
- Automation test: `FRuntimeTectonicInstantiationTest`

**Estimate**: 3-4 weeks (1 engineer)  
**Dependencies**: Milestone 3 complete  
**Risk**: Medium-high (core refactor, regression potential)

**Alternative (if choosing Option B — pre-baked assets)**:
- Create asset exporter in editor tool (serialize RMC mesh as `.uasset`)
- Create asset loader at runtime
- Skip helper extraction
- **Estimate**: 1-2 weeks

---

### Milestone 1: Coordinate System and Sector Management

**Goal**: Implement sectorized coordinates with precision guards and sector wrapping.

**Tasks**:
1. Implement `FUniverseCoordinate`, `NormalizeCoordinate`, `MakeLocalRenderTransform`
2. Create `UUniverseSubsystem : public UGameInstanceSubsystem` to track camera sector
3. Implement sector boundary crossing detection and origin shift triggers
4. Add debug draw for sector boundaries (3D grid overlay)
5. Unit tests: sector math, wrap-around, precision bounds

**Deliverables**:
- `Source/PlanetaryCreation/Public/UniverseCoordinate.h`
- `Source/PlanetaryCreation/Public/UniverseSubsystem.h`
- Automation tests:
  - `FUniverseCoordinateWrapTest` (verify LocalKm stays bounded)
  - `FUniverseCoordinatePrecisionTest` (verify render error <1 cm within 1 sector)
  - `FUniverseSectorTransitionTest` (cross boundary, validate origin shift)

**Estimate**: 2 weeks (1 engineer)  
**Dependencies**: Milestone 0 complete  
**Risk**: Low (math-only, well-understood)

---

### Milestone 2: Streaming Integration

**Goal**: Integrate RMC Spatial streaming with camera-driven sources.

**Tasks**:
1. Attach `URealtimeMeshSpatialStreamingSourceComponent` to player camera or pawn
2. Create test planet actor using `URealtimeMeshSpatialComponent`
3. Configure LOD thresholds (3 LODs: high/med/low)
4. Validate streaming subsystem registers source and updates LODs
5. Add debug visualization for streaming radius and active LOD

**Deliverables**:
- `Source/PlanetaryCreation/Private/TestPlanetActor.cpp` (streaming validation actor)
- Documentation: `Docs/RMC_Spatial_Streaming_Workflow.md`
- Automation tests:
  - `FRealtimeMeshSpatialSourceRegistrationTest` (verify source appears in subsystem)
  - `FRealtimeMeshSpatialLODSwitchTest` (move camera, verify LOD changes)

**Estimate**: 3 weeks (1 engineer) — includes learning curve for RMC Spatial API  
**Dependencies**: Milestone 1 complete  
**Risk**: Medium (plugin API learning, potential for misconfiguration)

---

### Milestone 3: Procedural Galaxy and Sector Generation

**Goal**: Generate galaxy structure with deterministic seeds; render as impostors.

**Tasks**:
1. Implement seed hierarchy: `Hash(UniverseSeed, Sector, Index)`
2. Generate galaxy distribution (spiral/elliptical presets)
3. Collision detection: reject overlapping star systems
4. **Prototype impostor rendering** (choose one: Niagara, mesh cards, volumetric):
   - Research phase: 1 week spike (test 3 approaches)
   - Implementation: 1 week
5. CSV export for validation (star counts, density, collision rate)

**Deliverables**:
- `Source/PlanetaryCreation/Private/GalaxyGenerator.cpp`
- `Source/PlanetaryCreation/Private/ImpostorRenderer.cpp`
- `Docs/Impostor_Rendering_Approach.md` (chosen technique + rationale)
- Automation tests:
  - `FGalaxySeedDeterminismTest` (same seed → same stars)
  - `FGalaxyCollisionTest` (verify <1% overlap rate)
  - `FGalaxyScaleTest` (generate 1M stars, export CSV <10s)

**Estimate**: 4 weeks (1 engineer) — includes 1-week impostor R&D spike  
**Dependencies**: Milestone 2 complete  
**Risk**: Medium (impostor rendering is new territory)

---

### Milestone 4: Star System Tier and LOD Transitions

**Goal**: Instantiate lightweight star/planet proxies; add hysteresis and cross-fade.

**Tasks**:
1. Spawn star system actors within streaming radius (≤100 concurrent)
2. Attach impostors at long range, switch to `URealtimeMeshSpatialComponent` at mid-range
3. Implement LOD hysteresis (don't thrash when near threshold)
4. Material cross-fade: interpolate opacity or dither pattern during transition
5. Instance culling for distant systems (frustum + distance)

**Deliverables**:
- `Source/PlanetaryCreation/Private/StarSystemActor.cpp`
- `Source/PlanetaryCreation/Private/LODTransitionManager.cpp`
- Material function: `MF_UniverseLODCrossFade`
- Automation tests:
  - `FStarSystemSpawnTest` (spawn 100, verify <16ms frame time)
  - `FLODHysteresisTest` (oscillate camera, verify no thrashing)

**Estimate**: 4 weeks (1 engineer)  
**Dependencies**: Milestone 3 complete  
**Risk**: Medium (LOD thrashing bugs, material complexity)

---

### Milestone 5: Planet Focus Mode

**Goal**: Instantiate full tectonic simulation when focusing on a planet.

**Tasks**:
1. Implement "focus planet" action (UI button or proximity trigger)
2. Spawn `URuntimeTectonicSubsystem` instance (or load pre-baked asset)
3. Bridge universe seed → tectonic parameters (Seed, SubdivisionLevel, etc.)
4. Create `URealtimeMeshSpatialComponent` with high-LOD planet mesh
5. Transition from impostor → low-LOD → high-LOD (multi-stage)
6. Handle unfocus: tear down subsystem, revert to impostor

**Deliverables**:
- `Source/PlanetaryCreation/Private/PlanetFocusManager.cpp`
- `Source/PlanetaryCreationEditor/Private/SUniverseToolPanel.cpp` (editor UI extension)
- Automation tests:
  - `FPlanetFocusTest` (focus → verify tectonic subsystem spawns)
  - `FPlanetUnfocusTest` (unfocus → verify cleanup, no leaks)
  - `FPlanetFocusMemoryTest` (focus/unfocus 10x, verify <100MB growth)

**Estimate**: 5-6 weeks (1 engineer) — depends on Milestone 0 quality  
**Dependencies**: Milestones 0, 4 complete  
**Risk**: High (integration point between universe and tectonic systems)

---

### Milestone 6: Performance, Stability, and Origin Rebasing

**Goal**: Validate budgets, fix hitches, implement origin rebasing.

**Tasks**:
1. Profile with `stat unit`, `stat RHI`, Unreal Insights
2. Implement origin rebasing when camera crosses >100 sectors
3. Add soak test: 30 min continuous flight, no leaks or hitches
4. Stress test: spawn max content (100 systems, 20 meshes), measure budgets
5. Precision validation: place objects at extreme sectors, verify <1 cm error
6. CSV metrics export for regression tracking

**Deliverables**:
- `Source/PlanetaryCreation/Private/OriginRebasingManager.cpp`
- `Saved/UniverseMetrics/UniversePerformance_<timestamp>.csv`
- Automation tests:
  - `FUniverseSoakTest` (30 min run, no leaks)
  - `FUniverseStressTest` (max content, <16ms frame)
  - `FUniversePrecisionTest` (sector ±1000, error <1 cm)

**Estimate**: 3-4 weeks (1 engineer)  
**Dependencies**: Milestones 1-5 complete  
**Risk**: Medium (performance tuning can uncover deep issues)

---

### Total Timeline

| Phase | Duration (1 Engineer) | Duration (2 Engineers) |
|-------|-----------------------|------------------------|
| Prerequisite (M3 finish) | 2-4 weeks | 2-4 weeks |
| Milestone 0 (Runtime refactor) | 3-4 weeks | 2-3 weeks |
| Milestone 1 (Coordinates) | 2 weeks | 1-2 weeks |
| Milestone 2 (Streaming) | 3 weeks | 2 weeks |
| Milestone 3 (Galaxy gen) | 4 weeks | 3 weeks |
| Milestone 4 (Star systems) | 4 weeks | 3 weeks |
| Milestone 5 (Planet focus) | 5-6 weeks | 4 weeks |
| Milestone 6 (Performance) | 3-4 weeks | 2-3 weeks |
| **Total** | **26-35 weeks (6.5-8.7 months)** | **19-24 weeks (4.75-6 months)** |

**Realistic commitment**: 22-28 weeks (5.5-7 months) for 1 engineer, 16-20 weeks (4-5 months) for 2 engineers, assuming no major blockers.

---

## 6. Testing Strategy (Expanded)

### 6.1 Unit Tests (Per Milestone)
- Location math (coordinate wrapping, precision)
- Streaming source registration
- Seed determinism and collision detection
- LOD transition thresholds

### 6.2 Integration Tests
- Planet focus → tectonic subsystem spawn → mesh generation
- Sector boundary crossing → origin rebase → coordinate stability
- Streaming radius change → LOD updates → no leaks

### 6.3 Load and Stress Tests (NEW)
- **Content Stress**: Spawn 100 star systems, 20 planet meshes, measure:
  - Frame time: must stay <16ms (60 FPS target)
  - Memory: must stay <budget (defined per test)
  - Draw calls: log and compare against baseline
- **Soak Test**: Run for 30 minutes with continuous camera motion:
  - No memory leaks (track allocator stats)
  - No frame hitches >100ms
  - Log all transitions (sector cross, LOD switch, focus events)
- **Scale Test**: Generate 1 million stars (data-only):
  - Generation time: <10 seconds
  - CSV export time: <5 seconds
  - Determinism: re-run with same seed, verify bit-identical output

### 6.4 Precision Tests (NEW)
- Place objects at sector coordinates `(±1000, ±1000, ±1000)`
- Measure render position error vs expected universe coordinate
- **Pass criteria**: Error <1 cm for all objects within 1 sector of camera

### 6.5 Regression Tracking
- Export CSV metrics after each milestone: frame times, memory usage, LOD switch counts
- Store in `Saved/UniverseMetrics/`
- CI/CD: compare against baseline, fail if >10% regression

---

## 7. Risks and Mitigations (Updated)

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| Runtime tectonic refactor breaks editor tool | Medium | High | Comprehensive regression tests; keep editor path stable |
| Impostor rendering R&D spike exceeds 1 week | Medium | Medium | Timebox spike; fallback to simplest approach (billboard cards) |
| RMC Spatial streaming misconfiguration | Low | Medium | Follow plugin examples; validate with streaming test actors |
| Origin rebasing causes teleport bugs | Medium | High | Unit tests for coordinate stability; incremental implementation |
| Performance budgets exceeded | Medium | High | Profile early and often; aggressively LOD/cull |
| Scope creep into voxel terrain | Low | High | Defer voxels; reuse current icosphere mesh |

---

## 8. Repository Integration (Corrected)

### 8.1 Module Structure

```
Source/
├── PlanetaryCreation/              # Runtime module (ships in packaged builds)
│   ├── Private/
│   │   ├── TectonicPlateGenerator.cpp      # Extracted from editor service
│   │   ├── RuntimeTectonicSubsystem.cpp
│   │   ├── UniverseSubsystem.cpp
│   │   ├── GalaxyGenerator.cpp
│   │   ├── StarSystemActor.cpp
│   │   ├── PlanetFocusManager.cpp
│   │   └── Tests/
│   │       ├── UniverseCoordinateTest.cpp
│   │       ├── RuntimeTectonicTest.cpp
│   │       └── UniverseStressTest.cpp
│   ├── Public/
│   │   ├── UniverseCoordinate.h
│   │   ├── RuntimeTectonicSubsystem.h
│   │   └── UniverseSubsystem.h
│   └── PlanetaryCreation.Build.cs          # Add RealtimeMeshSpatial dependency
│
└── PlanetaryCreationEditor/        # Editor-only module
    ├── Private/
    │   ├── TectonicSimulationService.cpp    # Refactored to delegate to runtime helpers
    │   ├── SPTectonicToolPanel.cpp
    │   └── SUniverseToolPanel.cpp           # New editor UI for universe preview
    └── PlanetaryCreationEditor.Build.cs     # Depends on runtime module
```

### 8.2 Build Configuration

**PlanetaryCreation.Build.cs**:
```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Core", "CoreUObject", "Engine",
    "RealtimeMeshComponent",
    "RealtimeMeshSpatial"  // ADD THIS for streaming support
});
```

**PlanetaryCreationEditor.Build.cs**:
```csharp
PrivateDependencyModuleNames.AddRange(new string[] {
    "PlanetaryCreation",  // Runtime module
    "UnrealEd", "Slate", "SlateCore", "EditorSubsystem"
});
```

---

## 9. Open Questions (Must Resolve Before Starting)

1. **Runtime tectonic approach**: Refactor (Option A) or pre-baked assets (Option B)?  
   **Owner**: Technical lead + stakeholders  
   **Deadline**: Before Milestone 0 kickoff

2. **Content budgets**: Are the limits in Section 4.1 acceptable? Can we reduce star system count or planet mesh resolution if needed?  
   **Owner**: Art lead + engineering  
   **Deadline**: Milestone 2 planning

3. **Impostor technique**: Niagara (particle-based), mesh cards (quad billboards), or volumetric (3D texture slices)?  
   **Owner**: Rendering engineer  
   **Deadline**: Milestone 3 spike (week 1)

4. **Seed collision handling**: Reject overlapping systems (safer) or spatial hash + offset (denser)?  
   **Owner**: Gameplay design + engineering  
   **Deadline**: Milestone 3 design review

5. **Origin rebasing frequency**: Every 100 sectors (safer) or 1000 sectors (less overhead)?  
   **Owner**: Engineering  
   **Deadline**: Milestone 6 tuning

---

## 10. Go/No-Go Criteria

### Before Starting Milestone 0
- ✅ Milestone 3 complete and validated (current work)
- ✅ Decision made on runtime tectonic approach (refactor vs assets)
- ✅ Team capacity confirmed (1-2 engineers for 5-7 months)
- ✅ Stakeholders accept 22-28 week timeline

### After Milestone 0 (Refactor Checkpoint)
- ✅ Runtime tectonic subsystem spawns and generates mesh in PIE
- ✅ Editor tool still works (no regressions)
- ✅ Automation tests pass

**Abort criteria**: If runtime refactor takes >6 weeks or breaks editor tool irreparably, pause and reassess approach (switch to Option B or defer universe layer).

### After Milestone 2 (Streaming Validation)
- ✅ RMC Spatial streaming sources registered and visible in debug
- ✅ LOD transitions occur without crashes or visual pops
- ✅ Frame time <16ms with 20 streaming mesh components

**Abort criteria**: If streaming subsystem doesn't integrate properly or performance is <30 FPS, halt and investigate plugin configuration or alternative approaches.

---

## 11. Success Criteria (Final Validation)

At the end of Milestone 6, the universe layer is **shippable** if:

1. **Functional**:
   - Player can fly through 27-sector region with seamless LOD transitions
   - Focusing on a planet spawns full tectonic surface (or loads asset)
   - Galaxy/system/planet tiers render correctly at all ranges

2. **Performant**:
   - Frame time: 60 FPS (16ms) with max content
   - Memory: Within budgets (Section 4.1)
   - No hitches >100ms during sector transitions or focus events

3. **Stable**:
   - 30-minute soak test: no crashes, leaks, or frame drops
   - Precision test: <1 cm error at all tested sectors
   - Deterministic: same seed produces identical universe on multiple runs

4. **Tested**:
   - All unit/integration/stress tests passing
   - CSV metrics baseline established for regression tracking
   - Documentation complete (RMC workflow, impostor approach, coordinate math)

---

## 12. References

- Galaxia now in UE4 — https://dexyfex.com/2018/07/23/galaxia-now-in-ue4/
- Galaxia: The basics – Coordinates — https://dexyfex.com/2016/07/11/galaxia-the-basics-coordinates/
- Galaxy rendering revisited — https://dexyfex.com/2016/09/12/galaxy-rendering-revisited/
- Voxels and Seamless LOD Transitions — https://dexyfex.com/2015/11/17/voxels-and-seamless-lod-transitions/
- Voxels and Dual Contouring — https://dexyfex.com/2015/04/30/voxels-and-dual-contouring/
- Black Holes & Accretion Discs – Gravitational Lensing — https://dexyfex.com/2016/10/13/black-holes-accretion-discs-gravitational-lensing/
- RealtimeMeshComponent How-To — `Plugins/RealtimeMeshComponent/RealtimeMeshComponent_HowTo.md`

---

## Document Change Log

- **v1.0** (2025-01-04): Initial feasibility analysis (overly optimistic)
- **v2.0** (2025-01-04): Critical review integrated; added Milestone 0, corrected streaming approach, guarded coordinate math, realistic timeline, expanded testing, clarified module structure

