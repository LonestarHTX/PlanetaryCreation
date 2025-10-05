# Milestone 6 Plan – Stage B Amplification & Terrane Mechanics

**Status:** 📋 Planning
**Timeline:** 4 weeks (Weeks 17-20)
**Dependencies:** Milestone 5 complete ✅
**Performance Budget:** 103ms available (110ms target - 6.32ms M5 actual)

---

## Mission

Complete **Section 5** of the "Procedural Tectonic Planets" paper by implementing Stage B amplification with procedural noise and exemplar blending to generate ~100m resolution detail. Add terrane extraction and reattachment mechanics for realistic continental collision dynamics. Optimize performance for LOD Level 6 (40,962 vertices), establish baseline for Level 7 (163,842 vertices), and validate **Level 8 (655,362 vertices)** against paper Table 2 benchmarks for full parity.

---

## Goals

1. **Stage B Amplification:** Procedural noise + exemplar-based terrain synthesis (Paper Section 5)
2. **Terrane Mechanics:** Extraction, transport, and reattachment at subduction zones (Paper Section 4.2)
3. **Advanced Erosion Coupling:** Hydraulic routing and erosion on amplified terrain
4. **Performance Optimization:** Achieve <90ms/step at L3, <120ms at L6
5. **Paper Table 2 Parity:** Validate all metrics match paper benchmarks

---

## Phase Breakdown

### Phase 1 – Terrane Mechanics *(Weeks 17–18)*

**Goal:** Implement realistic continental fragment extraction, transport, and collision dynamics per paper Section 4.2.

> **Integration Spike:** Before feature work, prototype terrane mesh surgery on a cloned test plate to validate extraction/reattachment math, rollback safety, and history capture. This guards against blocking issues mid-milestone.

#### Task 1.0: Terrane Mesh Surgery Spike
**Owner:** Simulation Engineer
**Effort:** 3 days (extended for production-scale testing)
**Deliverables:**
- Isolated test within `TectonicSimulationService` (not standalone - uses real service state)
- Test mesh: **Level 3 (642 vertices)** - ship-critical LOD, not trivial L0
- Instrumentation for Euler characteristic, edge manifold checks, rollback timing, **performance benchmarking**
- Integration testing: Extract/reattach during retessellation, undo/redo via rollback
- Failure mode catalog: Degenerate cases, edge cases, performance cliffs
- Report documenting findings and API adjustments needed

**Test Scenarios:**
1. **Basic extraction/reattachment:** 50-vertex terrane from 80-vertex plate
2. **Retessellation during terrane lifecycle:** Extract terrane → trigger retessellation → verify terrane indices still valid
3. **Rollback integration:** Extract terrane → undo via rollback → verify mesh bit-identical to pre-extraction
4. **Performance baseline:** Measure extraction/reattachment time at L3/L5/L6
5. **Edge cases:** Single-vertex terrane, terrane spanning plate boundary, extracting from single-terrane plate

**Acceptance Criteria (Concrete Assertions):**
- ✅ **Topology validity:** V - E + F = 2 (Euler characteristic) maintained after extract/reattach
- ✅ **Manifold edges:** Each edge touches exactly 2 triangles (no dangling edges)
- ✅ **No orphaned vertices:** All vertices referenced by ≥1 triangle
- ✅ **Retessellation safety:** Terrane survives retessellation with valid vertex indices
- ✅ **Rollback determinism:** Undo produces bit-identical mesh (same vertex positions, indices, plate assignments)
- ✅ **Performance targets:** Extraction <5ms, reattachment <10ms at L3
- ✅ **History integrity:** Snapshot diff shows only expected changes (terrane vertices moved, plate counts updated)
- ✅ **Failure mode catalog:** Documents 5+ edge cases with mitigation strategies

Findings feed directly into Task 1.1 design notes. If spike reveals blocking issues (e.g., retessellation incompatibility), escalate immediately.

#### Task 1.1: Terrane Extraction System
**Owner:** Simulation Engineer
**Effort:** 4 days
**Paper Reference:** Section 4.2 (Continental Collision), Section 4.4 (Plate Rifting)
**Deliverables:**
- Terrane detection: Identify continental fragments at rift boundaries
- Topology surgery: Extract terrane vertices/triangles from parent plate
- Ownership transfer: Assign extracted terrane to oceanic carrier plate
- Deterministic extraction: Seed-stable terrane boundaries

**Technical Notes:**
```cpp
// Terrane extraction criteria (paper Section 4.2)
struct FTerraneExtractionParams
{
    double MinContinentalArea_km2 = 500000.0; // 500k km² minimum (size of Madagascar)
    double MaxRiftVelocity_km_My = 0.08;      // Exceeding this triggers extraction
    double RiftDurationThreshold_My = 20.0;   // 20 My sustained rifting required
    double EdgeProximityThreshold_km = 100.0; // Distance to rift boundary
};

// Terrane data structure
struct FTectonicTerrane
{
    int32 TerraneID;                          // Unique identifier
    int32 SourcePlateID;                      // Original plate before extraction
    int32 CarrierPlateID;                     // Current oceanic plate transporting terrane
    TArray<int32> VertexIndices;              // Vertices in terrane
    FVector3d Centroid;                       // Geodesic centroid
    double Area_km2;                          // Total area
    double ExtractionTime_My;                 // When terrane was extracted
    ETerraneType Type;                        // Island arc, microcontinent, continental fragment
};
```

**Validation:**
- ✅ Automation test: `TerraneExtractionTest`
  - Rift boundary crossing threshold triggers extraction
  - Extracted vertices removed from source plate, added to carrier plate
  - Area conservation: Source plate area = pre-extraction - terrane area
  - Euler characteristic maintained: V - E + F = 2 (with holes)
- ✅ Manual check: Force rift with high divergence, verify terrane extraction within 50 steps
- ✅ CSV export: `TerraneID`, `SourcePlate`, `CarrierPlate`, `ExtractionTime_My` columns
- ✅ Performance: <3ms per extraction event

**Acceptance:**
- Rifting events spawn continental fragments that migrate with oceanic plates
- Topology surgery maintains mesh validity (no dangling edges, degenerate triangles)
- Deterministic: Same seed produces same terrane boundaries
- CSV export enables terrane lifecycle tracking

---

#### Task 1.2: Terrane Transport & Tracking
**Owner:** Simulation Engineer
**Effort:** 2 days
**Deliverables:**
- Terrane ownership tracking: Update carrier plate as terranes migrate
- Collision detection: Identify approaching convergent boundaries
- Visualization overlay: Highlight terranes with distinct colors
- History logging: Track terrane paths across plate boundaries

**Technical Notes:**
- Terranes inherit velocity from carrier plate (oceanic plate Euler pole)
- Update terrane centroid every step: `Centroid(t+dt) = Rotate(Centroid(t), EulerPole, AngularVelocity * dt)`
- Collision proximity check: Flag terrane when distance to convergent boundary < 500 km
- Store terrane migration history for visualization (dotted path lines)

**Mathematical Details:**
```cpp
// Terrane migration (inherits carrier plate motion)
void UpdateTerranePosition(FTectonicTerrane& Terrane, double DeltaTime_My)
{
    const FTectonicPlate& CarrierPlate = GetPlate(Terrane.CarrierPlateID);
    const FVector3d RotationAxis = CarrierPlate.EulerPoleAxis;
    const double AngularVelocity_rad_My = CarrierPlate.AngularVelocity;
    const double RotationAngle = AngularVelocity_rad_My * DeltaTime_My;

    // Rotate all terrane vertices around carrier plate's Euler pole
    for (int32 VertexIdx : Terrane.VertexIndices)
    {
        FVector3d& Vertex = RenderVertices[VertexIdx];
        Vertex = RotateAroundAxis(Vertex, RotationAxis, RotationAngle);
    }

    // Update centroid
    Terrane.Centroid = ComputeCentroid(Terrane.VertexIndices);
}
```

**Validation:**
- ✅ Automation test: `TerraneTransportTest`
  - Terrane centroid moves with carrier plate (within 1% error)
  - Collision proximity detection triggers 10-20 steps before impact
  - Visualization overlay shows terrane boundaries and migration paths
- ✅ Manual check: Extract terrane, advance 100 steps, verify collision proximity warning
- ✅ Performance: <1ms per step for terrane tracking

**Acceptance:**
- Terranes migrate realistically with oceanic carrier plates
- Collision detection provides early warning for reattachment events
- Visualization overlay enables intuitive understanding of terrane dynamics

---

#### Task 1.3: Terrane Reattachment & Suturing
**Owner:** Simulation Engineer
**Effort:** 4 days
**Paper Reference:** Section 4.2 (Continental Collision)
**Deliverables:**
- Collision event trigger: Detect terrane-continent proximity (<300 km per paper)
- Slab breakoff: Detach terrane from oceanic carrier plate
- Suturing surgery: Merge terrane vertices with target continent
- Uplift propagation: Elevate collision zone per paper formula (Section 4.2)

**Technical Notes:**
```cpp
// Terrane reattachment criteria (paper Section 4.2)
bool ShouldReattachTerrane(const FTectonicTerrane& Terrane, const FTectonicPlate& TargetPlate)
{
    const double DistanceToTarget = GetGeodesicDistance(Terrane.Centroid, TargetPlate);
    const double CollisionThreshold_km = 300.0; // Per paper Section 6

    if (DistanceToTarget < CollisionThreshold_km)
    {
        // Continental-continental collision required (terrane is continental by definition)
        return TargetPlate.CrustType == ECrustType::Continental;
    }
    return false;
}

// Uplift formula (paper Section 4.2, Equation for Δz)
double ComputeCollisionUplift(const FVector3d& Point, const FTectonicTerrane& Terrane, double RelativeVelocity_km_My)
{
    const double DistanceToTerrane = GetGeodesicDistance(Point, Terrane.Centroid);
    const double InfluenceRadius = ComputeInfluenceRadius(Terrane.Area_km2, RelativeVelocity_km_My); // Paper Eq. for r

    if (DistanceToTerrane > InfluenceRadius)
        return 0.0;

    // Paper formula: Δz(p) = Δz_c * f(d_R(p)), f(x) = (1 - (x/r)²)²
    const double NormalizedDistance = DistanceToTerrane / InfluenceRadius;
    const double FalloffFactor = FMath::Square(1.0 - FMath::Square(NormalizedDistance));
    const double CollisionCoefficient = 0.8; // Δz_c from paper Table A (Appendix A)

    return CollisionCoefficient * FalloffFactor;
}
```

**Validation:**
- ✅ Automation test: `TerraneReattachmentTest`
  - Terrane-continent proximity <300 km triggers reattachment
  - Slab breakoff removes terrane from oceanic plate
  - Suturing adds terrane vertices to target continent
  - Uplift creates mountain range in collision zone (elevation increase >1 km)
  - Area conservation: Target plate area = pre-collision + terrane area
- ✅ Manual check: Setup terrane collision, verify mountain range formation
- ✅ CSV export: `CollisionEvent`, `UpliftMagnitude_m`, `InfluenceRadius_km`
- ✅ Performance: <5ms per collision event

**Acceptance:**
- Terrane collision generates realistic mountain ranges (Himalayan-style orogeny)
- Topology surgery produces valid mesh after suturing
- Collision events logged with timestamp, plates, and uplift metrics
- Deterministic: Same seed produces same collision results

---

#### Task 1.4: Terrane Regression & Edge Cases
**Owner:** QA Engineer
**Effort:** 2 days
**Deliverables:**
- Edge case handling: Single-vertex terranes, terrane-terrane collisions, carrier plate subduction
- Validation suite: 5 automated tests covering terrane lifecycle
- Stress testing: 10 simultaneous terranes in 500-step simulation
- Error recovery: Graceful handling of invalid terrane states

**Test Scenarios:**
1. **Tiny Terrane:** <100 km² fragment, verify cleanup (merge with nearest plate)
2. **Terrane-Terrane Collision:** Two terranes collide before reaching continent
3. **Carrier Plate Subduction:** Oceanic carrier subducts while transporting terrane
4. **Rapid Rifting Storm:** 5 terranes extracted in 20 steps
5. **Mega-Terrane:** Gondwana-scale fragment (>10M km²) collision

**Validation:**
- ✅ Automation test: `TerraneEdgeCasesTest` (covers all 5 scenarios)
- ✅ Stress test: 10 terranes, 500 steps, no crashes
- ✅ Error messages: "Terrane too small, merging with carrier plate"
- ✅ Performance: No degradation with <20 active terranes

**Acceptance:**
- All edge cases handled gracefully without crashes
- Tiny terranes auto-merged, preventing mesh fragmentation
- Terrane-terrane collisions produce sensible results
- System scales to 20 simultaneous terranes

---

#### Task 1.5: Terrane State Serialization & Persistence
**Owner:** Simulation Engineer
**Effort:** 2 days
**Deliverables:**
- CSV export: `Terranes.csv` with columns `TerraneID`, `SourcePlateID`, `CarrierPlateID`, `Centroid_Lat`, `Centroid_Lon`, `Area_km2`, `ExtractionTime_My`, `Type`
- Binary serialization: Save/load terrane state with simulation snapshots
- Deterministic terrane ID generation: Seed-based ID assignment for cross-platform reproducibility
- History tracking: Export terrane lifecycle events (extraction, transport, collision)

**Technical Notes:**
```cpp
// Deterministic terrane ID generation (seed-based)
int32 GenerateTerraneID(int32 SourcePlateID, int32 Seed, double ExtractionTime_My)
{
    // Hash: PlateID + Seed + ExtractionTime → Unique ID
    uint32 Hash = 0;
    Hash = HashCombine(Hash, GetTypeHash(SourcePlateID));
    Hash = HashCombine(Hash, GetTypeHash(Seed));
    Hash = HashCombine(Hash, GetTypeHash(static_cast<int32>(ExtractionTime_My * 1000.0)));
    return static_cast<int32>(Hash);
}
```

**CSV Format:**
```csv
TerraneID,SourcePlateID,CarrierPlateID,Centroid_Lat,Centroid_Lon,Area_km2,ExtractionTime_My,Type,CollisionTime_My,TargetPlateID
12345,3,7,23.5,-45.2,750000,150.0,MicroContinent,NULL,NULL
67890,5,2,67.1,12.8,250000,175.0,IslandArc,220.0,8
```

**Validation:**
- ✅ Automation test: `TerranePersistenceTest`
  - Save simulation with 5 terranes → load → verify all terranes restored
  - CSV export includes all active terranes
  - Deterministic IDs: Same seed produces same terrane IDs across runs
- ✅ Cross-platform: Save on Windows → load on Linux → identical state
- ✅ Performance: <1ms per terrane for serialization

**Acceptance:**
- Terranes persist across save/load cycles
- CSV export enables terrane lifecycle analysis
- Deterministic IDs ensure cross-platform reproducibility
- M6 saves are backwards compatible with M5 (terranes optional)

---

### Phase 2 – Stage B Amplification *(Weeks 18–19)*

**Goal:** Implement paper Section 5 amplification to generate ~100m resolution detail on the base tectonic crust.

#### Task 2.1: Procedural Noise Amplification (Oceanic)
**Owner:** Rendering Engineer, Simulation Engineer
**Effort:** 5 days
**Paper Reference:** Section 5 (Procedural Amplification)
**Deliverables:**
- 3D Gabor noise generator for mid-ocean ridges
- Transform fault synthesis perpendicular to ridges
- Age-based fault accentuation (young crust = stronger faults)
- High-frequency gradient noise for underwater detail
- Material integration: Normal/tangent computation for lighting

**Technical Notes:**
```cpp
// Paper Section 5: Procedural amplification for oceanic crust
// "Mid-ocean ridges are characterized by many transform faults lying perpendicular to the ridges.
//  We recreate this feature by using 3D Gabor noise, oriented using the recorded parameters r_c,
//  i.e. the local direction parallel to the ridge, and oceanic crust age a_o to accentuate the
//  faults where the crust is young."

double ComputeOceanicAmplification(const FVector3d& Position, const FOceanicCrustData& CrustData)
{
    // Base elevation from M5 (coarse tectonic simulation)
    double BaseElevation = CrustData.Elevation_m;

    // 3D Gabor noise oriented perpendicular to ridge
    FVector3d RidgeParallel = CrustData.RidgeDirection; // r_c from paper
    FVector3d TransformFaultDir = FVector3d::CrossProduct(RidgeParallel, Position.GetSafeNormal());

    // Age-based fault amplitude (young crust has stronger faults)
    double CrustAge_My = CrustData.Age_My;
    double AgeFactor = FMath::Exp(-CrustAge_My / 50.0); // Exponential decay, τ = 50 My
    double FaultAmplitude_m = 150.0 * AgeFactor; // 150m max for young ridges, decays with age

    // 3D Gabor noise (anisotropic, oriented along transform faults)
    double GaborNoise = ComputeGaborNoise3D(Position, TransformFaultDir, /*frequency*/ 0.01);
    double FaultDetail = FaultAmplitude_m * GaborNoise;

    // High-frequency gradient noise for fine detail (underwater scenes)
    double GradientNoise = ComputeGradientNoise3D(Position, /*octaves*/ 4, /*frequency*/ 0.1);
    double FineDetail = 20.0 * GradientNoise; // ±20m variation

    return BaseElevation + FaultDetail + FineDetail;
}
```

**Implementation Steps:**
1. Integrate 3D Gabor noise library (or implement from paper [LLDD09])
2. Extend `RenderVertices` with amplified elevation field
3. Compute normals/tangents from amplified mesh for material system
4. Add visualization toggle: "Show Amplified Detail" vs "Show Coarse Tectonics"
5. Profile GPU mesh update cost with 10× vertex density

**Validation:**
- ✅ Automation test: `OceanicAmplificationTest`
  - Transform faults perpendicular to ridges (dot product < 0.1)
  - Young crust (<10 My) shows strong faults (amplitude >100m)
  - Old crust (>200 My) shows weak faults (amplitude <20m)
  - High-frequency detail adds ±20m variation
- ✅ Visual check: Ocean ridge shows realistic transform fault pattern
- ✅ Performance: <15ms per step for Level 3 amplification (1,280 faces → 12,800 faces estimated)

**Acceptance:**
- Oceanic crust displays realistic transform fault patterns
- Age-based fault decay matches geophysical observations
- Normal maps enable high-quality lighting of underwater terrain
- Performance budget: <15ms of 50ms allocated to Stage B

---

#### Task 2.2: Exemplar-Based Amplification (Continental)
**Owner:** Rendering Engineer, Content Engineer
**Effort:** 7 days
**Paper Reference:** Section 5 (Exemplar-Based Generation)
**Deliverables:**
- Exemplar library: 10+ real-world DEM patches (USGS SRTM 90m resolution)
- Terrain type classification: Andean, Himalayan, Old Mountains, Plains
- Primitive blending system: Weighted heightfield synthesis
- Fold direction alignment: Rotate primitives to match tectonic fold `f`
- Repetition mitigation: Random offsets, multi-exemplar blending

**Technical Notes:**
```cpp
// Paper Section 5: Exemplar-based amplification for continental crust
// "Continental points sampling the crust falling in an orogeny zone are assigned specific x_T
//  depending on the recorded endogenous factor σ, i.e. subduction or continental collision.
//  The resulting terrain type is either Andean or Himalayan."

enum class ETerrainType : uint8
{
    Plain,           // Low elevation, no orogeny
    OldMountains,    // Orogeny age >100 My (eroded ranges)
    AndeanMountains, // Subduction orogeny (volcanic arc)
    HimalayanMountains // Continental collision orogeny (fold/thrust belt)
};

ETerrainType ClassifyTerrainType(const FContinentalCrustData& CrustData)
{
    // Not in orogeny zone → Plain
    if (CrustData.OrogenyType == EOrogenyType::None)
        return ETerrainType::Plain;

    // Old orogeny (>100 My) → Old Mountains (eroded)
    if (CrustData.OrogenyAge_My > 100.0)
        return ETerrainType::OldMountains;

    // Recent subduction → Andean (volcanic arc)
    if (CrustData.OrogenyType == EOrogenyType::Subduction)
        return ETerrainType::AndeanMountains;

    // Recent continental collision → Himalayan (fold/thrust)
    if (CrustData.OrogenyType == EOrogenyType::Collision)
        return ETerrainType::HimalayanMountains;

    return ETerrainType::Plain;
}

double ComputeContinentalAmplification(const FVector3d& Position, const FContinentalCrustData& CrustData, const TArray<FExemplar>& Exemplars)
{
    double BaseElevation = CrustData.Elevation_m;
    ETerrainType TerrainType = ClassifyTerrainType(CrustData);

    // Select 3-5 matching exemplars for blending
    TArray<FExemplar> MatchingExemplars = FilterExemplarsByType(Exemplars, TerrainType);
    check(MatchingExemplars.Num() >= 3); // Require multiple exemplars per type

    // Blend weighted heightfield samples
    double AmplifiedElevation = 0.0;
    double TotalWeight = 0.0;

    for (const FExemplar& Exemplar : MatchingExemplars)
    {
        // Rotate primitive to align fold direction (paper: "rotated in the tangent plane to align f")
        FVector3d LocalFoldDir = CrustData.FoldDirection; // f from paper
        FVector3d ExemplarFoldDir = Exemplar.AverageFoldDirection;
        double RotationAngle = ComputeAlignmentAngle(LocalFoldDir, ExemplarFoldDir);

        // Sample exemplar heightfield with random offset (avoid repetition)
        FVector2D UV = ProjectToTangentPlane(Position, CrustData.Centroid);
        UV += FMath::VRand() * 0.1; // Small random offset
        UV = RotateUV(UV, RotationAngle);

        double SampledHeight = Exemplar.SampleHeightfield(UV);
        double Weight = ComputeBlendWeight(Position, Exemplar); // Distance-based falloff

        AmplifiedElevation += SampledHeight * Weight;
        TotalWeight += Weight;
    }

    AmplifiedElevation /= TotalWeight;

    // Scale amplified detail to match base elevation range
    double DetailScale = CrustData.ElevationRange_m / Exemplar.NativeElevationRange_m;
    return BaseElevation + (AmplifiedElevation * DetailScale);
}
```

**Exemplar Library Setup:**
1. Download USGS SRTM 90m DEM tiles (public domain)
   - Andes (Chile/Peru): Subduction zone, volcanic arc
   - Himalayas (Nepal/Tibet): Continental collision, fold/thrust belt
   - Appalachians (USA): Old mountains, eroded
   - Great Plains (USA): Low elevation, minimal relief
2. Preprocess exemplars: Extract fold direction, elevation range, terrain type
3. Store as UTexture2D assets in `Content/PlanetaryCreation/Exemplars/`
4. Create `UExemplarLibrary` DataAsset for runtime lookup

> **Fallback Strategy:** If USGS SRTM data becomes unavailable (license change, download issues) or exemplar quality insufficient, implement procedural fallback:
> - **Andean:** Perlin noise with volcanic peaks + linear ridge alignment
> - **Himalayan:** Multi-octave ridged noise with fold direction warping
> - **Old Mountains:** Smoothed erosion-style noise (low frequency)
> - **Plains:** Minimal elevation variation (high-frequency detail only)
> This ensures M6 ships even if exemplar pipeline has issues. Procedural quality lower but acceptable for initial release.

**Validation:**
- ✅ Automation test: `ContinentalAmplificationTest`
  - Subduction zones use Andean exemplars
  - Collision zones use Himalayan exemplars
  - Plains use low-relief exemplars
  - Fold direction alignment: Exemplar rotated to match within ±10°
  - Blending produces smooth transitions (no visible seams)
- ✅ Visual check: Mountain ranges show realistic detail, no repetition artifacts
- ✅ Performance: <20ms per step for Level 3 amplification

**Acceptance:**
- Continental crust displays realistic terrain types matching tectonic history
- Exemplar blending produces seamless, non-repetitive detail
- Fold direction alignment creates coherent mountain range orientations
- Performance budget: <20ms of 50ms allocated to Stage B

---

#### Task 2.3: Amplification Integration & LOD
**Owner:** Rendering Engineer
**Effort:** 3 days
**Deliverables:**
- LOD cascade: Amplification only at render subdivision levels ≥5
- GPU mesh streaming: Async upload of amplified vertex data
- Material system: PBR shaders with elevation-based coloring
- Visualization modes: Tectonic (M5), Amplified (M6), Blended
- Performance profiling: Measure GPU upload cost vs CPU amplification

**Technical Notes:**
- Amplification is **render-only**: Simulation uses coarse M5 mesh
- LOD Level 3 (1,280 faces): No amplification (M5 baseline)
- LOD Level 5 (20,480 faces): 2× subdivision + amplification
- LOD Level 6 (81,920 faces): 4× subdivision + amplification
- Store amplified data in separate `AmplifiedRenderVertices` array

**Validation:**
- ✅ Automation test: `AmplificationLODTest`
  - Level 3 shows M5 coarse mesh (no amplification)
  - Level 5 shows 2× subdivided + amplified mesh
  - Level 6 shows 4× subdivided + amplified mesh
  - Simulation time invariant to LOD (amplification is render-only)
- ✅ Performance: L5 <120ms/step, L6 <150ms/step (within M6 budget)

**Acceptance:**
- Amplification seamlessly integrated into existing LOD system
- GPU mesh streaming handles 4× vertex density increase
- Material system highlights tectonic features (ridges, trenches, mountains)

---

### Phase 3 – Advanced Erosion & Coupling *(Week 19)*

**Goal:** Couple M5 erosion system to M6 amplified terrain for high-resolution weathering.

#### Task 3.1: Hydraulic Routing on Amplified Mesh
**Owner:** Simulation Engineer
**Effort:** 4 days
**Deliverables:**
- Downhill flow accumulation: Compute drainage basins on amplified mesh
- Hydraulic erosion: Per-vertex erosion based on flow accumulation
- Sediment redistribution: Transport eroded material to basins
- Integration with M5 diffusion: Hybrid approach (diffusion + hydraulic)

**Technical Notes:**
```cpp
// Hydraulic erosion on amplified mesh (upgrade from M5 diffusion-only)
// PERFORMANCE OPTION: If full hydraulic routing exceeds 8ms budget at L6,
// apply continental-only erosion (skip oceanic vertices) or hierarchical routing (coarse→fine)
void ComputeHydraulicErosion(TArray<FVector3d>& AmplifiedVertices, double DeltaTime_My, bool bContinentalOnly = false)
{
    // Step 1: Build downhill flow graph (Dijkstra shortest path to sea level)
    TArray<int32> DownhillNeighbor; // For each vertex, stores index of lowest neighbor
    TArray<double> FlowAccumulation; // Accumulated upstream drainage area

    for (int32 VertexIdx = 0; VertexIdx < AmplifiedVertices.Num(); ++VertexIdx)
    {
        // CONTINGENCY: Skip oceanic vertices if performance budget exceeded
        if (bContinentalOnly && VertexPlateAssignments[VertexIdx].CrustType == ECrustType::Oceanic)
            continue;

        int32 LowestNeighbor = FindLowestNeighbor(VertexIdx, AmplifiedVertices);
        DownhillNeighbor[VertexIdx] = LowestNeighbor;
    }

    // Step 2: Compute flow accumulation (upstream drainage area)
    ComputeFlowAccumulation(DownhillNeighbor, FlowAccumulation);

    // Step 3: Hydraulic erosion (proportional to flow accumulation and slope)
    for (int32 VertexIdx = 0; VertexIdx < AmplifiedVertices.Num(); ++VertexIdx)
    {
        if (bContinentalOnly && VertexPlateAssignments[VertexIdx].CrustType == ECrustType::Oceanic)
            continue;

        double Elevation = AmplifiedVertices[VertexIdx].Z;
        double Slope = ComputeSlope(VertexIdx, AmplifiedVertices);
        double Discharge = FlowAccumulation[VertexIdx]; // Drainage area

        // Stream power erosion law: E = K * A^m * S^n (K=erodibility, A=area, S=slope)
        const double K_hydraulic = 0.002; // m/My
        const double m = 0.5; // Area exponent
        const double n = 1.0; // Slope exponent

        double ErosionRate = K_hydraulic * FMath::Pow(Discharge, m) * FMath::Pow(Slope, n);
        double ErosionAmount = ErosionRate * DeltaTime_My;

        AmplifiedVertices[VertexIdx].Z -= ErosionAmount;

        // Deposit sediment in downstream neighbor (conservation)
        int32 DownstreamIdx = DownhillNeighbor[VertexIdx];
        if (DownstreamIdx != INDEX_NONE)
        {
            AmplifiedVertices[DownstreamIdx].Z += ErosionAmount * 0.5; // 50% deposition
        }
    }
}

// Alternative: Hierarchical flow routing (if full routing too slow)
// 1. Build coarse flow graph on L3 mesh (642 vertices)
// 2. Interpolate flow directions to fine mesh (L6: 40,962 vertices)
// 3. Refine locally within drainage basins
// Expected speedup: 3-5× (trades accuracy for performance)
```

**Validation:**
- ✅ Automation test: `HydraulicRoutingTest`
  - Flow accumulation increases downstream
  - Drainage basins correctly identified
  - Mass conservation: Total erosion ≈ total deposition (within 5%)
- ✅ Visual check: River valleys form in mountain ranges
- ✅ Performance: <8ms per step (within M6 budget)

**Acceptance:**
- Hydraulic erosion creates realistic drainage networks
- Valleys and river basins emerge from amplified terrain
- Mass conservation maintained (erosion/deposition balance)

---

#### Task 3.2: Erosion-Deposition Coupling with Stage B
**Owner:** Simulation Engineer
**Effort:** 2 days
**Deliverables:**
- Couple hydraulic erosion to exemplar-based amplification
- Adjust exemplar selection based on erosion history
- Age-based weathering: Old mountains more eroded than young
- Visualization: Erosion/deposition heatmap overlay

**Technical Notes:**
- Young orogeny (<20 My): Minimal erosion, sharp peaks
- Medium orogeny (20-100 My): Moderate erosion, rounded peaks
- Old orogeny (>100 My): Heavy erosion, rolling hills

**Validation:**
- ✅ Automation test: `ErosionCouplingTest`
  - Young mountains retain amplified detail
  - Old mountains show smoothed, eroded profiles
- ✅ Visual check: Appalachian-style erosion for old ranges
- ✅ Performance: <2ms overhead (included in 8ms hydraulic budget)

**Acceptance:**
- Erosion respects tectonic age and orogeny type
- Exemplar blending adapts to erosion state

---

### Phase 4 – Performance Optimization & Profiling *(Week 20)*

**Goal:** Achieve production performance targets: <90ms/step L3, <120ms L6, baseline L7 for Table 2 parity.

#### Task 4.1: SIMD Vectorization (Voronoi, Stress)
**Owner:** Performance Engineer
**Effort:** 3 days
**Deliverables:**
- SIMD Voronoi mapping: Vectorize geodesic distance calculation
- SIMD stress interpolation: Vectorize Gaussian falloff
- Platform-specific optimizations: SSE4.2/AVX2 (x64), NEON (ARM)
- Benchmark suite: Measure speedup vs scalar implementation

**Technical Notes:**
```cpp
// SIMD geodesic distance (4 vertices per iteration)
void ComputeVoronoiMapping_SIMD(const TArray<FVector3d>& Vertices, const TArray<FVector3d>& PlateCentroids, TArray<int32>& VertexPlateAssignments)
{
    const int32 NumVertices = Vertices.Num();
    const int32 NumPlates = PlateCentroids.Num();

    for (int32 VertexIdx = 0; VertexIdx < NumVertices; VertexIdx += 4)
    {
        // Load 4 vertices into SIMD registers
        __m256d VertexX = _mm256_set_pd(Vertices[VertexIdx+0].X, Vertices[VertexIdx+1].X, Vertices[VertexIdx+2].X, Vertices[VertexIdx+3].X);
        __m256d VertexY = _mm256_set_pd(Vertices[VertexIdx+0].Y, Vertices[VertexIdx+1].Y, Vertices[VertexIdx+2].Y, Vertices[VertexIdx+3].Y);
        __m256d VertexZ = _mm256_set_pd(Vertices[VertexIdx+0].Z, Vertices[VertexIdx+1].Z, Vertices[VertexIdx+2].Z, Vertices[VertexIdx+3].Z);

        __m256d MinDistance = _mm256_set1_pd(DBL_MAX);
        __m256i ClosestPlate = _mm256_set1_epi64x(-1);

        for (int32 PlateIdx = 0; PlateIdx < NumPlates; ++PlateIdx)
        {
            // Compute geodesic distance (dot product → acos)
            const FVector3d& Centroid = PlateCentroids[PlateIdx];
            __m256d CentroidX = _mm256_set1_pd(Centroid.X);
            __m256d CentroidY = _mm256_set1_pd(Centroid.Y);
            __m256d CentroidZ = _mm256_set1_pd(Centroid.Z);

            __m256d DotProduct = _mm256_add_pd(_mm256_mul_pd(VertexX, CentroidX),
                                 _mm256_add_pd(_mm256_mul_pd(VertexY, CentroidY),
                                               _mm256_mul_pd(VertexZ, CentroidZ)));

            // Geodesic distance = acos(dot) * PlanetRadius
            __m256d Distance = _mm256_mul_pd(_mm256_acos_pd(DotProduct), _mm256_set1_pd(PlanetRadius));

            // Update closest plate
            __m256d Mask = _mm256_cmp_pd(Distance, MinDistance, _CMP_LT_OQ);
            MinDistance = _mm256_blendv_pd(MinDistance, Distance, Mask);
            ClosestPlate = _mm256_blendv_epi8(ClosestPlate, _mm256_set1_epi64x(PlateIdx), _mm256_castpd_si256(Mask));
        }

        // Store results
        _mm256_storeu_si256((__m256i*)&VertexPlateAssignments[VertexIdx], ClosestPlate);
    }
}
```

**Expected Gains (Target):**
- Voronoi mapping: 2.2ms → 0.8ms (2.75× speedup)
- Stress interpolation: 1.1ms → 0.4ms (2.75× speedup)
- **Total savings:** ~2.1ms per step

**Contingency Plan (If SIMD Underdelivers):**
If actual speedup <2× (e.g., compiler auto-vectorization interference, cache thrashing):
- **Minimum acceptable:** 1.5× speedup → saves 1.0ms instead of 2.1ms
- **Budget impact:** M6 total becomes 49.87ms instead of 48.77ms (still 44% headroom)
- **Mitigation:** Prioritize Voronoi SIMD (larger impact than stress), defer stress optimization to M7
- **Documentation:** Note SIMD gains as "up to 2.75×" instead of guaranteed

**Validation:**
- ✅ Automation test: `SIMDRegressionTest`
  - SIMD results match scalar within 0.01% (floating-point tolerance)
  - Determinism preserved (bit-exact on same CPU)
- ✅ Benchmark: SIMD vs scalar speedup ≥1.5× minimum (target: ≥2.5×)
- ✅ Performance: Voronoi <1.2ms, Stress <0.7ms (conservative targets)
- ✅ Platform coverage: Test on AVX2, SSE4.2, NEON, scalar fallback

**Acceptance:**
- SIMD implementation matches scalar accuracy
- Speedup ≥1.5× measured on target hardware (stretch goal: 2.5×)
- Falls back to scalar on unsupported CPUs
- Multi-platform build: AVX2 (x64 modern), SSE4.2 (x64 legacy), NEON (ARM), scalar (fallback)

---

#### Task 4.2: GPU Compute Shaders (Thermal, Velocity Fields)
**Owner:** Rendering Engineer
**Effort:** 4 days
**Deliverables:**
- GPU thermal field: Compute shader for hotspot contribution
- GPU velocity field: Compute shader for v = ω × r
- CPU/GPU hybrid: Simulation on CPU, visualization on GPU
- Profiling: Measure GPU compute cost vs CPU baseline

**Technical Notes:**
```hlsl
// GPU Compute Shader: Velocity Vector Field
// v = ω × r (angular velocity cross position)
[numthreads(64, 1, 1)]
void ComputeVelocityField(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint VertexIdx = DispatchThreadID.x;
    if (VertexIdx >= NumVertices)
        return;

    float3 Position = RenderVertices[VertexIdx].Position;
    int PlateID = VertexPlateAssignments[VertexIdx];

    float3 EulerPoleAxis = PlateData[PlateID].EulerPoleAxis;
    float AngularVelocity = PlateData[PlateID].AngularVelocity;

    // v = ω × r
    float3 VelocityDir = cross(EulerPoleAxis, Position);
    float VelocityMagnitude = AngularVelocity * length(Position);
    float3 Velocity = normalize(VelocityDir) * VelocityMagnitude;

    VelocityField[VertexIdx] = Velocity;
}
```

**Expected Gains (Target):**
- Thermal field: 0.2ms → 0.05ms (4× speedup, GPU compute)
- Velocity field: 0.4ms → 0.1ms (4× speedup, GPU compute)
- **Total savings:** ~0.45ms per step

**Contingency Plan (If GPU Transfer Overhead High):**
If CPU→GPU data transfer exceeds compute savings (e.g., PCIe bandwidth bottleneck):
- **Minimum acceptable:** 2× speedup → saves 0.2ms instead of 0.45ms
- **Budget impact:** M6 total becomes 49.02ms instead of 48.77ms (still 45% headroom)
- **Mitigation:** Batch multiple compute passes (thermal + velocity + stress) to amortize transfer cost
- **Fallback:** Keep CPU implementation, defer GPU optimization to M7

**Validation:**
- ✅ GPU results match CPU within 0.1% (floating-point variance)
- ✅ Performance: GPU compute <0.35ms total (including transfer, target: <0.2ms)
- ✅ Compatibility: Falls back to CPU on unsupported GPUs (DX11 Compute Shader 5.0 minimum)
- ✅ Profiling: Measure CPU→GPU transfer overhead vs compute savings

**Acceptance:**
- GPU compute provides ≥2× speedup for field calculations (stretch goal: 4×)
- CPU/GPU hybrid maintains determinism for simulation
- Visualization fields (thermal, velocity) computed on GPU
- Graceful degradation to CPU on older hardware

---

#### Task 4.3: Level 6, 7, 8 Profiling (Paper Table 2 Parity)
**Owner:** Performance Engineer
**Effort:** 4 days (extended for L8 paper-parity profiling)
**Paper Reference:** Table 2 (Performance Metrics)
**Deliverables:**
- Level 6 profiling: 40,962 vertices, <120ms/step target (ship-critical)
- Level 7 baseline: 163,842 vertices, document actual timing
- **Level 8 paper-parity:** 655,362 vertices, validate against Table 2 benchmarks
- Table 2 comparison: Align metrics with paper benchmarks
- Performance report: `Docs/Performance_M6.md`

**Performance Targets:**
| LOD | Vertices | M5 Actual | M6 Target | Paper Table 2 | Notes |
|-----|----------|-----------|-----------|---------------|-------|
| L3 ✅ | 642 | 6.32ms | <90ms | N/A | Ship-critical (M5 baseline) |
| L5 ✅ | 10,242 | ~15ms | <120ms | N/A | High-detail preview |
| L6 🎯 | 40,962 | ~35ms | <120ms | Baseline | Ship-critical (M6 target) |
| L7 📊 | 163,842 | TBD | <200ms | ~50km sampling | Document for M7 |
| L8 🎯 | 655,362 | TBD | <400ms | **Paper-parity** (~25km sampling) | Table 2 validation |

**M6 Performance Budget Breakdown:**
```
M5 Baseline (L3):               6.32 ms
+ Terrane extraction/tracking:  2.00 ms (amortized, rare events)
+ Procedural amplification:    15.00 ms (Gabor noise, gradient noise)
+ Exemplar amplification:      20.00 ms (heightfield blending)
+ Hydraulic erosion:            8.00 ms (flow routing + redistribution)
+ SIMD optimizations:          -2.10 ms (Voronoi + stress)
+ GPU compute:                 -0.45 ms (thermal + velocity)
= M6 Total (L3):               48.77 ms (target: <90ms, 54% headroom) ✅

M6 Total (L6):                ~120ms (extrapolated, 4× vertex scaling)
```

**Validation:**
- ✅ Profiling session: Capture 500-step run at L6 with Unreal Insights
- ✅ Table 2 comparison: Document alignment/deviations from paper
- ✅ Hot-path analysis: Identify top 10 functions by CPU time

**Acceptance:**
- L6 performance <120ms/step (ship-critical)
- L7 baseline documented for future optimization
- Table 2 metrics aligned with paper (where applicable)
- Performance report published

---

#### Task 4.4: Memory Profiling & Optimization
**Owner:** Performance Engineer
**Effort:** 2 days
**Deliverables:**
- Heap profiling: Measure M6 memory footprint increase
- Amplification buffers: Optimize exemplar storage
- Terrane tracking: Minimize overhead (<100 KB per terrane)
- Memory target: <8 MB total (M5: ~2 MB, budget +6 MB for M6)

**Memory Budget:**
| Component | M5 Baseline | M6 Target | Notes |
|-----------|-------------|-----------|-------|
| Simulation state | ~2.0 MB | ~2.5 MB | +terranes, +exemplar refs |
| Amplified mesh | 0 MB | ~3.0 MB | L6: 40k vertices × 48 bytes |
| Exemplar library | 0 MB | ~2.0 MB | 10 exemplars × 512×512×4 bytes |
| Terrane tracking | 0 MB | ~0.5 MB | 20 terranes × 10k vertices × 4 bytes |
| **Total** | **2.0 MB** | **8.0 MB** | 4× increase, acceptable |

**Validation:**
- ✅ Memory profiler: No leaks over 1000-step run
- ✅ Heap fragmentation: <10% overhead
- ✅ Streaming: Exemplars loaded on-demand, not all in memory

**Acceptance:**
- Total memory <8 MB (within budget)
- No memory leaks detected
- Exemplar streaming reduces peak memory by 30%

---

### Phase 5 – Validation & Documentation *(Week 20)*

**Goal:** Expand test suite to cover M6 features, validate paper parity, document completion.

#### Task 5.1: Automation Test Suite Expansion
**Owner:** QA Engineer
**Effort:** 2 days
**Deliverables:**
- 8 new tests covering M6 features
- Regression suite: Verify M4/M5 tests still pass
- Stress testing: 1000-step simulations with amplification enabled
- Cross-platform validation: Windows/Linux determinism

**New Tests:**
1. `TerraneExtractionTest` - Rift-triggered terrane creation
2. `TerraneTransportTest` - Migration with carrier plate
3. `TerraneReattachmentTest` - Collision and suturing
4. `TerraneEdgeCasesTest` - Tiny terranes, carrier subduction
5. `OceanicAmplificationTest` - Gabor noise, transform faults
6. `ContinentalAmplificationTest` - Exemplar blending, fold alignment
7. `HydraulicRoutingTest` - Flow accumulation, drainage basins
8. `AmplificationLODTest` - Amplification only at L5+
9. `TerranePersistenceTest` - Save/load terrane state
10. **`PerformanceRegressionTest_M6`** - Validate M6 overhead <50ms vs M5 baseline
11. **`AmplificationDeterminismTest`** - Same seed + LOD = identical amplified mesh (cross-platform)

**Performance Regression Test Details:**
```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPerformanceRegressionTest_M6,
    "PlanetaryCreation.Milestone6.PerformanceRegression",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPerformanceRegressionTest_M6::RunTest(const FString& Parameters)
{
    // M5 Baseline: 6.32ms @ L3 (measured in Performance_M5.md)
    const double M5_Baseline_L3_ms = 6.32;
    const double M6_Target_L3_ms = 90.0;
    const double M6_Overhead_Budget_ms = 50.0; // Max acceptable M6 overhead

    // Run 100-step simulation with M6 features enabled
    UTectonicSimulationService* Service = GetTectonicSimulationService();
    FTectonicSimulationParameters Params = GetDefaultM6Params(); // Amplification, terranes, hydraulic erosion
    Service->SetParameters(Params);

    double TotalStepTime_ms = 0.0;
    for (int32 Step = 0; Step < 100; ++Step)
    {
        double StepStartTime = FPlatformTime::Seconds();
        Service->AdvanceSteps(1);
        double StepEndTime = FPlatformTime::Seconds();
        TotalStepTime_ms += (StepEndTime - StepStartTime) * 1000.0;
    }

    double AvgStepTime_ms = TotalStepTime_ms / 100.0;
    double M6_Overhead_ms = AvgStepTime_ms - M5_Baseline_L3_ms;

    UE_LOG(LogTemp, Log, TEXT("M6 Performance: Avg step time = %.2f ms (M5 baseline: %.2f ms, Overhead: %.2f ms)"),
        AvgStepTime_ms, M5_Baseline_L3_ms, M6_Overhead_ms);

    // Validate targets
    TestTrue(TEXT("L3 step time <90ms"), AvgStepTime_ms < M6_Target_L3_ms);
    TestTrue(TEXT("M6 overhead <50ms"), M6_Overhead_ms < M6_Overhead_Budget_ms);

    return true;
}
```

**Amplification Determinism Test Details:**
```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAmplificationDeterminismTest,
    "PlanetaryCreation.Milestone6.AmplificationDeterminism",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAmplificationDeterminismTest::RunTest(const FString& Parameters)
{
    UTectonicSimulationService* Service = GetTectonicSimulationService();

    // Run 1: Seed 42, L5 amplification, 50 steps
    FTectonicSimulationParameters Params;
    Params.Seed = 42;
    Params.RenderSubdivisionLevel = 5; // 10,242 vertices with amplification
    Params.bEnableAmplification = true;
    Service->SetParameters(Params);
    Service->AdvanceSteps(50);
    TArray<FVector3d> AmplifiedMesh_Run1 = Service->GetAmplifiedRenderVertices();

    // Run 2: Same seed, same LOD, same steps
    Service->ResetSimulation();
    Service->SetParameters(Params); // Same seed 42
    Service->AdvanceSteps(50);
    TArray<FVector3d> AmplifiedMesh_Run2 = Service->GetAmplifiedRenderVertices();

    // Validate bit-identical results (same seed = same Gabor/exemplar noise)
    TestEqual(TEXT("Vertex count matches"), AmplifiedMesh_Run1.Num(), AmplifiedMesh_Run2.Num());

    for (int32 i = 0; i < AmplifiedMesh_Run1.Num(); ++i)
    {
        const FVector3d& V1 = AmplifiedMesh_Run1[i];
        const FVector3d& V2 = AmplifiedMesh_Run2[i];

        // Floating-point tolerance: ±0.01m (1cm) for amplified elevation
        TestTrue(FString::Printf(TEXT("Vertex %d position matches"), i),
            V1.Equals(V2, 0.01));
    }

    return true;
}
```

**Validation:**
- ✅ All 38 tests pass (27 from M5 + 11 new)
- ✅ 1000-step stress test with amplification (no crashes, no leaks)
- ✅ Cross-platform determinism: Identical results Windows/Linux (within floating-point tolerance)
- ✅ Performance regression: M6 overhead <50ms vs M5 (enforced by automated test)
- ✅ Amplification determinism: Same seed produces identical amplified mesh across runs

**Acceptance:**
- Test suite covers all M6 features
- Long-duration stability with amplification enabled
- Determinism maintained across platforms

---

#### Task 5.2: Paper Table 2 Parity Validation
**Owner:** Simulation Engineer, Performance Engineer
**Effort:** 2 days
**Paper Reference:** Table 2 (Performance & Quality Metrics)
**Deliverables:**
- Reproduce paper Table 2 benchmark scenarios
- Document alignment/deviations from paper results
- Quality validation: Visual comparison with paper figures
- Publish parity report: `Docs/PaperParityReport_M6.md`

**Table 2 Metrics (Paper):**
- **Simulation time:** 1000 steps at L6 (40,962 vertices)
- **Amplification time:** Stage B on full planet
- **Memory footprint:** Peak heap usage
- **Visual quality:** Continent shape realism, mountain detail

**Validation:**
- ✅ Run 1000-step simulation with paper-equivalent parameters
- ✅ Compare timing: Our L6 vs paper's reported metrics
- ✅ Visual comparison: Our amplified output vs paper Figures 1, 11, 12
- ✅ Parity report: Document deviations (e.g., UE implementation vs custom engine)

**Acceptance:**
- Key metrics within 20% of paper values (accounting for hardware/engine differences)
- Visual quality matches paper figures (expert validation)
- Deviations documented with justification

---

#### Task 5.3: Documentation & Release Notes
**Owner:** Technical Writer, Tools Engineer
**Effort:** 2 days
**Deliverables:**
- Update `Docs/UserGuide.md` with M6 features
- Create `Docs/ReleaseNotes_M6.md`
- Create `Docs/Milestone6_CompletionSummary.md`
- Update `CLAUDE.md` with M6 patterns

**User Guide Updates:**
- Terrane system: How to trigger extraction, monitor transport
- Amplification controls: Enable/disable, LOD selection, exemplar library
- Advanced erosion: Hydraulic vs diffusion modes
- Performance tuning: LOD recommendations, exemplar memory usage

**Release Notes:**
- M6 headline features: Terrane mechanics, Stage B amplification
- Performance improvements: SIMD, GPU compute, L6 optimization
- **Backwards compatibility:**
  - ✅ M6 can load M5 saves (terranes ignored if missing from save file)
  - ✅ M6 saves include terrane data in optional fields (M5 can skip unknown columns)
  - ✅ CSV schema v4.0: Adds `Terranes.csv`, extends main CSV with amplification columns
  - ⚠️ M5 cannot load M6 saves if terranes present (will fail with "Unknown field: TerraneID")
  - **Migration path:** Users should upgrade to M6 before creating new saves with terranes
- Breaking changes: None for M5→M6 upgrade path
- Known issues: L7/L8 performance not yet optimized (L8 profiling baseline only)

**Completion Summary:**
- Executive summary: M6 achievements vs plan
- Lessons learned: Exemplar library size, SIMD portability
- Deferred work: Full Stage A adaptive meshing (partial in M4)
- M7 preview: Climate coupling, advanced materials

**Acceptance:**
- User guide updated with M6 workflows
- Release notes published for external users
- Completion summary documents lessons learned

---

## Paper Alignment

### Implemented in M6 (Section 5 Complete)
- ✅ **Section 5:** Procedural amplification (3D Gabor noise, transform faults)
- ✅ **Section 5:** Exemplar-based amplification (real-world DEM blending)
- ✅ **Section 4.2:** Terrane extraction, transport, reattachment (complete)
- ✅ **Section 4.5:** Hydraulic erosion upgrade (from M5 diffusion)

### Paper Parity Status
- ✅ **Section 4:** Complete (4.1-4.5 all implemented)
- ✅ **Section 5:** Complete (procedural + exemplar amplification)
- ⚠️ **Section 3:** Partial (adaptive meshing deferred, LOD system covers most use cases)
- ❌ **Section 6:** Not started (future work: user study, geophysics validation)

### Deferred to M7+
- ⚠️ **Full Stage A:** Adaptive meshing with dynamic refinement (partial LOD in M4/M6)
- ❌ **Climate coupling:** Atmospheric circulation, precipitation patterns
- ❌ **Advanced materials:** PBR textures, biome-specific shaders

---

## Acceptance Criteria

### Terrane Mechanics ✅
- [ ] Rift boundaries trigger terrane extraction when divergence threshold exceeded
- [ ] Terranes migrate with oceanic carrier plates
- [ ] Terrane-continent collision produces realistic mountain ranges
- [ ] Topology surgery maintains mesh validity (Euler characteristic = 2)
- [ ] 10 simultaneous terranes supported in 500-step simulation

### Stage B Amplification ✅
- [ ] Oceanic crust displays transform faults perpendicular to ridges
- [ ] Young ridges (<10 My) show strong faults, old abyssal plains smooth
- [ ] Continental crust uses terrain-type-specific exemplars (Andean, Himalayan, etc.)
- [ ] Fold direction alignment: Exemplars rotated to match tectonic fold within ±10°
- [ ] No visible repetition artifacts in amplified terrain
- [ ] LOD Level 5+ shows amplified detail, L3 remains coarse (M5 baseline)

### Advanced Erosion ✅
- [ ] Hydraulic routing computes downhill flow paths
- [ ] Drainage basins correctly identified
- [ ] Mass conservation: Total erosion ≈ total deposition (within 5%)
- [ ] River valleys emerge in amplified mountain ranges
- [ ] Age-based weathering: Old mountains more eroded than young

### Performance ✅
- [ ] L3 (642 vertices): <90ms/step (M6 target, ship-critical)
- [ ] L6 (40,962 vertices): <120ms/step (ship-critical)
- [ ] L7 (163,842 vertices): Baseline documented (target: <200ms)
- [ ] **L8 (655,362 vertices): Baseline documented (target: <400ms, paper Table 2 parity)**
- [ ] Memory footprint: <8 MB total (L6), document L8 memory usage
- [ ] SIMD optimizations: ≥1.5× speedup minimum (target: 2.5×)
- [ ] GPU compute: ≥2× speedup minimum (target: 4×)

### Validation ✅
- [ ] **38 automation tests pass (27 M5 + 11 M6)**
- [ ] 1000-step stress test with amplification enabled (no crashes, no leaks)
- [ ] Cross-platform determinism: Windows/Linux identical results
- [ ] Amplification determinism: Same seed = identical amplified mesh
- [ ] Performance regression: M6 overhead <50ms vs M5 baseline
- [ ] Paper Table 2 parity: L8 metrics within 20% of paper values

### Documentation ✅
- [ ] User guide covers terrane workflow, amplification controls
- [ ] Release notes document M6 features and performance improvements
- [ ] Completion summary captures lessons learned
- [ ] Paper parity report validates Table 2 alignment

---

## Performance Targets

### Step Time Budget (M6)
| LOD   | Vertices | M5 Actual | M6 Target | M6 Budget Breakdown | Notes |
|-------|----------|-----------|-----------|---------------------|-------|
| L3 🎯 | 642      | 6.32 ms   | <90 ms    | 48.77ms (54% headroom) | Ship-critical |
| L5 ✅ | 10,242   | ~15 ms    | <120 ms   | ~75ms (38% headroom) | High-detail preview |
| L6 🎯 | 40,962   | ~35 ms    | <120 ms   | ~120ms (0% headroom) | Ship-critical (M6 target) |
| L7 📊 | 163,842  | TBD       | <200 ms   | Document baseline | ~50km sampling (M7 target) |
| L8 🎯 | 655,362  | TBD       | <400 ms   | Document for Table 2 | **Paper-parity** ~25km sampling |

### M6 Feature Overhead (L3 Budget)
| Feature | Target | Notes |
|---------|--------|-------|
| Terrane extraction | <2ms | Amortized (rare events) |
| Terrane tracking | <1ms | Per-step overhead |
| Procedural amplification | <15ms | Gabor noise + gradient noise |
| Exemplar amplification | <20ms | Heightfield blending |
| Hydraulic erosion | <8ms | Flow routing + redistribution |
| SIMD optimizations | -2.1ms | Voronoi + stress |
| GPU compute | -0.45ms | Thermal + velocity fields |
| **Net M6 Overhead** | **+42.45ms** | Total: 48.77ms |

### Memory Budget (M6)
| Component | M5 Baseline | M6 Target | Delta |
|-----------|-------------|-----------|-------|
| Simulation state | 2.0 MB | 2.5 MB | +0.5 MB |
| Amplified mesh (L6) | 0 MB | 3.0 MB | +3.0 MB |
| Exemplar library | 0 MB | 2.0 MB | +2.0 MB |
| Terrane tracking | 0 MB | 0.5 MB | +0.5 MB |
| **Total** | **2.0 MB** | **8.0 MB** | **+6.0 MB** |

---

## Risk Register

### High Priority
1. **Exemplar Library Size:** 10+ exemplars × 2048×2048 could exceed memory budget
   - **Mitigation:** Stream exemplars on-demand, use 512×512 tiles, compress with DXT1
   - **Contingency:** Reduce to 5 core exemplars (Andean, Himalayan, Old, Plain, Oceanic)

2. **Hydraulic Erosion Cost:** Flow routing on 40k vertices may exceed 8ms budget
   - **Mitigation:** Implement hierarchical flow routing (coarse → fine)
   - **Contingency:** Apply hydraulic erosion only to continental vertices (<50% of mesh)

3. **Terrane Topology Surgery:** Edge cases (self-intersecting terranes, slivers) may crash
   - **Mitigation:** Robust intersection tests, validate mesh after surgery
   - **Contingency:** Log invalid terrane states, skip reattachment, continue simulation

### Medium Priority
4. **SIMD Platform Compatibility:** AVX2 not available on older CPUs
   - **Mitigation:** Multi-path implementation (AVX2, SSE4.2, NEON, scalar)
   - **Contingency:** Fall back to scalar if SIMD unavailable (2× slower but functional)

5. **GPU Compute Shader Compatibility:** Some GPUs may not support compute shaders
   - **Mitigation:** Feature detection, fall back to CPU implementation
   - **Contingency:** Document GPU requirements in user guide

### Low Priority
6. **Paper Table 2 Parity:** Hardware/engine differences may prevent exact match
   - **Mitigation:** Document deviations with justification (UE vs custom engine)
   - **Contingency:** Aim for "within 20%" tolerance, prioritize visual quality over exact timing

---

## Dependencies

### External
- **USGS SRTM Data:** Public domain DEM tiles for exemplar library (freely available)
- **Gabor Noise Library:** Implement from paper [LLDD09] or use existing UE plugin
- **Unreal Engine 5.5:** SIMD intrinsics, compute shaders, Unreal Insights profiling
- **RealtimeMeshComponent v5:** High-density mesh streaming (no changes expected)

### Internal (Milestone 5 Complete ✅)
- ✅ Continental erosion (M5) - baseline for hydraulic upgrade
- ✅ Sediment diffusion (M5) - integrate with hydraulic routing
- ✅ LOD system (M4) - amplification targets L5+
- ✅ Rollback system (M4) - terrane extraction snapshots

---

## Deliverables Checklist

### Code
- [ ] Terrane extraction system (`ExtractTerrane()`, `FTectonicTerrane` struct)
- [ ] Terrane transport & tracking (`UpdateTerranePosition()`, collision detection)
- [ ] Terrane reattachment (`ReattachTerrane()`, suturing surgery)
- [ ] Procedural oceanic amplification (`ComputeOceanicAmplification()`, Gabor noise)
- [ ] Exemplar continental amplification (`ComputeContinentalAmplification()`, heightfield blending)
- [ ] Hydraulic erosion upgrade (`ComputeHydraulicErosion()`, flow routing)
- [ ] SIMD optimizations (`ComputeVoronoiMapping_SIMD()`, `ComputeStressInterpolation_SIMD()`)
- [ ] GPU compute shaders (`ComputeVelocityField.usf`, `ComputeThermalField.usf`)

### Tests
- [ ] **11 new automation tests** (terranes, amplification, hydraulic erosion, performance regression, determinism)
- [ ] Regression suite (verify M4/M5 27 tests still pass)
- [ ] 1000-step stress test with amplification enabled
- [ ] Cross-platform determinism test (Windows/Linux)
- [ ] Amplification determinism test (same seed = identical mesh)
- [ ] Performance regression test (M6 overhead <50ms)
- [ ] Paper Table 2 parity validation (L8 profiling)

### Documentation
- [ ] `Docs/UserGuide.md` (updated with M6 features)
- [ ] `Docs/ReleaseNotes_M6.md`
- [ ] `Docs/Performance_M6.md` (profiling report)
- [ ] `Docs/PaperParityReport_M6.md` (Table 2 validation)
- [ ] `Docs/Milestone6_CompletionSummary.md`
- [ ] `CLAUDE.md` (updated with M6 patterns)

### Assets
- [ ] Exemplar library: 10 USGS SRTM DEM tiles (Andes, Himalayas, Appalachians, Plains)
- [ ] `UExemplarLibrary` DataAsset (`Content/PlanetaryCreation/Exemplars/`)
- [ ] Material shaders: PBR with elevation-based coloring

---

## Success Metrics

### Quantitative
- ✅ **Test Coverage:** 38/38 tests passing (100%)
- ✅ **Performance:** L3 <90ms, L6 <120ms, L7 baseline <200ms, **L8 baseline <400ms (paper-parity)**
- ✅ **Memory:** L6 footprint <8 MB, document L8 usage
- ✅ **Paper Parity:** L8 Table 2 metrics within 20% of paper values
- ✅ **Optimization Gain:** SIMD ≥1.5× (target: 2.5×), GPU compute ≥2× (target: 4×)

### Qualitative
- ✅ **Visual Quality:** Amplified terrain indistinguishable from paper figures
- ✅ **Terrane Realism:** Continental collision produces believable mountain ranges
- ✅ **User Feedback:** External reviewer confirms improved detail vs M5
- ✅ **Code Quality:** No critical bugs in release candidate
- ✅ **Paper Parity:** Section 5 fully implemented (4.1-5 complete)

---

## Post-Milestone Review

### Retrospective Questions
1. Did terrane mechanics add acceptable complexity vs realism gain?
2. Was exemplar library size manageable (memory, content creation)?
3. Did SIMD/GPU optimizations deliver projected speedup?
4. Were paper Table 2 metrics achievable in UE implementation?
5. Are we ready for M7 (climate coupling, advanced materials)?

### Lessons Learned
- Document in `Docs/Milestone6_CompletionSummary.md`
- Capture SIMD portability challenges (AVX2 vs SSE vs NEON)
- Note exemplar selection criteria (DEM quality, fold direction extraction)
- Document terrane edge cases (tiny fragments, carrier subduction)

---

## Next Milestone Preview: M7 – Climate Coupling & Advanced Materials

**Timeline:** 3-4 weeks
**Focus Areas:**
1. **Atmospheric Circulation:** Simplified climate model (temperature, precipitation)
2. **Biome Classification:** Elevation + latitude + climate → terrain type
3. **Advanced Materials:** PBR shaders with biome-specific textures
4. **Glacial Erosion:** Extend erosion model for ice sheets
5. **Performance Polish:** Achieve <60ms/step at L3, <90ms at L6

**Goal:** Add climate-aware terrain generation, realistic material shading, and final performance optimization for production release.

---

**End of Milestone 6 Plan**
