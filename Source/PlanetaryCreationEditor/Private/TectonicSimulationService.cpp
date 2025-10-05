#include "TectonicSimulationService.h"
#include "Math/RandomStream.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "SphericalKDTree.h"

void UTectonicSimulationService::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    ResetSimulation();
}

void UTectonicSimulationService::Deinitialize()
{
    BaseSphereSamples.Reset();
    Plates.Reset();
    SharedVertices.Reset();
    Boundaries.Reset();
    RenderVertices.Reset();
    RenderTriangles.Reset();
    VertexPlateAssignments.Reset();
    VertexVelocities.Reset();
    VertexStressValues.Reset();
    Super::Deinitialize();
}

void UTectonicSimulationService::ResetSimulation()
{
    CurrentTimeMy = 0.0;

    // Milestone 5: Clear all per-vertex arrays for deterministic resets
    VertexElevationValues.Empty();
    VertexErosionRates.Empty();
    VertexSedimentThickness.Empty();
    VertexCrustAge.Empty();

    // Milestone 6: Clear amplification arrays
    VertexRidgeDirections.Empty();
    VertexAmplifiedElevation.Empty();

    // Milestone 4 Phase 5: Reset version counters for test isolation
    TopologyVersion = 0;
    SurfaceDataVersion = 0;
    RetessellationCount = 0;

    // Milestone 6 Task 1.3: Clear terranes on reset
    Terranes.Empty();
    NextTerraneID = 0;

    GenerateDefaultSphereSamples();

    // Milestone 2: Generate plate simulation state
    GenerateIcospherePlates();
    InitializeEulerPoles();
    BuildBoundaryAdjacencyMap();
    ValidateSolidAngleCoverage();

    // Milestone 3: Generate high-density render mesh
    GenerateRenderMesh();

    // Milestone 3 Task 3.1: Apply Lloyd relaxation BEFORE Voronoi mapping
    // Note: Lloyd uses render mesh vertices to compute Voronoi cells, so must run after GenerateRenderMesh()
    ApplyLloydRelaxation();

    // Milestone 3 Phase 2: Build Voronoi mapping (event-driven, refreshed after Lloyd)
    BuildVoronoiMapping();

    // Milestone 3 Task 2.2: Compute velocity field (event-driven, same trigger as Voronoi)
    ComputeVelocityField();

    // Milestone 3 Task 2.3: Initialize stress field (zero at start)
    InterpolateStressToVertices();

    // Milestone 3 Task 3.3: Capture initial plate positions for re-tessellation detection
    InitialPlateCentroids.SetNum(Plates.Num());
    for (int32 i = 0; i < Plates.Num(); ++i)
    {
        InitialPlateCentroids[i] = Plates[i].Centroid;
    }

    // Milestone 4 Task 2.1: Generate hotspots (deterministic from seed)
    GenerateHotspots();

    // Milestone 4 Task 1.2: Clear topology event log
    TopologyEvents.Empty();

    // Milestone 5: Initialize per-vertex arrays to match render vertex count
    // This ensures arrays are properly sized even when feature flags are disabled
    const int32 VertexCount = RenderVertices.Num();
    VertexElevationValues.SetNumZeroed(VertexCount);
    VertexErosionRates.SetNumZeroed(VertexCount);
    VertexSedimentThickness.SetNumZeroed(VertexCount);
    VertexCrustAge.SetNumZeroed(VertexCount);

    // Milestone 6 Task 2.1: Initialize amplification arrays
    VertexRidgeDirections.SetNum(VertexCount);
    for (int32 i = 0; i < VertexCount; ++i)
    {
        VertexRidgeDirections[i] = FVector3d::ZAxisVector; // Default fallback direction
    }
    VertexAmplifiedElevation.SetNumZeroed(VertexCount);

    // M5 Phase 3 fix: Seed elevation baselines from plate crust type for order independence
    // This ensures oceanic dampening/erosion work regardless of execution order
    int32 OceanicCount = 0, ContinentalCount = 0;
    for (int32 VertexIdx = 0; VertexIdx < VertexCount; ++VertexIdx)
    {
        const int32 PlateIdx = VertexPlateAssignments.IsValidIndex(VertexIdx) ? VertexPlateAssignments[VertexIdx] : INDEX_NONE;
        if (PlateIdx != INDEX_NONE && Plates.IsValidIndex(PlateIdx))
        {
            const bool bIsOceanic = (Plates[PlateIdx].CrustType == ECrustType::Oceanic);
            if (bIsOceanic)
            {
                /**
                 * Paper-compliant oceanic baseline (Appendix A):
                 * - Abyssal plains: -6000m (zᵇ)
                 * - Ridges: -1000m (zᵀ)
                 * Initialize to abyssal depth; ridges will form at divergent boundaries via oceanic crust generation.
                 * Age-subsidence formula will deepen crust from ridge depth toward abyssal depth over time.
                 */
                VertexElevationValues[VertexIdx] = PaperElevationConstants::AbyssalPlainDepth_m;
                OceanicCount++;
            }
            else
            {
                /**
                 * Paper-compliant continental baseline (Appendix A):
                 * Continents start at sea level (0m) and rise via subduction uplift, collision, and erosion.
                 * The +250m offset used previously compressed the achievable relief range.
                 */
                VertexElevationValues[VertexIdx] = PaperElevationConstants::ContinentalBaseline_m;
                ContinentalCount++;
            }
        }
    }
    UE_LOG(LogTemp, Warning, TEXT("[DEBUG] ResetSimulation: Initialized %d vertices (%d oceanic @ -6000m, %d continental @ 0m)"),
        VertexCount, OceanicCount, ContinentalCount);

    // DEBUG: Check vertex 0 specifically
    if (VertexCount > 0 && VertexPlateAssignments.Num() > 0)
    {
        const int32 Plate0 = VertexPlateAssignments[0];
        const double Elev0 = VertexElevationValues[0];
        const bool bOceanic0 = (Plate0 != INDEX_NONE && Plates.IsValidIndex(Plate0)) ?
            (Plates[Plate0].CrustType == ECrustType::Oceanic) : false;
        UE_LOG(LogTemp, Warning, TEXT("[DEBUG] Vertex 0: Plate=%d, Oceanic=%s, Elevation=%.2f m"),
            Plate0, bOceanic0 ? TEXT("YES") : TEXT("NO"), Elev0);
    }

    // Milestone 5 Task 1.3: Initialize history stack with initial state
    HistoryStack.Empty();
    CurrentHistoryIndex = -1;
    CaptureHistorySnapshot();
    UE_LOG(LogTemp, Log, TEXT("ResetSimulation: History stack initialized with initial state"));
}

void UTectonicSimulationService::AdvanceSteps(int32 StepCount)
{
    if (StepCount <= 0)
    {
        return;
    }

    // Milestone 3 Task 4.5: Track step performance
    const double StartTime = FPlatformTime::Seconds();

    constexpr double StepDurationMy = 2.0; // Paper defines delta t = 2 My per iteration

    for (int32 Step = 0; Step < StepCount; ++Step)
    {
        // Phase 2 Task 4: Migrate plate centroids via Euler pole rotation
        MigratePlateCentroids(StepDurationMy);

        // Milestone 6 Task 1.2: Update terrane positions (migrate with carrier plates)
        UpdateTerranePositions(StepDurationMy);

        // Phase 2 Task 5: Update boundary classifications based on relative velocities
        UpdateBoundaryClassifications();

        // Milestone 6 Task 1.2: Detect terrane collisions (after boundary updates)
        DetectTerraneCollisions();

        // Milestone 6 Task 1.3: Automatically reattach colliding terranes (after collision detection)
        ProcessTerraneReattachments();

        // Milestone 3 Task 2.3: Update stress at boundaries (cosmetic visualization)
        UpdateBoundaryStress(StepDurationMy);

        // Milestone 4 Task 1.3: Update boundary lifecycle states (Nascent/Active/Dormant)
        UpdateBoundaryStates(StepDurationMy);

        // Milestone 4 Task 2.2: Update rift progression for divergent boundaries
        UpdateRiftProgression(StepDurationMy);

        // Milestone 4 Task 2.1: Update hotspot drift in mantle frame
        UpdateHotspotDrift(StepDurationMy);

        CurrentTimeMy += StepDurationMy;

        // Milestone 3 Task 2.3: Interpolate stress to render vertices (per step for accurate snapshots)
        InterpolateStressToVertices();

        // Milestone 4 Task 2.3: Compute thermal field from hotspots + subduction
        ComputeThermalField();

        // Milestone 4 Task 2.1: Apply hotspot thermal contribution to stress field
        ApplyHotspotThermalContribution();

        // Milestone 5 Task 2.1: Apply continental erosion
        ApplyContinentalErosion(StepDurationMy);

        // Milestone 5 Task 2.2: Redistribute eroded sediment
        ApplySedimentTransport(StepDurationMy);

        // Milestone 5 Task 2.3: Apply oceanic dampening
        ApplyOceanicDampening(StepDurationMy);

        // Milestone 6 Task 2.1: Apply Stage B oceanic amplification
        // Must run after erosion/dampening to use base elevations as input
        // Must run before topology changes to ensure valid vertex indices
        if (Parameters.bEnableOceanicAmplification && Parameters.RenderSubdivisionLevel >= Parameters.MinAmplificationLOD)
        {
            ComputeRidgeDirections();
            ApplyOceanicAmplification();
        }

        // Milestone 6 Task 2.2: Apply Stage B continental amplification (exemplar-based)
        if (Parameters.bEnableContinentalAmplification && Parameters.RenderSubdivisionLevel >= Parameters.MinAmplificationLOD)
        {
            ApplyContinentalAmplification();
        }

        // Milestone 4 Task 1.2: Detect and execute plate splits/merges
        if (Parameters.bEnablePlateTopologyChanges)
        {
            DetectAndExecutePlateSplits();
            DetectAndExecutePlateMerges();
        }

        // Milestone 4 Task 1.1: Perform re-tessellation if plates have drifted beyond threshold
        if (Parameters.bEnableDynamicRetessellation)
        {
            PerformRetessellation();
        }
        else
        {
            // M3 compatibility: Just check and log (don't rebuild)
            CheckRetessellationNeeded();
        }

        // Milestone 5 Task 1.3: Capture history snapshot after each individual step for undo/redo
        CaptureHistorySnapshot();

        // Bump surface data version after the snapshot so caches and history stay in sync.
        SurfaceDataVersion++;
    }

    // Milestone 3 Task 4.5: Record step time for UI display
    const double EndTime = FPlatformTime::Seconds();
    LastStepTimeMs = (EndTime - StartTime) * 1000.0;
}

void UTectonicSimulationService::SetParameters(const FTectonicSimulationParameters& NewParams)
{
    if (Parameters.bEnableHeightmapVisualization != NewParams.bEnableHeightmapVisualization)
    {
        FTectonicSimulationParameters ComparableParams = NewParams;
        ComparableParams.bEnableHeightmapVisualization = Parameters.bEnableHeightmapVisualization;

        if (FMemory::Memcmp(&ComparableParams, &Parameters, sizeof(FTectonicSimulationParameters)) == 0)
        {
            SetHeightmapVisualizationEnabled(NewParams.bEnableHeightmapVisualization);
            return;
        }
    }

    Parameters = NewParams;

    // M5 Phase 3: Validate and clamp PlanetRadius to prevent invalid simulations
    const double MinRadius = 10000.0;   // 10 km (minimum for asteroid-like bodies)
    const double MaxRadius = 10000000.0; // 10,000 km (smaller than Jupiter)

    if (Parameters.PlanetRadius < MinRadius || Parameters.PlanetRadius > MaxRadius)
    {
        UE_LOG(LogTemp, Warning, TEXT("PlanetRadius %.0f m outside valid range [%.0f, %.0f]. Clamping to valid range."),
            Parameters.PlanetRadius, MinRadius, MaxRadius);
        Parameters.PlanetRadius = FMath::Clamp(Parameters.PlanetRadius, MinRadius, MaxRadius);
    }

    ResetSimulation();
}

void UTectonicSimulationService::SetHeightmapVisualizationEnabled(bool bEnabled)
{
    if (Parameters.bEnableHeightmapVisualization == bEnabled)
    {
        return;
    }

    Parameters.bEnableHeightmapVisualization = bEnabled;

    // Increment surface data version so cached LODs rebuild with updated vertex colors.
    SurfaceDataVersion++;

    UE_LOG(LogTemp, Log, TEXT("[Visualization] Heightmap visualization %s (SurfaceVersion=%d)"),
        bEnabled ? TEXT("enabled") : TEXT("disabled"), SurfaceDataVersion);
}

void UTectonicSimulationService::SetRenderSubdivisionLevel(int32 NewLevel)
{
    // Milestone 4 Phase 4.1: Update only the render subdivision level without resetting simulation
    // This preserves all tectonic state (plates, stress, rifts, hotspots, etc.) while changing LOD

    if (Parameters.RenderSubdivisionLevel == NewLevel)
    {
        return; // No change needed
    }

    UE_LOG(LogTemp, Log, TEXT("[LOD] Updating render subdivision level: L%d → L%d (preserving simulation state)"),
        Parameters.RenderSubdivisionLevel, NewLevel);

    // Update parameter
    Parameters.RenderSubdivisionLevel = NewLevel;

    // Regenerate only the render mesh at new subdivision level
    GenerateRenderMesh();

    // BUG FIX: Resize all per-vertex arrays to match the new render mesh size.
    const int32 VertexCount = RenderVertices.Num();
    VertexPlateAssignments.SetNum(VertexCount);
    VertexVelocities.SetNum(VertexCount);
    VertexStressValues.SetNumZeroed(VertexCount);
    VertexElevationValues.SetNumZeroed(VertexCount);
    VertexErosionRates.SetNumZeroed(VertexCount);
    VertexSedimentThickness.SetNumZeroed(VertexCount);
    VertexCrustAge.SetNumZeroed(VertexCount);
    VertexRidgeDirections.SetNum(VertexCount);
    VertexAmplifiedElevation.SetNumZeroed(VertexCount);
    VertexTemperatureValues.SetNumZeroed(VertexCount);

    // Rebuild Voronoi mapping (render vertices changed, but plate centroids unchanged)
    BuildVoronoiMapping();

    // Recompute velocity field for new render vertices
    ComputeVelocityField();

    // Reinterpolate stress field to new render vertices
    InterpolateStressToVertices();

    // Milestone 4 Task 2.3: Recompute thermal field for new render vertices
    ComputeThermalField();

    UE_LOG(LogTemp, Log, TEXT("[LOD] Render mesh regenerated at L%d: %d vertices, %d triangles"),
        NewLevel, RenderVertices.Num(), RenderTriangles.Num() / 3);
}

void UTectonicSimulationService::SetAutomaticLODEnabled(bool bEnabled)
{
    if (Parameters.bEnableAutomaticLOD == bEnabled)
    {
        return;
    }

    Parameters.bEnableAutomaticLOD = bEnabled;

    UE_LOG(LogTemp, Log, TEXT("[LOD] Automatic LOD %s"), bEnabled ? TEXT("enabled") : TEXT("disabled"));
}

void UTectonicSimulationService::GenerateDefaultSphereSamples()
{
    BaseSphereSamples.Reset();

    // Minimal placeholder: an octahedron on the unit sphere
    const TArray<FVector3d> SampleSeeds = {
        FVector3d(1.0, 0.0, 0.0),
        FVector3d(-1.0, 0.0, 0.0),
        FVector3d(0.0, 1.0, 0.0),
        FVector3d(0.0, -1.0, 0.0),
        FVector3d(0.0, 0.0, 1.0),
        FVector3d(0.0, 0.0, -1.0)
    };

    BaseSphereSamples.Reserve(SampleSeeds.Num());
    for (const FVector3d& Seed : SampleSeeds)
    {
        BaseSphereSamples.Add(Seed.GetSafeNormal());
    }
}

void UTectonicSimulationService::GenerateIcospherePlates()
{
    Plates.Reset();
    SharedVertices.Reset();

    // Phase 1 Task 1: Generate icosphere subdivision to approximate desired plate count
    // An icosahedron has 20 faces. Each subdivision level quadruples face count:
    // Level 0: 20 faces (baseline from paper, ~Earth's 7-15 plates)
    // Level 1: 80 faces (experimental high-resolution mode)
    // Level 2: 320 faces (experimental ultra-high resolution)
    // Level 3: 1280 faces (experimental maximum resolution)

    const int32 SubdivisionLevel = FMath::Clamp(Parameters.SubdivisionLevel, 0, 3);
    SubdivideIcosphere(SubdivisionLevel);

    // Assign plate IDs and initialize properties
    FRandomStream RNG(Parameters.Seed);
    const int32 NumPlates = Plates.Num();

    // Deterministic 70/30 oceanic/continental split (M5 Phase 3 fix)
    // Pre-compute desired oceanic count to guarantee mix even with unlucky seeds
    const int32 DesiredOceanic = FMath::RoundToInt(NumPlates * 0.7);

    // Create shuffled index array for randomness
    TArray<int32> PlateIndices;
    for (int32 i = 0; i < NumPlates; ++i)
    {
        PlateIndices.Add(i);
    }

    // Shuffle with seeded RNG for deterministic but varied assignments
    for (int32 i = NumPlates - 1; i > 0; --i)
    {
        const int32 j = RNG.RandRange(0, i);
        PlateIndices.Swap(i, j);
    }

    for (int32 i = 0; i < NumPlates; ++i)
    {
        FTectonicPlate& Plate = Plates[i];
        Plate.PlateID = i;

        // Calculate centroid from vertices
        FVector3d CentroidSum = FVector3d::ZeroVector;
        for (int32 VertexIdx : Plate.VertexIndices)
        {
            CentroidSum += SharedVertices[VertexIdx];
        }
        Plate.Centroid = (CentroidSum / static_cast<double>(Plate.VertexIndices.Num())).GetSafeNormal();

        // Assign crust type: 70% oceanic, 30% continental (deterministic)
        // First DesiredOceanic plates in shuffled order become oceanic
        const bool bIsOceanic = PlateIndices[i] < DesiredOceanic;
        Plate.CrustType = bIsOceanic ? ECrustType::Oceanic : ECrustType::Continental;
        Plate.CrustThickness = bIsOceanic ? 7.0 : 35.0; // Oceanic ~7km, Continental ~35km
    }

    UE_LOG(LogTemp, Log, TEXT("Generated %d plates from icosphere subdivision level %d"), NumPlates, SubdivisionLevel);
}

void UTectonicSimulationService::SubdivideIcosphere(int32 SubdivisionLevel)
{
    // Golden ratio for icosahedron vertex positioning
    const double Phi = (1.0 + FMath::Sqrt(5.0)) / 2.0;
    const double InvNorm = 1.0 / FMath::Sqrt(1.0 + Phi * Phi);

    // Base icosahedron vertices (12 vertices)
    TArray<FVector3d> Vertices = {
        FVector3d(-1,  Phi, 0).GetSafeNormal(),
        FVector3d( 1,  Phi, 0).GetSafeNormal(),
        FVector3d(-1, -Phi, 0).GetSafeNormal(),
        FVector3d( 1, -Phi, 0).GetSafeNormal(),
        FVector3d(0, -1,  Phi).GetSafeNormal(),
        FVector3d(0,  1,  Phi).GetSafeNormal(),
        FVector3d(0, -1, -Phi).GetSafeNormal(),
        FVector3d(0,  1, -Phi).GetSafeNormal(),
        FVector3d( Phi, 0, -1).GetSafeNormal(),
        FVector3d( Phi, 0,  1).GetSafeNormal(),
        FVector3d(-Phi, 0, -1).GetSafeNormal(),
        FVector3d(-Phi, 0,  1).GetSafeNormal()
    };

    // Base icosahedron faces (20 triangular faces)
    // Using right-hand winding order (counter-clockwise when viewed from outside)
    TArray<TArray<int32>> Faces = {
        {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
        {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
        {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
        {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}
    };

    // Subdivide icosphere to requested level (reuse same logic as GenerateRenderMesh)
    for (int32 Level = 0; Level < SubdivisionLevel; ++Level)
    {
        TArray<TArray<int32>> NewFaces;
        TMap<TPair<int32, int32>, int32> MidpointCache;

        for (const TArray<int32>& Face : Faces)
        {
            // Get or create midpoints for each edge
            const int32 V0 = Face[0];
            const int32 V1 = Face[1];
            const int32 V2 = Face[2];

            const int32 A = GetMidpointIndex(V0, V1, MidpointCache, Vertices);
            const int32 B = GetMidpointIndex(V1, V2, MidpointCache, Vertices);
            const int32 C = GetMidpointIndex(V2, V0, MidpointCache, Vertices);

            // Split triangle into 4 smaller triangles
            NewFaces.Add({V0, A, C});
            NewFaces.Add({V1, B, A});
            NewFaces.Add({V2, C, B});
            NewFaces.Add({A, B, C});
        }

        Faces = MoveTemp(NewFaces);
    }

    // Store vertices in shared pool
    SharedVertices = Vertices;

    // Create one plate per face
    Plates.Reserve(Faces.Num());
    for (const TArray<int32>& Face : Faces)
    {
        FTectonicPlate Plate;
        Plate.VertexIndices = Face;
        Plates.Add(Plate);
    }
}

void UTectonicSimulationService::InitializeEulerPoles()
{
    // Phase 1 Task 2: Assign deterministic Euler poles to each plate
    FRandomStream RNG(Parameters.Seed + 1); // Offset seed for pole generation

    for (FTectonicPlate& Plate : Plates)
    {
        // Generate random axis on unit sphere
        const double Theta = RNG.FRand() * 2.0 * PI;
        const double Phi = FMath::Acos(2.0 * RNG.FRand() - 1.0);

        Plate.EulerPoleAxis = FVector3d(
            FMath::Sin(Phi) * FMath::Cos(Theta),
            FMath::Sin(Phi) * FMath::Sin(Theta),
            FMath::Cos(Phi)
        ).GetSafeNormal();

        // Angular velocity: random magnitude 0.01-0.1 radians per My (realistic tectonic speeds)
        // Earth's plates move ~1-10 cm/year → ~0.01-0.1 radians/My on Earth-scale sphere
        Plate.AngularVelocity = RNG.FRandRange(0.01, 0.1);
    }

    UE_LOG(LogTemp, Log, TEXT("Initialized Euler poles for %d plates"), Plates.Num());
}

void UTectonicSimulationService::BuildBoundaryAdjacencyMap()
{
    // Phase 1 Task 3: Build adjacency map from shared edges in icosphere topology
    Boundaries.Reset();

    // For each pair of plates, check if they share an edge
    for (int32 i = 0; i < Plates.Num(); ++i)
    {
        const FTectonicPlate& PlateA = Plates[i];

        for (int32 j = i + 1; j < Plates.Num(); ++j)
        {
            const FTectonicPlate& PlateB = Plates[j];

            // Find shared vertices between the two plates
            TArray<int32> SharedVerts;
            for (int32 VertA : PlateA.VertexIndices)
            {
                if (PlateB.VertexIndices.Contains(VertA))
                {
                    SharedVerts.Add(VertA);
                }
            }

            // If exactly 2 shared vertices, they form a boundary edge
            if (SharedVerts.Num() == 2)
            {
                FPlateBoundary Boundary;
                Boundary.SharedEdgeVertices = SharedVerts;
                Boundary.BoundaryType = EBoundaryType::Transform; // Default, updated in Phase 2

                // Store with sorted plate IDs as key
                const TPair<int32, int32> Key(PlateA.PlateID, PlateB.PlateID);
                Boundaries.Add(Key, Boundary);
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("Built boundary adjacency map with %d boundaries"), Boundaries.Num());
}

void UTectonicSimulationService::ValidateSolidAngleCoverage()
{
    // Phase 1 Task 1: Validate that plates cover the sphere (total solid angle ≈ 4π steradians)
    double TotalSolidAngle = 0.0;

    for (const FTectonicPlate& Plate : Plates)
    {
        // Approximate solid angle using spherical triangle formula (for triangular plates)
        // Ω = E (spherical excess), where E = A + B + C - π for triangle with angles A,B,C
        // For simplicity, use area approximation: Ω ≈ Area / R² (R=1 for unit sphere)

        if (Plate.VertexIndices.Num() == 3)
        {
            const FVector3d& V0 = SharedVertices[Plate.VertexIndices[0]];
            const FVector3d& V1 = SharedVertices[Plate.VertexIndices[1]];
            const FVector3d& V2 = SharedVertices[Plate.VertexIndices[2]];

            // Spherical excess formula: E = A + B + C - π
            // Using l'Huilier's theorem for numerical stability
            const double A = FMath::Acos(FVector3d::DotProduct(V1, V2));
            const double B = FMath::Acos(FVector3d::DotProduct(V2, V0));
            const double C = FMath::Acos(FVector3d::DotProduct(V0, V1));
            const double S = (A + B + C) / 2.0;

            const double TanQuarter = FMath::Sqrt(
                FMath::Tan(S / 2.0) *
                FMath::Tan((S - A) / 2.0) *
                FMath::Tan((S - B) / 2.0) *
                FMath::Tan((S - C) / 2.0)
            );

            const double SolidAngle = 4.0 * FMath::Atan(TanQuarter);
            TotalSolidAngle += SolidAngle;
        }
    }

    const double ExpectedSolidAngle = 4.0 * PI;
    const double Error = FMath::Abs(TotalSolidAngle - ExpectedSolidAngle) / ExpectedSolidAngle;

    UE_LOG(LogTemp, Log, TEXT("Solid angle validation: Total=%.6f, Expected=%.6f (4π), Error=%.4f%%"),
        TotalSolidAngle, ExpectedSolidAngle, Error * 100.0);

    if (Error > 0.01) // 1% tolerance
    {
        UE_LOG(LogTemp, Warning, TEXT("Solid angle coverage error exceeds 1%% tolerance"));
    }
}

void UTectonicSimulationService::MigratePlateCentroids(double DeltaTimeMy)
{
    // Phase 2 Task 4: Apply Euler pole rotation to each plate centroid
    // Rotation formula: v' = v + ω × v * Δt (for small angles)
    // For accuracy, use Rodrigues' rotation formula: v' = v*cos(θ) + (k × v)*sin(θ) + k*(k·v)*(1-cos(θ))
    // where k = normalized Euler pole axis, θ = angular velocity * Δt

    for (FTectonicPlate& Plate : Plates)
    {
        const double RotationAngle = Plate.AngularVelocity * DeltaTimeMy; // radians

        // Rodrigues' rotation formula
        const FVector3d& Axis = Plate.EulerPoleAxis; // Already normalized
        const FVector3d& V = Plate.Centroid;

        const double CosTheta = FMath::Cos(RotationAngle);
        const double SinTheta = FMath::Sin(RotationAngle);
        const double DotProduct = FVector3d::DotProduct(Axis, V);

        const FVector3d RotatedCentroid =
            V * CosTheta +
            FVector3d::CrossProduct(Axis, V) * SinTheta +
            Axis * DotProduct * (1.0 - CosTheta);

        Plate.Centroid = RotatedCentroid.GetSafeNormal(); // Keep on unit sphere

        // Log displacement for first few plates (debug)
        if (Plate.PlateID < 3)
        {
            const double DisplacementRadians = FMath::Acos(FMath::Clamp(FVector3d::DotProduct(V, Plate.Centroid), -1.0, 1.0));
            UE_LOG(LogTemp, VeryVerbose, TEXT("Plate %d displaced by %.6f radians (%.2f km on Earth-scale)"),
                Plate.PlateID, DisplacementRadians, DisplacementRadians * 6370.0);
        }
    }
}

void UTectonicSimulationService::UpdateBoundaryClassifications()
{
    // Phase 2 Task 5: Classify boundaries based on relative plate velocities
    // Velocity at a point on plate = ω × r (angular velocity cross radius vector)
    // For boundary between plates A and B, compute velocities at boundary midpoint

    // Helper: Apply Rodrigues rotation to a vertex (shared with visualization)
    auto RotateVertex = [](const FVector3d& Vertex, const FVector3d& EulerPoleAxis, double TotalRotationAngle) -> FVector3d
    {
        const double CosTheta = FMath::Cos(TotalRotationAngle);
        const double SinTheta = FMath::Sin(TotalRotationAngle);
        const double DotProduct = FVector3d::DotProduct(EulerPoleAxis, Vertex);

        const FVector3d Rotated =
            Vertex * CosTheta +
            FVector3d::CrossProduct(EulerPoleAxis, Vertex) * SinTheta +
            EulerPoleAxis * DotProduct * (1.0 - CosTheta);

        return Rotated.GetSafeNormal();
    };

    int32 DivergentCount = 0;
    int32 ConvergentCount = 0;
    int32 TransformCount = 0;

    for (auto& BoundaryPair : Boundaries)
    {
        const TPair<int32, int32>& PlateIDs = BoundaryPair.Key;
        FPlateBoundary& Boundary = BoundaryPair.Value;

        // Find the two plates
        const FTectonicPlate* PlateA = Plates.FindByPredicate([ID = PlateIDs.Key](const FTectonicPlate& P) { return P.PlateID == ID; });
        const FTectonicPlate* PlateB = Plates.FindByPredicate([ID = PlateIDs.Value](const FTectonicPlate& P) { return P.PlateID == ID; });

        if (!PlateA || !PlateB || Boundary.SharedEdgeVertices.Num() != 2)
        {
            continue;
        }

        // Rotate boundary vertices to current simulation time for accurate classification
        const FVector3d& V0_Original = SharedVertices[Boundary.SharedEdgeVertices[0]];
        const FVector3d& V1_Original = SharedVertices[Boundary.SharedEdgeVertices[1]];

        const double RotationAngleA = PlateA->AngularVelocity * CurrentTimeMy;
        const double RotationAngleB = PlateB->AngularVelocity * CurrentTimeMy;

        const FVector3d V0_FromA = RotateVertex(V0_Original, PlateA->EulerPoleAxis, RotationAngleA);
        const FVector3d V1_FromA = RotateVertex(V1_Original, PlateA->EulerPoleAxis, RotationAngleA);
        const FVector3d V0_FromB = RotateVertex(V0_Original, PlateB->EulerPoleAxis, RotationAngleB);
        const FVector3d V1_FromB = RotateVertex(V1_Original, PlateB->EulerPoleAxis, RotationAngleB);

        // Average both plate rotations so the midpoint stays between drifting plates.
        const FVector3d V0_Current = ((V0_FromA + V0_FromB) * 0.5).GetSafeNormal();
        const FVector3d V1_Current = ((V1_FromA + V1_FromB) * 0.5).GetSafeNormal();

        if (V0_Current.IsNearlyZero() || V1_Current.IsNearlyZero())
        {
            continue;
        }

        const FVector3d BoundaryMidpoint = ((V0_Current + V1_Current) * 0.5).GetSafeNormal();
        if (BoundaryMidpoint.IsNearlyZero())
        {
            continue;
        }

        // Compute velocity at boundary for each plate: v = ω × r
        // ω is the angular velocity vector = AngularVelocity * EulerPoleAxis
        const FVector3d OmegaA = PlateA->EulerPoleAxis * PlateA->AngularVelocity;
        const FVector3d OmegaB = PlateB->EulerPoleAxis * PlateB->AngularVelocity;

        const FVector3d VelocityA = FVector3d::CrossProduct(OmegaA, BoundaryMidpoint);
        const FVector3d VelocityB = FVector3d::CrossProduct(OmegaB, BoundaryMidpoint);

        // Relative velocity: vRel = vA - vB
        const FVector3d RelativeVelocity = VelocityA - VelocityB;
        Boundary.RelativeVelocity = RelativeVelocity.Length();

        // Build a boundary normal that is tangent to the sphere and consistently oriented.
        const FVector3d EdgeVector = (V1_Current - V0_Current).GetSafeNormal();
        if (EdgeVector.IsNearlyZero())
        {
            continue;
        }

        // Project Plate A's centroid direction onto the tangent plane so the sign check
        // is unaffected by radial components (which would otherwise flip classification).
        const FVector3d PlateATangent = (PlateA->Centroid - FVector3d::DotProduct(PlateA->Centroid, BoundaryMidpoint) * BoundaryMidpoint);

        FVector3d BoundaryNormal = FVector3d::CrossProduct(BoundaryMidpoint, EdgeVector);

        if (!BoundaryNormal.Normalize())
        {
            continue; // Degenerate geometry, skip classification this frame
        }

        FVector3d PlateTangentNormalized = PlateATangent;
        const bool bHasPlateTangent = PlateTangentNormalized.Normalize();

        // Ensure the normal points toward Plate A's side of the boundary so the
        // dot(RelativeVelocity, BoundaryNormal) sign matches physical intuition.
        if (bHasPlateTangent && FVector3d::DotProduct(BoundaryNormal, PlateTangentNormalized) < 0.0)
        {
            BoundaryNormal *= -1.0;
        }

        // Project relative velocity onto boundary normal
        const double NormalComponent = FVector3d::DotProduct(RelativeVelocity, BoundaryNormal);

        // Classify boundary
        const double ClassificationThreshold = 0.001; // Radians/My threshold
        if (NormalComponent > ClassificationThreshold)
        {
            Boundary.BoundaryType = EBoundaryType::Divergent; // Plates separating
            DivergentCount++;
        }
        else if (NormalComponent < -ClassificationThreshold)
        {
            Boundary.BoundaryType = EBoundaryType::Convergent; // Plates colliding
            ConvergentCount++;
        }
        else
        {
            Boundary.BoundaryType = EBoundaryType::Transform; // Shear/parallel motion
            TransformCount++;
        }

    }

    UE_LOG(LogTemp, VeryVerbose, TEXT("Boundary classification: %d divergent, %d convergent, %d transform"),
        DivergentCount, ConvergentCount, TransformCount);
}

void UTectonicSimulationService::ExportMetricsToCSV()
{
    // Phase 4 Task 9: Export simulation metrics to CSV for validation and analysis

    // Create output directory if it doesn't exist
    const FString OutputDir = FPaths::ProjectSavedDir() / TEXT("TectonicMetrics");
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.DirectoryExists(*OutputDir))
    {
        PlatformFile.CreateDirectory(*OutputDir);
    }

    // Generate timestamped filename
    const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
    const FString Filename = FString::Printf(TEXT("TectonicMetrics_Seed%d_Step%d_%s.csv"),
        Parameters.Seed,
        static_cast<int32>(CurrentTimeMy / 2.0), // Step count
        *Timestamp);
    const FString FilePath = OutputDir / Filename;

    // Build CSV content
    TArray<FString> CSVLines;

    // Milestone 4 Task 1.3/2.1/2.2/2.3: CSV v3.0 schema version header
    CSVLines.Add(TEXT("# Planetary Creation Tectonic Simulation Metrics"));
    CSVLines.Add(TEXT("# CSV Schema Version: 3.0"));
    CSVLines.Add(TEXT("# Changes from v2.0: Added BoundaryState, StateTransitionTime, DivergentDuration, ConvergentDuration, ThermalFlux (boundary section)"));
    CSVLines.Add(TEXT("#                     Added TopologyEvents section (splits/merges)"));
    CSVLines.Add(TEXT("#                     Added Hotspots section (Task 2.1: positions, thermal output, drift)"));
    CSVLines.Add(TEXT("#                     Added RiftWidth, RiftAge columns (Task 2.2: rift progression tracking)"));
    CSVLines.Add(TEXT("#                     Added TemperatureK column (Task 2.3: analytic thermal field from hotspots + subduction)"));
    CSVLines.Add(TEXT("# Backward compatible: v2.0 readers will ignore new columns"));
    CSVLines.Add(TEXT(""));

    // Header
    CSVLines.Add(TEXT("PlateID,CentroidX,CentroidY,CentroidZ,CrustType,CrustThickness,EulerPoleAxisX,EulerPoleAxisY,EulerPoleAxisZ,AngularVelocity"));

    // Plate data
    for (const FTectonicPlate& Plate : Plates)
    {
        const FString CrustTypeName = (Plate.CrustType == ECrustType::Oceanic) ? TEXT("Oceanic") : TEXT("Continental");

        CSVLines.Add(FString::Printf(TEXT("%d,%.8f,%.8f,%.8f,%s,%.2f,%.8f,%.8f,%.8f,%.8f"),
            Plate.PlateID,
            Plate.Centroid.X, Plate.Centroid.Y, Plate.Centroid.Z,
            *CrustTypeName,
            Plate.CrustThickness,
            Plate.EulerPoleAxis.X, Plate.EulerPoleAxis.Y, Plate.EulerPoleAxis.Z,
            Plate.AngularVelocity
        ));
    }

    // Add separator and boundary data (CSV v3.0: added BoundaryState, StateTransitionTime, ThermalFlux, Task 2.2: RiftWidth, RiftAge)
    CSVLines.Add(TEXT("")); // Empty line
    CSVLines.Add(TEXT("PlateA_ID,PlateB_ID,BoundaryType,BoundaryState,StateTransitionTime_My,RelativeVelocity,AccumulatedStress_MPa,DivergentDuration_My,ConvergentDuration_My,ThermalFlux,RiftWidth_m,RiftAge_My"));

    for (const auto& BoundaryPair : Boundaries)
    {
        const TPair<int32, int32>& PlateIDs = BoundaryPair.Key;
        const FPlateBoundary& Boundary = BoundaryPair.Value;

        FString BoundaryTypeName;
        switch (Boundary.BoundaryType)
        {
            case EBoundaryType::Divergent:  BoundaryTypeName = TEXT("Divergent"); break;
            case EBoundaryType::Convergent: BoundaryTypeName = TEXT("Convergent"); break;
            case EBoundaryType::Transform:  BoundaryTypeName = TEXT("Transform"); break;
        }

        FString BoundaryStateName;
        switch (Boundary.BoundaryState)
        {
            case EBoundaryState::Nascent: BoundaryStateName = TEXT("Nascent"); break;
            case EBoundaryState::Active:  BoundaryStateName = TEXT("Active"); break;
            case EBoundaryState::Dormant: BoundaryStateName = TEXT("Dormant"); break;
            case EBoundaryState::Rifting: BoundaryStateName = TEXT("Rifting"); break; // Milestone 4 Task 2.2
        }

        // Milestone 4 Task 1.3: Thermal flux (placeholder - will be populated by Task 2.3)
        const double ThermalFlux = 0.0;

        // Milestone 4 Task 2.2: Rift width and age (only valid for rifting boundaries)
        const double RiftAgeMy = (Boundary.BoundaryState == EBoundaryState::Rifting && Boundary.RiftFormationTimeMy > 0.0)
            ? (CurrentTimeMy - Boundary.RiftFormationTimeMy)
            : 0.0;

        CSVLines.Add(FString::Printf(TEXT("%d,%d,%s,%s,%.2f,%.8f,%.2f,%.2f,%.2f,%.4f,%.0f,%.2f"),
            PlateIDs.Key, PlateIDs.Value,
            *BoundaryTypeName,
            *BoundaryStateName,
            Boundary.StateTransitionTimeMy,
            Boundary.RelativeVelocity,
            Boundary.AccumulatedStress,
            Boundary.DivergentDurationMy,
            Boundary.ConvergentDurationMy,
            ThermalFlux,
            Boundary.RiftWidthMeters,
            RiftAgeMy
        ));
    }

    // Add summary statistics
    CSVLines.Add(TEXT("")); // Empty line
    CSVLines.Add(TEXT("Metric,Value"));
    CSVLines.Add(FString::Printf(TEXT("SimulationTime_My,%.2f"), CurrentTimeMy));
    CSVLines.Add(FString::Printf(TEXT("PlateCount,%d"), Plates.Num()));
    CSVLines.Add(FString::Printf(TEXT("BoundaryCount,%d"), Boundaries.Num()));
    CSVLines.Add(FString::Printf(TEXT("Seed,%d"), Parameters.Seed));

    // Calculate total kinetic energy (for monitoring)
    double TotalKineticEnergy = 0.0;
    for (const FTectonicPlate& Plate : Plates)
    {
        // Simplified: KE ∝ ω² (ignoring moment of inertia for now)
        TotalKineticEnergy += Plate.AngularVelocity * Plate.AngularVelocity;
    }
    CSVLines.Add(FString::Printf(TEXT("TotalKineticEnergy,%.8f"), TotalKineticEnergy));

    // Count boundary types
    int32 DivergentCount = 0, ConvergentCount = 0, TransformCount = 0;
    for (const auto& BoundaryPair : Boundaries)
    {
        switch (BoundaryPair.Value.BoundaryType)
        {
            case EBoundaryType::Divergent:  DivergentCount++; break;
            case EBoundaryType::Convergent: ConvergentCount++; break;
            case EBoundaryType::Transform:  TransformCount++; break;
        }
    }
    CSVLines.Add(FString::Printf(TEXT("DivergentBoundaries,%d"), DivergentCount));
    CSVLines.Add(FString::Printf(TEXT("ConvergentBoundaries,%d"), ConvergentCount));
    CSVLines.Add(FString::Printf(TEXT("TransformBoundaries,%d"), TransformCount));

    // Milestone 4 Task 1.3: Export topology events (CSV v3.0)
    CSVLines.Add(TEXT("")); // Empty line
    CSVLines.Add(TEXT("EventType,OriginalPlateID,NewPlateID,Timestamp_My,StressAtEvent_MPa,VelocityAtEvent"));

    for (const FPlateTopologyEvent& Event : TopologyEvents)
    {
        FString EventTypeName;
        switch (Event.EventType)
        {
            case EPlateTopologyEventType::Split: EventTypeName = TEXT("Split"); break;
            case EPlateTopologyEventType::Merge: EventTypeName = TEXT("Merge"); break;
            default: EventTypeName = TEXT("None"); break;
        }

        // For split: [OriginalID, NewID], for merge: [ConsumedID, SurvivorID]
        const int32 PlateID1 = Event.PlateIDs.IsValidIndex(0) ? Event.PlateIDs[0] : INDEX_NONE;
        const int32 PlateID2 = Event.PlateIDs.IsValidIndex(1) ? Event.PlateIDs[1] : INDEX_NONE;

        CSVLines.Add(FString::Printf(TEXT("%s,%d,%d,%.2f,%.2f,%.8f"),
            *EventTypeName,
            PlateID1,
            PlateID2,
            Event.TimestampMy,
            Event.StressAtEvent,
            Event.VelocityAtEvent
        ));
    }

    if (TopologyEvents.Num() == 0)
    {
        CSVLines.Add(TEXT("# No topology events this simulation"));
    }

    // Milestone 4 Task 2.1: Export hotspot data
    CSVLines.Add(TEXT("")); // Empty line
    CSVLines.Add(TEXT("HotspotID,Type,PositionX,PositionY,PositionZ,ThermalOutput,InfluenceRadius_rad,DriftVelocityX,DriftVelocityY,DriftVelocityZ"));

    for (const FMantleHotspot& Hotspot : Hotspots)
    {
        FString TypeName;
        switch (Hotspot.Type)
        {
            case EHotspotType::Major: TypeName = TEXT("Major"); break;
            case EHotspotType::Minor: TypeName = TEXT("Minor"); break;
            default: TypeName = TEXT("Unknown"); break;
        }

        CSVLines.Add(FString::Printf(TEXT("%d,%s,%.8f,%.8f,%.8f,%.2f,%.6f,%.8f,%.8f,%.8f"),
            Hotspot.HotspotID,
            *TypeName,
            Hotspot.Position.X, Hotspot.Position.Y, Hotspot.Position.Z,
            Hotspot.ThermalOutput,
            Hotspot.InfluenceRadius,
            Hotspot.DriftVelocity.X, Hotspot.DriftVelocity.Y, Hotspot.DriftVelocity.Z
        ));
    }

    if (Hotspots.Num() == 0)
    {
        CSVLines.Add(TEXT("# No hotspots active (bEnableHotspots=false)"));
    }

    // Milestone 3: Export vertex-level data (stress, velocity, elevation, Task 2.3: temperature)
    CSVLines.Add(TEXT("")); // Empty line
    CSVLines.Add(TEXT("VertexIndex,PositionX,PositionY,PositionZ,PlateID,VelocityX,VelocityY,VelocityZ,VelocityMagnitude,StressMPa,ElevationMeters,TemperatureK"));

    const int32 MaxVerticesToExport = FMath::Min(RenderVertices.Num(), 1000); // Limit to first 1000 vertices for CSV size
    for (int32 i = 0; i < MaxVerticesToExport; ++i)
    {
        const FVector3d& Position = RenderVertices[i];
        const int32 PlateID = VertexPlateAssignments.IsValidIndex(i) ? VertexPlateAssignments[i] : INDEX_NONE;
        const FVector3d& Velocity = VertexVelocities.IsValidIndex(i) ? VertexVelocities[i] : FVector3d::ZeroVector;
        const double VelocityMag = Velocity.Length();
        const double StressMPa = VertexStressValues.IsValidIndex(i) ? VertexStressValues[i] : 0.0;
        const double ElevationMeters = VertexElevationValues.IsValidIndex(i)
            ? VertexElevationValues[i]
            : 0.0;
        const double TemperatureK = VertexTemperatureValues.IsValidIndex(i) ? VertexTemperatureValues[i] : 0.0; // Milestone 4 Task 2.3

        CSVLines.Add(FString::Printf(TEXT("%d,%.8f,%.8f,%.8f,%d,%.8f,%.8f,%.8f,%.8f,%.2f,%.2f,%.1f"),
            i,
            Position.X, Position.Y, Position.Z,
            PlateID,
            Velocity.X, Velocity.Y, Velocity.Z,
            VelocityMag,
            StressMPa,
            ElevationMeters,
            TemperatureK
        ));
    }

    if (RenderVertices.Num() > MaxVerticesToExport)
    {
        CSVLines.Add(FString::Printf(TEXT("# Note: Vertex data truncated to %d of %d vertices for CSV size"), MaxVerticesToExport, RenderVertices.Num()));
    }

    // Write to file
    const FString CSVContent = FString::Join(CSVLines, TEXT("\n"));
    if (FFileHelper::SaveStringToFile(CSVContent, *FilePath))
    {
        UE_LOG(LogTemp, Log, TEXT("Exported metrics to: %s"), *FilePath);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to export metrics to: %s"), *FilePath);
    }
}

// Milestone 3 Task 1.1: Generate high-density render mesh
void UTectonicSimulationService::GenerateRenderMesh()
{
    RenderVertices.Reset();
    RenderTriangles.Reset();

    // Start with base icosahedron vertices
    const double Phi = (1.0 + FMath::Sqrt(5.0)) / 2.0;
    TArray<FVector3d> Vertices = {
        FVector3d(-1,  Phi, 0).GetSafeNormal(),
        FVector3d( 1,  Phi, 0).GetSafeNormal(),
        FVector3d(-1, -Phi, 0).GetSafeNormal(),
        FVector3d( 1, -Phi, 0).GetSafeNormal(),
        FVector3d(0, -1,  Phi).GetSafeNormal(),
        FVector3d(0,  1,  Phi).GetSafeNormal(),
        FVector3d(0, -1, -Phi).GetSafeNormal(),
        FVector3d(0,  1, -Phi).GetSafeNormal(),
        FVector3d( Phi, 0, -1).GetSafeNormal(),
        FVector3d( Phi, 0,  1).GetSafeNormal(),
        FVector3d(-Phi, 0, -1).GetSafeNormal(),
        FVector3d(-Phi, 0,  1).GetSafeNormal()
    };

    // Base icosahedron faces (20 triangles)
    TArray<TArray<int32>> Faces = {
        {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
        {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
        {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
        {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}
    };

    // Subdivide based on RenderSubdivisionLevel
    const int32 SubdivLevel = FMath::Clamp(Parameters.RenderSubdivisionLevel, 0, 8);

    for (int32 Level = 0; Level < SubdivLevel; ++Level)
    {
        TArray<TArray<int32>> NewFaces;
        TMap<TPair<int32, int32>, int32> MidpointCache;

        for (const TArray<int32>& Face : Faces)
        {
            // Get or create midpoints for each edge
            const int32 V0 = Face[0];
            const int32 V1 = Face[1];
            const int32 V2 = Face[2];

            const int32 A = GetMidpointIndex(V0, V1, MidpointCache, Vertices);
            const int32 B = GetMidpointIndex(V1, V2, MidpointCache, Vertices);
            const int32 C = GetMidpointIndex(V2, V0, MidpointCache, Vertices);

            // Split triangle into 4 smaller triangles
            NewFaces.Add({V0, A, C});
            NewFaces.Add({V1, B, A});
            NewFaces.Add({V2, C, B});
            NewFaces.Add({A, B, C});
        }

        Faces = MoveTemp(NewFaces);
    }

    // Store final vertices and triangles
    RenderVertices = Vertices;
    RenderTriangles.Reserve(Faces.Num() * 3);

    for (const TArray<int32>& Face : Faces)
    {
        RenderTriangles.Add(Face[0]);
        RenderTriangles.Add(Face[1]);
        RenderTriangles.Add(Face[2]);
    }

    const int32 ExpectedFaceCount = 20 * FMath::Pow(4.0f, static_cast<float>(SubdivLevel));
    UE_LOG(LogTemp, Log, TEXT("Generated render mesh: Level %d, %d vertices, %d triangles (expected %d)"),
        SubdivLevel, RenderVertices.Num(), Faces.Num(), ExpectedFaceCount);

    // Validate Euler characteristic (V - E + F = 2)
    const int32 V = RenderVertices.Num();
    const int32 F = Faces.Num();
    const int32 E = (F * 3) / 2; // Each edge shared by 2 faces
    const int32 EulerChar = V - E + F;

    if (EulerChar != 2)
    {
        UE_LOG(LogTemp, Warning, TEXT("Render mesh Euler characteristic validation failed: V=%d, E=%d, F=%d, χ=%d (expected 2)"),
            V, E, F, EulerChar);
    }
    else
    {
        UE_LOG(LogTemp, Verbose, TEXT("Render mesh topology validated: Euler characteristic χ=2"));
    }
}

int32 UTectonicSimulationService::GetMidpointIndex(int32 V0, int32 V1, TMap<TPair<int32, int32>, int32>& MidpointCache, TArray<FVector3d>& Vertices)
{
    // Ensure consistent edge key ordering
    const TPair<int32, int32> EdgeKey = (V0 < V1) ? TPair<int32, int32>(V0, V1) : TPair<int32, int32>(V1, V0);

    // Check cache
    if (const int32* CachedIndex = MidpointCache.Find(EdgeKey))
    {
        return *CachedIndex;
    }

    // Create new midpoint vertex
    const FVector3d Midpoint = ((Vertices[V0] + Vertices[V1]) * 0.5).GetSafeNormal();
    const int32 NewIndex = Vertices.Add(Midpoint);
    MidpointCache.Add(EdgeKey, NewIndex);

    return NewIndex;
}

// Milestone 3 Task 2.1: Build Voronoi mapping
void UTectonicSimulationService::BuildVoronoiMapping()
{
    VertexPlateAssignments.Reset();

    if (RenderVertices.Num() == 0 || Plates.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("Cannot build Voronoi mapping: RenderVertices=%d, Plates=%d"),
            RenderVertices.Num(), Plates.Num());
        return;
    }

    const double StartTime = FPlatformTime::Seconds();

    // For small plate counts (N<50), brute force is faster than KD-tree
    // due to cache locality and no tree traversal overhead
    VertexPlateAssignments.SetNumUninitialized(RenderVertices.Num());

    // Milestone 4 Task 5.0: Voronoi warping parameters
    const bool bUseWarping = Parameters.bEnableVoronoiWarping;
    const double WarpAmplitude = Parameters.VoronoiWarpingAmplitude;
    const double WarpFrequency = Parameters.VoronoiWarpingFrequency;

    for (int32 i = 0; i < RenderVertices.Num(); ++i)
    {
        int32 ClosestPlateID = INDEX_NONE;
        double MinDistSq = TNumericLimits<double>::Max();

        const FVector3d& Vertex = RenderVertices[i];

        for (const FTectonicPlate& Plate : Plates)
        {
            double DistSq = FVector3d::DistSquared(Vertex, Plate.Centroid);

            // Milestone 4 Task 5.0: Apply noise warping to distance
            // "More irregular continent shapes can be obtained by warping the geodesic distances
            // to the centroids using a simple noise function." (Paper Section 3)
            if (bUseWarping && WarpAmplitude > SMALL_NUMBER)
            {
                // Use 3D Perlin noise seeded by vertex + plate centroid for per-plate variation
                // This ensures each plate boundary gets unique noise patterns
                const FVector NoiseInput = FVector((Vertex + Plate.Centroid) * WarpFrequency);
                const float NoiseValue = FMath::PerlinNoise3D(NoiseInput);

                // Warp distance: d' = d * (1 + amplitude * noise)
                // NoiseValue range [-1, 1], so warping range: [1 - amplitude, 1 + amplitude]
                const double WarpFactor = 1.0 + WarpAmplitude * static_cast<double>(NoiseValue);
                DistSq *= WarpFactor;
            }

            if (DistSq < MinDistSq)
            {
                MinDistSq = DistSq;
                ClosestPlateID = Plate.PlateID;
            }
        }

        VertexPlateAssignments[i] = ClosestPlateID;
    }

    const double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;

    UE_LOG(LogTemp, Log, TEXT("Built Voronoi mapping: %d vertices → %d plates in %.2f ms (avg %.3f μs per vertex)"),
        RenderVertices.Num(), Plates.Num(), ElapsedMs, (ElapsedMs * 1000.0) / RenderVertices.Num());

    // Validate all vertices assigned
    int32 UnassignedCount = 0;
    for (int32 PlateID : VertexPlateAssignments)
    {
        if (PlateID == INDEX_NONE)
        {
            UnassignedCount++;
        }
    }

    if (UnassignedCount > 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("Voronoi mapping incomplete: %d vertices unassigned"), UnassignedCount);
    }
}

void UTectonicSimulationService::TransferElevationFromPreviousMesh(const TArray<FVector3d>& OldVertices, const TArray<double>& OldElevations, const TArray<double>& OldAmplifiedElevations)
{
    const int32 NewVertexCount = RenderVertices.Num();

    if (NewVertexCount == 0)
    {
        VertexElevationValues.Reset();
        VertexAmplifiedElevation.Reset();
        return;
    }

    VertexElevationValues.SetNum(NewVertexCount);
    VertexAmplifiedElevation.SetNum(NewVertexCount);

    if (OldVertices.Num() == 0 || OldElevations.Num() != OldVertices.Num())
    {
        for (int32 VertexIdx = 0; VertexIdx < NewVertexCount; ++VertexIdx)
        {
            VertexElevationValues[VertexIdx] = 0.0;
            VertexAmplifiedElevation[VertexIdx] = 0.0;
        }

        UE_LOG(LogTemp, Warning, TEXT("[Re-tessellation] Missing prior elevation data; defaulting to 0 m for %d vertices."),
            NewVertexCount);
        return;
    }

    FSphericalKDTree KDTree;
    TArray<int32> PointIDs;
    PointIDs.Reserve(OldVertices.Num());
    for (int32 Index = 0; Index < OldVertices.Num(); ++Index)
    {
        PointIDs.Add(Index);
    }
    KDTree.Build(OldVertices, PointIDs);

    if (!KDTree.IsValid())
    {
        for (int32 VertexIdx = 0; VertexIdx < NewVertexCount; ++VertexIdx)
        {
            VertexElevationValues[VertexIdx] = 0.0;
            VertexAmplifiedElevation[VertexIdx] = 0.0;
        }
        UE_LOG(LogTemp, Warning, TEXT("[Re-tessellation] KD-tree build failed; elevations reset to 0 m for %d vertices."),
            NewVertexCount);
        return;
    }

    const int32 NeighborCount = FMath::Min(3, OldVertices.Num());
    const bool bHasAmplifiedData = (OldAmplifiedElevations.Num() == OldVertices.Num());

    TArray<int32> NeighborIDs;
    NeighborIDs.Reserve(NeighborCount);

    TArray<double> NeighborDistances;
    NeighborDistances.Reserve(NeighborCount);

    double MaxDeviation = 0.0;

    for (int32 VertexIdx = 0; VertexIdx < NewVertexCount; ++VertexIdx)
    {
        const FVector3d& NewVertex = RenderVertices[VertexIdx];

        NeighborIDs.Reset();
        NeighborDistances.Reset();
        KDTree.FindKNearest(NewVertex, NeighborCount, NeighborIDs, &NeighborDistances);

        double InterpolatedElevation = 0.0;
        double InterpolatedAmplified = 0.0;
        double WeightSum = 0.0;
        bool bUsedExactMatch = false;

        for (int32 NeighborIdx = 0; NeighborIdx < NeighborIDs.Num(); ++NeighborIdx)
        {
            const int32 OldIndex = NeighborIDs[NeighborIdx];
            if (!OldElevations.IsValidIndex(OldIndex))
            {
                continue;
            }

            const double DistSq = NeighborDistances.IsValidIndex(NeighborIdx)
                ? NeighborDistances[NeighborIdx]
                : FVector3d::DistSquared(NewVertex, OldVertices[OldIndex]);

            if (DistSq <= KINDA_SMALL_NUMBER)
            {
                InterpolatedElevation = OldElevations[OldIndex];
                InterpolatedAmplified = bHasAmplifiedData ? OldAmplifiedElevations[OldIndex] : OldElevations[OldIndex];
                bUsedExactMatch = true;
                WeightSum = 1.0;
                break;
            }

            const double Distance = FMath::Sqrt(DistSq);
            const double Weight = 1.0 / FMath::Max(Distance, 1e-6);

            InterpolatedElevation += OldElevations[OldIndex] * Weight;
            if (bHasAmplifiedData)
            {
                InterpolatedAmplified += OldAmplifiedElevations[OldIndex] * Weight;
            }
            else
            {
                InterpolatedAmplified += OldElevations[OldIndex] * Weight;
            }

            WeightSum += Weight;
        }

        if (!bUsedExactMatch)
        {
            if (WeightSum > 0.0)
            {
                InterpolatedElevation /= WeightSum;
                InterpolatedAmplified /= WeightSum;
            }
            else
            {
                InterpolatedElevation = 0.0;
                InterpolatedAmplified = InterpolatedElevation;
            }
        }

        VertexElevationValues[VertexIdx] = InterpolatedElevation;
        VertexAmplifiedElevation[VertexIdx] = InterpolatedAmplified;

        if (NeighborIDs.Num() > 0)
        {
            const int32 ClosestIndex = NeighborIDs[0];
            if (OldElevations.IsValidIndex(ClosestIndex))
            {
                MaxDeviation = FMath::Max(MaxDeviation, FMath::Abs(InterpolatedElevation - OldElevations[ClosestIndex]));
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("[Re-tessellation] Transferred elevations to %d vertices (max Δ=%.2f m)"),
        NewVertexCount, MaxDeviation);
}

void UTectonicSimulationService::ComputeVelocityField()
{
    // Milestone 3 Task 2.2: Compute per-vertex velocity v = ω × r
    // where ω = plate's angular velocity vector (EulerPoleAxis * AngularVelocity)
    // and r = vertex position on unit sphere

    VertexVelocities.SetNumUninitialized(RenderVertices.Num());

    for (int32 i = 0; i < RenderVertices.Num(); ++i)
    {
        const int32 PlateID = VertexPlateAssignments[i];
        if (PlateID == INDEX_NONE)
        {
            VertexVelocities[i] = FVector3d::ZeroVector;
            continue;
        }

        // Find the plate
        const FTectonicPlate* Plate = Plates.FindByPredicate([PlateID](const FTectonicPlate& P)
        {
            return P.PlateID == PlateID;
        });

        if (!Plate)
        {
            VertexVelocities[i] = FVector3d::ZeroVector;
            continue;
        }

        // Compute angular velocity vector: ω = axis * magnitude (rad/My)
        const FVector3d Omega = Plate->EulerPoleAxis * Plate->AngularVelocity;

        // Compute velocity: v = ω × r
        // Cross product gives tangent vector in direction of motion
        const FVector3d Velocity = FVector3d::CrossProduct(Omega, RenderVertices[i]);

        VertexVelocities[i] = Velocity;
    }
}

void UTectonicSimulationService::UpdateBoundaryStress(double DeltaTimeMy)
{
    // Milestone 3 Task 2.3: COSMETIC STRESS VISUALIZATION (simplified model)
    // NOT physically accurate - for visual effect only

    constexpr double MaxStressMPa = 100.0; // Cap at 100 MPa (~10km elevation equivalent)
    constexpr double DecayTimeConstant = 10.0; // τ = 10 My for exponential decay

    for (auto& BoundaryPair : Boundaries)
    {
        FPlateBoundary& Boundary = BoundaryPair.Value;

        switch (Boundary.BoundaryType)
        {
        case EBoundaryType::Convergent:
            // Convergent: accumulate stress based on relative velocity
            // Stress += relativeVelocity × ΔT (simplified linear model)
            // relativeVelocity is in rad/My, convert to stress units
            {
                const double StressRate = Boundary.RelativeVelocity * 1000.0; // Arbitrary scaling for visualization
                Boundary.AccumulatedStress += StressRate * DeltaTimeMy;
                Boundary.AccumulatedStress = FMath::Min(Boundary.AccumulatedStress, MaxStressMPa);
            }
            break;

        case EBoundaryType::Divergent:
            // Divergent: exponential decay toward zero
            // S(t) = S₀ × exp(-Δt/τ)
            {
                const double DecayFactor = FMath::Exp(-DeltaTimeMy / DecayTimeConstant);
                Boundary.AccumulatedStress *= DecayFactor;
            }
            break;

        case EBoundaryType::Transform:
            // Transform: minimal accumulation (small fraction of convergent rate)
            {
                const double StressRate = Boundary.RelativeVelocity * 100.0; // 10x less than convergent
                Boundary.AccumulatedStress += StressRate * DeltaTimeMy;
                Boundary.AccumulatedStress = FMath::Min(Boundary.AccumulatedStress, MaxStressMPa * 0.5); // Lower cap
            }
            break;
        }
    }
}

void UTectonicSimulationService::InterpolateStressToVertices()
{
    // Milestone 3 Task 2.3: Interpolate boundary stress to render vertices
    // Using distance-based Gaussian falloff with σ = 10° angular distance

    VertexStressValues.SetNumZeroed(RenderVertices.Num());

    constexpr double SigmaDegrees = 10.0;
    const double SigmaRadians = FMath::DegreesToRadians(SigmaDegrees);
    const double TwoSigmaSquared = 2.0 * SigmaRadians * SigmaRadians;

    // For each vertex, sum weighted contributions from all boundaries
    for (int32 i = 0; i < RenderVertices.Num(); ++i)
    {
        const FVector3d& VertexPos = RenderVertices[i];
        double TotalWeight = 0.0;
        double WeightedStress = 0.0;

        for (const auto& BoundaryPair : Boundaries)
        {
            const FPlateBoundary& Boundary = BoundaryPair.Value;

            if (Boundary.SharedEdgeVertices.Num() < 2)
            {
                continue; // Need at least 2 vertices for boundary edge
            }

            // Calculate distance from vertex to boundary edge (approximate as midpoint)
            const int32 V0Index = Boundary.SharedEdgeVertices[0];
            const int32 V1Index = Boundary.SharedEdgeVertices[1];

            if (!SharedVertices.IsValidIndex(V0Index) || !SharedVertices.IsValidIndex(V1Index))
            {
                continue;
            }

            const FVector3d& EdgeV0 = SharedVertices[V0Index];
            const FVector3d& EdgeV1 = SharedVertices[V1Index];
            const FVector3d EdgeMidpoint = (EdgeV0 + EdgeV1).GetSafeNormal();

            // Angular distance from vertex to boundary (great circle distance on unit sphere)
            const double DotProduct = FVector3d::DotProduct(VertexPos, EdgeMidpoint);
            const double AngularDistance = FMath::Acos(FMath::Clamp(DotProduct, -1.0, 1.0));

            // Gaussian weight: exp(-distance² / (2σ²))
            const double Weight = FMath::Exp(-(AngularDistance * AngularDistance) / TwoSigmaSquared);

            WeightedStress += Boundary.AccumulatedStress * Weight;
            TotalWeight += Weight;
        }

        if (TotalWeight > 1e-9)
        {
            VertexStressValues[i] = WeightedStress / TotalWeight;
        }
    }
}

void UTectonicSimulationService::ApplyLloydRelaxation()
{
    // Milestone 3 Task 3.1: Lloyd's algorithm for even centroid distribution
    // Algorithm:
    // 1. For each plate, compute Voronoi cell (all render vertices closest to this plate)
    // 2. Calculate centroid of Voronoi cell (spherical average)
    // 3. Move plate centroid toward cell centroid (weighted step α = 0.5)
    // 4. Repeat until convergence (max delta < ε) or max iterations reached

    const int32 MaxIterations = Parameters.LloydIterations;
    if (MaxIterations <= 0)
    {
        UE_LOG(LogTemp, Verbose, TEXT("Lloyd relaxation disabled (iterations=0)"));
        return;
    }

    constexpr double ConvergenceThreshold = 0.01; // radians (~0.57°)
    constexpr double Alpha = 0.5; // Step weight for stability

    UE_LOG(LogTemp, Log, TEXT("Starting Lloyd relaxation with %d iterations, ε=%.4f rad"), MaxIterations, ConvergenceThreshold);

    for (int32 Iteration = 0; Iteration < MaxIterations; ++Iteration)
    {
        // Step 1: Assign render vertices to Voronoi cells (compute nearest plate centroid)
        TArray<TArray<FVector3d>> VoronoiCells;
        VoronoiCells.SetNum(Plates.Num());

        for (int32 i = 0; i < RenderVertices.Num(); ++i)
        {
            // Find nearest plate centroid to this render vertex
            int32 NearestPlateIdx = 0;
            double MinDistanceSquared = TNumericLimits<double>::Max();

            for (int32 PlateIdx = 0; PlateIdx < Plates.Num(); ++PlateIdx)
            {
                const double DistanceSquared = FVector3d::DistSquared(RenderVertices[i], Plates[PlateIdx].Centroid);
                if (DistanceSquared < MinDistanceSquared)
                {
                    MinDistanceSquared = DistanceSquared;
                    NearestPlateIdx = PlateIdx;
                }
            }

            VoronoiCells[NearestPlateIdx].Add(RenderVertices[i]);
        }

        // Step 2: Compute cell centroids and update plate centroids
        double MaxDelta = 0.0;

        for (int32 PlateIdx = 0; PlateIdx < Plates.Num(); ++PlateIdx)
        {
            FTectonicPlate& Plate = Plates[PlateIdx];
            const TArray<FVector3d>& Cell = VoronoiCells[PlateIdx];

            if (Cell.Num() == 0)
            {
                UE_LOG(LogTemp, Warning, TEXT("Lloyd iteration %d: Plate %d has empty Voronoi cell"), Iteration, PlateIdx);
                continue;
            }

            // Compute spherical centroid (normalized sum of cell vertices)
            FVector3d CellCentroid = FVector3d::ZeroVector;
            for (const FVector3d& Vertex : Cell)
            {
                CellCentroid += Vertex;
            }
            CellCentroid.Normalize();

            // Step 3: Move plate centroid toward cell centroid (weighted)
            const FVector3d OldCentroid = Plate.Centroid;
            const FVector3d NewCentroid = ((1.0 - Alpha) * Plate.Centroid + Alpha * CellCentroid).GetSafeNormal();
            Plate.Centroid = NewCentroid;

            // Track maximum centroid movement for convergence check
            const double Delta = FMath::Acos(FMath::Clamp(FVector3d::DotProduct(OldCentroid, NewCentroid), -1.0, 1.0));
            MaxDelta = FMath::Max(MaxDelta, Delta);
        }

        UE_LOG(LogTemp, Verbose, TEXT("Lloyd iteration %d: max delta = %.6f rad (%.4f°)"),
            Iteration, MaxDelta, FMath::RadiansToDegrees(MaxDelta));

        // Early termination if converged
        if (MaxDelta < ConvergenceThreshold)
        {
            UE_LOG(LogTemp, Log, TEXT("Lloyd relaxation converged after %d iterations (delta=%.6f rad < ε=%.4f rad)"),
                Iteration + 1, MaxDelta, ConvergenceThreshold);
            return;
        }
    }

    UE_LOG(LogTemp, Log, TEXT("Lloyd relaxation completed %d iterations (did not fully converge)"), MaxIterations);
}

void UTectonicSimulationService::CheckRetessellationNeeded()
{
    // Milestone 3 Task 3.3: Framework stub for dynamic re-tessellation detection
    // Full implementation deferred to M4

    if (InitialPlateCentroids.Num() != Plates.Num())
    {
        // Mismatch indicates simulation was reset - re-capture initial positions
        InitialPlateCentroids.SetNum(Plates.Num());
        for (int32 i = 0; i < Plates.Num(); ++i)
        {
            InitialPlateCentroids[i] = Plates[i].Centroid;
        }
        return;
    }

    const double ThresholdRadians = FMath::DegreesToRadians(Parameters.RetessellationThresholdDegrees);
    double MaxDrift = 0.0;
    int32 MaxDriftPlateID = INDEX_NONE;

    for (int32 i = 0; i < Plates.Num(); ++i)
    {
        const FVector3d& InitialCentroid = InitialPlateCentroids[i];
        const FVector3d& CurrentCentroid = Plates[i].Centroid;

        // Angular distance on unit sphere
        const double DotProduct = FVector3d::DotProduct(InitialCentroid, CurrentCentroid);
        const double AngularDistance = FMath::Acos(FMath::Clamp(DotProduct, -1.0, 1.0));

        if (AngularDistance > MaxDrift)
        {
            MaxDrift = AngularDistance;
            MaxDriftPlateID = Plates[i].PlateID;
        }
    }

    // Log warning if threshold exceeded (M3 behavior - no actual re-tessellation)
    if (MaxDrift > ThresholdRadians)
    {
        if (Parameters.bEnableDynamicRetessellation)
        {
            UE_LOG(LogTemp, Warning, TEXT("Re-tessellation triggered: Plate %d drifted %.2f° (threshold: %.2f°). Implementation deferred to M4."),
                MaxDriftPlateID, FMath::RadiansToDegrees(MaxDrift), Parameters.RetessellationThresholdDegrees);
        }
        else
        {
            UE_LOG(LogTemp, Verbose, TEXT("Re-tessellation would be needed: Plate %d drifted %.2f° (threshold: %.2f°), but bEnableDynamicRetessellation=false"),
                MaxDriftPlateID, FMath::RadiansToDegrees(MaxDrift), Parameters.RetessellationThresholdDegrees);
        }
    }
}

// ============================================================================
// Milestone 5 Task 1.3: History Management (Undo/Redo)
// ============================================================================

void UTectonicSimulationService::CaptureHistorySnapshot()
{
    // If we're in the middle of the history stack (after undo), truncate future history
    if (CurrentHistoryIndex < HistoryStack.Num() - 1)
    {
        HistoryStack.SetNum(CurrentHistoryIndex + 1);
    }

    // Create new snapshot
    FSimulationHistorySnapshot Snapshot;
    Snapshot.CurrentTimeMy = CurrentTimeMy;
    Snapshot.Plates = Plates;
    Snapshot.SharedVertices = SharedVertices;
    Snapshot.RenderVertices = RenderVertices;
    Snapshot.RenderTriangles = RenderTriangles;
    Snapshot.VertexPlateAssignments = VertexPlateAssignments;
    Snapshot.VertexVelocities = VertexVelocities;
    Snapshot.VertexStressValues = VertexStressValues;
    Snapshot.VertexTemperatureValues = VertexTemperatureValues;
    Snapshot.Boundaries = Boundaries;
    Snapshot.TopologyEvents = TopologyEvents;
    Snapshot.Hotspots = Hotspots;
    Snapshot.InitialPlateCentroids = InitialPlateCentroids;
    Snapshot.TopologyVersion = TopologyVersion;
    Snapshot.SurfaceDataVersion = SurfaceDataVersion;

    // Milestone 5: Capture erosion state
    Snapshot.VertexElevationValues = VertexElevationValues;
    Snapshot.VertexErosionRates = VertexErosionRates;
    Snapshot.VertexSedimentThickness = VertexSedimentThickness;
    Snapshot.VertexCrustAge = VertexCrustAge;

    // Milestone 6: Capture terrane state
    Snapshot.Terranes = Terranes;
    Snapshot.NextTerraneID = NextTerraneID;

    // Add to stack
    HistoryStack.Add(Snapshot);
    CurrentHistoryIndex = HistoryStack.Num() - 1;

    // Enforce max history size (sliding window)
    if (HistoryStack.Num() > MaxHistorySize)
    {
        HistoryStack.RemoveAt(0);
        CurrentHistoryIndex = HistoryStack.Num() - 1;
        UE_LOG(LogTemp, Verbose, TEXT("History stack full, removed oldest snapshot (max %d)"), MaxHistorySize);
    }

    UE_LOG(LogTemp, Verbose, TEXT("CaptureHistorySnapshot: Snapshot %d captured at %.1f My"),
        CurrentHistoryIndex, CurrentTimeMy);
}

bool UTectonicSimulationService::Undo()
{
    if (!CanUndo())
    {
        UE_LOG(LogTemp, Warning, TEXT("Undo: No previous state available"));
        return false;
    }

    CurrentHistoryIndex--;
    const FSimulationHistorySnapshot& Snapshot = HistoryStack[CurrentHistoryIndex];

    // Restore state from snapshot
    CurrentTimeMy = Snapshot.CurrentTimeMy;
    Plates = Snapshot.Plates;
    SharedVertices = Snapshot.SharedVertices;
    RenderVertices = Snapshot.RenderVertices;
    RenderTriangles = Snapshot.RenderTriangles;
    VertexPlateAssignments = Snapshot.VertexPlateAssignments;
    VertexVelocities = Snapshot.VertexVelocities;
    VertexStressValues = Snapshot.VertexStressValues;
    VertexTemperatureValues = Snapshot.VertexTemperatureValues;
    Boundaries = Snapshot.Boundaries;
    TopologyEvents = Snapshot.TopologyEvents;
    Hotspots = Snapshot.Hotspots;
    InitialPlateCentroids = Snapshot.InitialPlateCentroids;
    TopologyVersion = Snapshot.TopologyVersion;
    SurfaceDataVersion = Snapshot.SurfaceDataVersion;

    // Milestone 5: Restore erosion state
    VertexElevationValues = Snapshot.VertexElevationValues;
    VertexErosionRates = Snapshot.VertexErosionRates;
    VertexSedimentThickness = Snapshot.VertexSedimentThickness;
    VertexCrustAge = Snapshot.VertexCrustAge;

    // Milestone 6: Restore terrane state
    Terranes = Snapshot.Terranes;
    NextTerraneID = Snapshot.NextTerraneID;

    UE_LOG(LogTemp, Log, TEXT("Undo: Restored snapshot %d (%.1f My)"),
        CurrentHistoryIndex, CurrentTimeMy);
    return true;
}

bool UTectonicSimulationService::Redo()
{
    if (!CanRedo())
    {
        UE_LOG(LogTemp, Warning, TEXT("Redo: No future state available"));
        return false;
    }

    CurrentHistoryIndex++;
    const FSimulationHistorySnapshot& Snapshot = HistoryStack[CurrentHistoryIndex];

    // Restore state from snapshot
    CurrentTimeMy = Snapshot.CurrentTimeMy;
    Plates = Snapshot.Plates;
    SharedVertices = Snapshot.SharedVertices;
    RenderVertices = Snapshot.RenderVertices;
    RenderTriangles = Snapshot.RenderTriangles;
    VertexPlateAssignments = Snapshot.VertexPlateAssignments;
    VertexVelocities = Snapshot.VertexVelocities;
    VertexStressValues = Snapshot.VertexStressValues;
    VertexTemperatureValues = Snapshot.VertexTemperatureValues;
    Boundaries = Snapshot.Boundaries;
    TopologyEvents = Snapshot.TopologyEvents;
    Hotspots = Snapshot.Hotspots;
    InitialPlateCentroids = Snapshot.InitialPlateCentroids;
    TopologyVersion = Snapshot.TopologyVersion;
    SurfaceDataVersion = Snapshot.SurfaceDataVersion;

    // Milestone 5: Restore erosion state
    VertexElevationValues = Snapshot.VertexElevationValues;
    VertexErosionRates = Snapshot.VertexErosionRates;
    VertexSedimentThickness = Snapshot.VertexSedimentThickness;
    VertexCrustAge = Snapshot.VertexCrustAge;

    // Milestone 6: Restore terrane state
    Terranes = Snapshot.Terranes;
    NextTerraneID = Snapshot.NextTerraneID;

    UE_LOG(LogTemp, Log, TEXT("Redo: Restored snapshot %d (%.1f My)"),
        CurrentHistoryIndex, CurrentTimeMy);
    return true;
}

bool UTectonicSimulationService::JumpToHistoryIndex(int32 Index)
{
    if (!HistoryStack.IsValidIndex(Index))
    {
        UE_LOG(LogTemp, Warning, TEXT("JumpToHistoryIndex: Invalid index %d (stack size %d)"),
            Index, HistoryStack.Num());
        return false;
    }

    CurrentHistoryIndex = Index;
    const FSimulationHistorySnapshot& Snapshot = HistoryStack[CurrentHistoryIndex];

    // Restore state from snapshot
    CurrentTimeMy = Snapshot.CurrentTimeMy;
    Plates = Snapshot.Plates;
    SharedVertices = Snapshot.SharedVertices;
    RenderVertices = Snapshot.RenderVertices;
    RenderTriangles = Snapshot.RenderTriangles;
    VertexPlateAssignments = Snapshot.VertexPlateAssignments;
    VertexVelocities = Snapshot.VertexVelocities;
    VertexStressValues = Snapshot.VertexStressValues;
    VertexTemperatureValues = Snapshot.VertexTemperatureValues;
    Boundaries = Snapshot.Boundaries;
    TopologyEvents = Snapshot.TopologyEvents;
    Hotspots = Snapshot.Hotspots;
    InitialPlateCentroids = Snapshot.InitialPlateCentroids;
    TopologyVersion = Snapshot.TopologyVersion;
    SurfaceDataVersion = Snapshot.SurfaceDataVersion;

    // Milestone 5: Restore erosion state
    VertexElevationValues = Snapshot.VertexElevationValues;
    VertexErosionRates = Snapshot.VertexErosionRates;
    VertexSedimentThickness = Snapshot.VertexSedimentThickness;
    VertexCrustAge = Snapshot.VertexCrustAge;

    // Milestone 6: Restore terrane state
    Terranes = Snapshot.Terranes;
    NextTerraneID = Snapshot.NextTerraneID;

    UE_LOG(LogTemp, Log, TEXT("JumpToHistoryIndex: Jumped to snapshot %d (%.1f My)"),
        CurrentHistoryIndex, CurrentTimeMy);
    return true;
}

// ========================================
// Milestone 6 Task 1.1: Terrane Mechanics
// ========================================

bool UTectonicSimulationService::ValidateTopology(FString& OutErrorMessage) const
{
    const int32 V = RenderVertices.Num();
    const int32 F = RenderTriangles.Num() / 3;

    if (V == 0 || F == 0)
    {
        OutErrorMessage = TEXT("Empty mesh: no vertices or faces");
        return false;
    }

    // Compute unique edges
    TSet<TPair<int32, int32>> UniqueEdges;
    TMap<TPair<int32, int32>, int32> EdgeCounts;

    for (int32 i = 0; i < RenderTriangles.Num(); i += 3)
    {
        int32 V0 = RenderTriangles[i];
        int32 V1 = RenderTriangles[i + 1];
        int32 V2 = RenderTriangles[i + 2];

        // Validate triangle indices
        if (V0 < 0 || V0 >= V || V1 < 0 || V1 >= V || V2 < 0 || V2 >= V)
        {
            OutErrorMessage = FString::Printf(TEXT("Invalid triangle indices: (%d, %d, %d), vertex count: %d"),
                V0, V1, V2, V);
            return false;
        }

        // Check for degenerate triangles
        if (V0 == V1 || V1 == V2 || V2 == V0)
        {
            OutErrorMessage = FString::Printf(TEXT("Degenerate triangle at face %d: (%d, %d, %d)"),
                i / 3, V0, V1, V2);
            return false;
        }

        // Add edges (canonical order: min vertex first)
        auto Edge01 = MakeTuple(FMath::Min(V0, V1), FMath::Max(V0, V1));
        auto Edge12 = MakeTuple(FMath::Min(V1, V2), FMath::Max(V1, V2));
        auto Edge20 = MakeTuple(FMath::Min(V2, V0), FMath::Max(V2, V0));

        UniqueEdges.Add(Edge01);
        UniqueEdges.Add(Edge12);
        UniqueEdges.Add(Edge20);

        EdgeCounts.FindOrAdd(Edge01)++;
        EdgeCounts.FindOrAdd(Edge12)++;
        EdgeCounts.FindOrAdd(Edge20)++;
    }

    const int32 E = UniqueEdges.Num();
    const int32 EulerChar = V - E + F;

    // Validation 1: Euler characteristic (must be 2 for closed sphere)
    if (EulerChar != 2)
    {
        OutErrorMessage = FString::Printf(TEXT("Invalid Euler characteristic: V=%d, E=%d, F=%d, V-E+F=%d (expected 2)"),
            V, E, F, EulerChar);
        return false;
    }

    // Validation 2: Manifold edges (each edge touches exactly 2 triangles)
    int32 NonManifoldEdges = 0;
    for (const auto& Pair : EdgeCounts)
    {
        if (Pair.Value != 2)
        {
            NonManifoldEdges++;
            if (NonManifoldEdges <= 3) // Log first 3 only
            {
                OutErrorMessage += FString::Printf(TEXT("Non-manifold edge: (%d, %d) appears %d times (expected 2); "),
                    Pair.Key.Key, Pair.Key.Value, Pair.Value);
            }
        }
    }

    if (NonManifoldEdges > 0)
    {
        OutErrorMessage = FString::Printf(TEXT("%d non-manifold edges found. "), NonManifoldEdges) + OutErrorMessage;
        return false;
    }

    // Validation 3: No orphaned vertices
    TSet<int32> ReferencedVertices;
    for (int32 Idx : RenderTriangles)
    {
        ReferencedVertices.Add(Idx);
    }

    const int32 OrphanedVertices = V - ReferencedVertices.Num();
    if (OrphanedVertices > 0)
    {
        OutErrorMessage = FString::Printf(TEXT("%d orphaned vertices found (not referenced by any triangle)"),
            OrphanedVertices);
        return false;
    }

    // All validations passed
    return true;
}

double UTectonicSimulationService::ComputeTerraneArea(const TArray<int32>& VertexIndices) const
{
    if (VertexIndices.Num() < 3)
    {
        return 0.0; // Too few vertices for area
    }

    // Find triangles that are entirely within the terrane region
    TSet<int32> TerraneVertexSet(VertexIndices);
    double TotalArea = 0.0;
    const double RadiusKm = Parameters.PlanetRadius / 1000.0; // Convert meters to km

    for (int32 i = 0; i < RenderTriangles.Num(); i += 3)
    {
        int32 V0 = RenderTriangles[i];
        int32 V1 = RenderTriangles[i + 1];
        int32 V2 = RenderTriangles[i + 2];

        // Check if all 3 vertices belong to terrane
        if (TerraneVertexSet.Contains(V0) && TerraneVertexSet.Contains(V1) && TerraneVertexSet.Contains(V2))
        {
            // Spherical triangle area using L'Huilier's theorem
            const FVector3d& A = RenderVertices[V0];
            const FVector3d& B = RenderVertices[V1];
            const FVector3d& C = RenderVertices[V2];

            // Edge lengths (angles in radians on unit sphere)
            double a = FMath::Acos(FMath::Clamp(B | C, -1.0, 1.0)); // Angle BC
            double b = FMath::Acos(FMath::Clamp(C | A, -1.0, 1.0)); // Angle CA
            double c = FMath::Acos(FMath::Clamp(A | B, -1.0, 1.0)); // Angle AB

            // Semi-perimeter
            double s = (a + b + c) * 0.5;

            // L'Huilier's formula: tan(E/4) = sqrt(tan(s/2) * tan((s-a)/2) * tan((s-b)/2) * tan((s-c)/2))
            // where E is the spherical excess (area on unit sphere)
            double tan_s2 = FMath::Tan(s * 0.5);
            double tan_sa2 = FMath::Tan((s - a) * 0.5);
            double tan_sb2 = FMath::Tan((s - b) * 0.5);
            double tan_sc2 = FMath::Tan((s - c) * 0.5);

            double tan_E4 = FMath::Sqrt(FMath::Max(0.0, tan_s2 * tan_sa2 * tan_sb2 * tan_sc2));
            double E = 4.0 * FMath::Atan(tan_E4); // Spherical excess (area on unit sphere)

            // Scale by radius²
            double TriangleAreaKm2 = E * RadiusKm * RadiusKm;
            TotalArea += TriangleAreaKm2;
        }
    }

    return TotalArea;
}

const FContinentalTerrane* UTectonicSimulationService::GetTerraneByID(int32 TerraneID) const
{
    for (const FContinentalTerrane& Terrane : Terranes)
    {
        if (Terrane.TerraneID == TerraneID)
        {
            return &Terrane;
        }
    }
    return nullptr;
}

bool UTectonicSimulationService::ExtractTerrane(int32 SourcePlateID, const TArray<int32>& TerraneVertexIndices, int32& OutTerraneID)
{
    UE_LOG(LogTemp, Log, TEXT("ExtractTerrane: Attempting to extract %d vertices from plate %d"),
        TerraneVertexIndices.Num(), SourcePlateID);

    // Find source plate
    FTectonicPlate* SourcePlate = nullptr;
    for (FTectonicPlate& Plate : Plates)
    {
        if (Plate.PlateID == SourcePlateID)
        {
            SourcePlate = &Plate;
            break;
        }
    }

    if (!SourcePlate)
    {
        UE_LOG(LogTemp, Error, TEXT("ExtractTerrane: Source plate %d not found"), SourcePlateID);
        return false;
    }

    // Validation: Source must be continental
    if (SourcePlate->CrustType != ECrustType::Continental)
    {
        UE_LOG(LogTemp, Error, TEXT("ExtractTerrane: Source plate %d is not continental"), SourcePlateID);
        return false;
    }

    // Validation: Vertices must be valid
    for (int32 VertexIdx : TerraneVertexIndices)
    {
        if (VertexIdx < 0 || VertexIdx >= RenderVertices.Num())
        {
            UE_LOG(LogTemp, Error, TEXT("ExtractTerrane: Invalid vertex index %d (range: 0-%d)"),
                VertexIdx, RenderVertices.Num() - 1);
            return false;
        }

        // Check vertex belongs to source plate
        if (VertexPlateAssignments[VertexIdx] != SourcePlateID)
        {
            UE_LOG(LogTemp, Error, TEXT("ExtractTerrane: Vertex %d does not belong to plate %d (assigned to %d)"),
                VertexIdx, SourcePlateID, VertexPlateAssignments[VertexIdx]);
            return false;
        }
    }

    // Edge case 1: Single-vertex terrane (min area: 100 km²)
    const double TerraneArea = ComputeTerraneArea(TerraneVertexIndices);
    if (TerraneArea < 100.0)
    {
        UE_LOG(LogTemp, Warning, TEXT("ExtractTerrane: Terrane area %.2f km² below minimum 100 km², rejecting extraction"),
            TerraneArea);
        return false;
    }

    // Edge case 3: Single-terrane plate (all vertices extracted)
    int32 PlateVertexCount = 0;
    for (int32 Assignment : VertexPlateAssignments)
    {
        if (Assignment == SourcePlateID)
        {
            PlateVertexCount++;
        }
    }

    if (TerraneVertexIndices.Num() == PlateVertexCount)
    {
        UE_LOG(LogTemp, Warning, TEXT("ExtractTerrane: Extracting all %d vertices from plate %d (treat as plate split)"),
            PlateVertexCount, SourcePlateID);
        // TODO: Convert to plate split instead of terrane extraction (deferred to edge case implementation)
        return false;
    }

    // Capture pre-extraction snapshot for rollback
    FString ValidationError;
    if (!ValidateTopology(ValidationError))
    {
        UE_LOG(LogTemp, Error, TEXT("ExtractTerrane: Pre-extraction topology invalid: %s"), *ValidationError);
        return false;
    }

    // Assign terrane vertices to a temporary "detached" plate ID (-1 by convention)
    for (int32 VertexIdx : TerraneVertexIndices)
    {
        VertexPlateAssignments[VertexIdx] = INDEX_NONE; // Mark as unassigned
    }

    // Compute terrane centroid
    FVector3d TerraneCentroid = FVector3d::ZeroVector;
    for (int32 VertexIdx : TerraneVertexIndices)
    {
        TerraneCentroid += RenderVertices[VertexIdx];
    }
    TerraneCentroid /= static_cast<double>(TerraneVertexIndices.Num());
    TerraneCentroid.Normalize();

    // Create terrane record
    FContinentalTerrane NewTerrane;
    NewTerrane.TerraneID = NextTerraneID++;
    NewTerrane.State = ETerraneState::Extracted;
    NewTerrane.VertexIndices = TerraneVertexIndices;
    NewTerrane.SourcePlateID = SourcePlateID;
    NewTerrane.CarrierPlateID = INDEX_NONE;
    NewTerrane.TargetPlateID = INDEX_NONE;
    NewTerrane.Centroid = TerraneCentroid;
    NewTerrane.AreaKm2 = TerraneArea;
    NewTerrane.ExtractionTimeMy = CurrentTimeMy;
    NewTerrane.ReattachmentTimeMy = 0.0;

    Terranes.Add(NewTerrane);
    OutTerraneID = NewTerrane.TerraneID;

    // Validate post-extraction topology
    if (!ValidateTopology(ValidationError))
    {
        UE_LOG(LogTemp, Error, TEXT("ExtractTerrane: Post-extraction topology invalid: %s"), *ValidationError);

        // Rollback: Reassign vertices back to source plate
        for (int32 VertexIdx : TerraneVertexIndices)
        {
            VertexPlateAssignments[VertexIdx] = SourcePlateID;
        }
        Terranes.Pop(); // Remove failed terrane
        NextTerraneID--; // Restore ID counter

        return false;
    }

    // Increment topology version (mesh structure unchanged, but plate assignments changed)
    TopologyVersion++;

    UE_LOG(LogTemp, Log, TEXT("ExtractTerrane: Successfully extracted terrane %d (%.2f km²) from plate %d"),
        OutTerraneID, TerraneArea, SourcePlateID);

    // Milestone 6 Task 1.2: Automatically assign carrier plate to initiate transport
    AssignTerraneCarrier(OutTerraneID);

    return true;
}

bool UTectonicSimulationService::ReattachTerrane(int32 TerraneID, int32 TargetPlateID)
{
    UE_LOG(LogTemp, Log, TEXT("ReattachTerrane: Attempting to reattach terrane %d to plate %d"),
        TerraneID, TargetPlateID);

    // Find terrane
    FContinentalTerrane* Terrane = nullptr;
    int32 TerraneIndex = INDEX_NONE;
    for (int32 i = 0; i < Terranes.Num(); ++i)
    {
        if (Terranes[i].TerraneID == TerraneID)
        {
            Terrane = &Terranes[i];
            TerraneIndex = i;
            break;
        }
    }

    if (!Terrane)
    {
        UE_LOG(LogTemp, Error, TEXT("ReattachTerrane: Terrane %d not found"), TerraneID);
        return false;
    }

    // Validation: Terrane must be detached (Extracted, Transporting, or Colliding)
    if (Terrane->State != ETerraneState::Extracted &&
        Terrane->State != ETerraneState::Transporting &&
        Terrane->State != ETerraneState::Colliding)
    {
        UE_LOG(LogTemp, Error, TEXT("ReattachTerrane: Terrane %d not in detached state (current: %d, expected: 1=Extracted, 2=Transporting, 3=Colliding)"),
            TerraneID, static_cast<int32>(Terrane->State));
        return false;
    }

    // Find target plate
    FTectonicPlate* TargetPlate = nullptr;
    for (FTectonicPlate& Plate : Plates)
    {
        if (Plate.PlateID == TargetPlateID)
        {
            TargetPlate = &Plate;
            break;
        }
    }

    if (!TargetPlate)
    {
        UE_LOG(LogTemp, Error, TEXT("ReattachTerrane: Target plate %d not found"), TargetPlateID);
        return false;
    }

    // Validation: Target must be continental
    if (TargetPlate->CrustType != ECrustType::Continental)
    {
        UE_LOG(LogTemp, Error, TEXT("ReattachTerrane: Target plate %d is not continental"), TargetPlateID);
        return false;
    }

    // Capture pre-reattachment snapshot for rollback
    FString ValidationError;
    if (!ValidateTopology(ValidationError))
    {
        UE_LOG(LogTemp, Error, TEXT("ReattachTerrane: Pre-reattachment topology invalid: %s"), *ValidationError);
        return false;
    }

    // Assign terrane vertices to target plate
    for (int32 VertexIdx : Terrane->VertexIndices)
    {
        if (VertexIdx < 0 || VertexIdx >= VertexPlateAssignments.Num())
        {
            UE_LOG(LogTemp, Error, TEXT("ReattachTerrane: Invalid vertex index %d in terrane %d"),
                VertexIdx, TerraneID);
            return false;
        }

        VertexPlateAssignments[VertexIdx] = TargetPlateID;
    }

    // Update terrane record
    Terrane->State = ETerraneState::Attached;
    Terrane->TargetPlateID = TargetPlateID;
    Terrane->CarrierPlateID = INDEX_NONE;
    Terrane->ReattachmentTimeMy = CurrentTimeMy;

    // Validate post-reattachment topology
    if (!ValidateTopology(ValidationError))
    {
        UE_LOG(LogTemp, Error, TEXT("ReattachTerrane: Post-reattachment topology invalid: %s"), *ValidationError);

        // Rollback: Unassign vertices
        for (int32 VertexIdx : Terrane->VertexIndices)
        {
            VertexPlateAssignments[VertexIdx] = INDEX_NONE;
        }

        // Restore terrane state
        Terrane->State = ETerraneState::Extracted;
        Terrane->TargetPlateID = INDEX_NONE;
        Terrane->ReattachmentTimeMy = 0.0;

        return false;
    }

    // Remove terrane from active list (now fully integrated into target plate)
    Terranes.RemoveAt(TerraneIndex);

    // Increment topology version
    TopologyVersion++;

    UE_LOG(LogTemp, Log, TEXT("ReattachTerrane: Successfully reattached terrane %d to plate %d (%.2f My transport duration)"),
        TerraneID, TargetPlateID, CurrentTimeMy - Terrane->ExtractionTimeMy);

    return true;
}

bool UTectonicSimulationService::AssignTerraneCarrier(int32 TerraneID)
{
    // Find terrane
    FContinentalTerrane* Terrane = nullptr;
    for (FContinentalTerrane& T : Terranes)
    {
        if (T.TerraneID == TerraneID)
        {
            Terrane = &T;
            break;
        }
    }

    if (!Terrane)
    {
        UE_LOG(LogTemp, Error, TEXT("AssignTerraneCarrier: Terrane %d not found"), TerraneID);
        return false;
    }

    // Validation: Terrane must be in Extracted state
    if (Terrane->State != ETerraneState::Extracted)
    {
        UE_LOG(LogTemp, Error, TEXT("AssignTerraneCarrier: Terrane %d not in Extracted state (current: %d)"),
            TerraneID, static_cast<int32>(Terrane->State));
        return false;
    }

    // Find nearest oceanic plate to terrane centroid
    double MinDistance = TNumericLimits<double>::Max();
    int32 NearestOceanicPlateID = INDEX_NONE;

    for (const FTectonicPlate& Plate : Plates)
    {
        if (Plate.CrustType == ECrustType::Oceanic)
        {
            // Geodesic distance between terrane centroid and plate centroid
            const double Distance = FMath::Acos(FMath::Clamp(Terrane->Centroid | Plate.Centroid, -1.0, 1.0));

            if (Distance < MinDistance)
            {
                MinDistance = Distance;
                NearestOceanicPlateID = Plate.PlateID;
            }
        }
    }

    if (NearestOceanicPlateID == INDEX_NONE)
    {
        UE_LOG(LogTemp, Warning, TEXT("AssignTerraneCarrier: No oceanic plates found, terrane %d remains in Extracted state"), TerraneID);
        return false;
    }

    // Assign carrier and transition to Transporting state
    Terrane->CarrierPlateID = NearestOceanicPlateID;
    Terrane->State = ETerraneState::Transporting;

    const double DistanceKm = MinDistance * (Parameters.PlanetRadius / 1000.0);
    UE_LOG(LogTemp, Log, TEXT("AssignTerraneCarrier: Terrane %d assigned to oceanic carrier %d (distance: %.1f km)"),
        TerraneID, NearestOceanicPlateID, DistanceKm);

    return true;
}

void UTectonicSimulationService::UpdateTerranePositions(double DeltaTimeMy)
{
    for (FContinentalTerrane& Terrane : Terranes)
    {
        // Only update terranes that are being transported by carrier plates
        if (Terrane.State != ETerraneState::Transporting)
        {
            continue;
        }

        // Find carrier plate
        const FTectonicPlate* CarrierPlate = nullptr;
        for (const FTectonicPlate& Plate : Plates)
        {
            if (Plate.PlateID == Terrane.CarrierPlateID)
            {
                CarrierPlate = &Plate;
                break;
            }
        }

        if (!CarrierPlate)
        {
            UE_LOG(LogTemp, Warning, TEXT("UpdateTerranePositions: Carrier plate %d not found for terrane %d"),
                Terrane.CarrierPlateID, Terrane.TerraneID);
            continue;
        }

        // Rotate terrane vertices around carrier plate's Euler pole
        const FVector3d& RotationAxis = CarrierPlate->EulerPoleAxis;
        const double RotationAngle = CarrierPlate->AngularVelocity * DeltaTimeMy;

        // Rodrigues' rotation formula: v' = v*cos(θ) + (k×v)*sin(θ) + k*(k·v)*(1-cos(θ))
        const double CosTheta = FMath::Cos(RotationAngle);
        const double SinTheta = FMath::Sin(RotationAngle);
        const double OneMinusCosTheta = 1.0 - CosTheta;

        for (int32 VertexIdx : Terrane.VertexIndices)
        {
            if (VertexIdx < 0 || VertexIdx >= RenderVertices.Num())
            {
                continue;
            }

            const FVector3d& V = RenderVertices[VertexIdx];
            const FVector3d KCrossV = RotationAxis ^ V;
            const double KDotV = RotationAxis | V;

            RenderVertices[VertexIdx] = V * CosTheta + KCrossV * SinTheta + RotationAxis * KDotV * OneMinusCosTheta;
            RenderVertices[VertexIdx].Normalize(); // Maintain unit sphere
        }

        // Update terrane centroid
        FVector3d NewCentroid = FVector3d::ZeroVector;
        for (int32 VertexIdx : Terrane.VertexIndices)
        {
            if (VertexIdx >= 0 && VertexIdx < RenderVertices.Num())
            {
                NewCentroid += RenderVertices[VertexIdx];
            }
        }
        NewCentroid /= static_cast<double>(Terrane.VertexIndices.Num());
        Terrane.Centroid = NewCentroid.GetSafeNormal();
    }
}

void UTectonicSimulationService::DetectTerraneCollisions()
{
    const double CollisionThreshold_km = 500.0; // Paper Section 6: proximity threshold for collision detection
    const double CollisionThreshold_rad = CollisionThreshold_km / (Parameters.PlanetRadius / 1000.0);

    for (FContinentalTerrane& Terrane : Terranes)
    {
        // Only check terranes that are currently being transported
        if (Terrane.State != ETerraneState::Transporting)
        {
            continue;
        }

        // Check distance to all continental plates
        for (const FTectonicPlate& Plate : Plates)
        {
            if (Plate.CrustType != ECrustType::Continental)
            {
                continue; // Skip oceanic plates
            }

            // Skip source plate (terrane came from here)
            if (Plate.PlateID == Terrane.SourcePlateID)
            {
                continue;
            }

            // Check if terrane is approaching this continental plate
            // Find closest boundary point between carrier and target plate
            const TPair<int32, int32> BoundaryKey = Terrane.CarrierPlateID < Plate.PlateID
                ? MakeTuple(Terrane.CarrierPlateID, Plate.PlateID)
                : MakeTuple(Plate.PlateID, Terrane.CarrierPlateID);

            const FPlateBoundary* Boundary = Boundaries.Find(BoundaryKey);
            if (!Boundary)
            {
                continue; // No boundary between carrier and this continental plate
            }

            // Check if boundary is convergent (collision zone)
            if (Boundary->BoundaryType != EBoundaryType::Convergent)
            {
                continue; // Only convergent boundaries trigger collisions
            }

            // Compute distance from terrane centroid to boundary
            double MinDistanceToBoundary = TNumericLimits<double>::Max();
            for (int32 SharedVertexIdx : Boundary->SharedEdgeVertices)
            {
                if (SharedVertexIdx < 0 || SharedVertexIdx >= SharedVertices.Num())
                {
                    continue;
                }

                const FVector3d& BoundaryPoint = SharedVertices[SharedVertexIdx];
                const double Distance = FMath::Acos(FMath::Clamp(Terrane.Centroid | BoundaryPoint, -1.0, 1.0));

                if (Distance < MinDistanceToBoundary)
                {
                    MinDistanceToBoundary = Distance;
                }
            }

            // Check if terrane is within collision threshold
            if (MinDistanceToBoundary < CollisionThreshold_rad)
            {
                // Mark terrane as colliding and set target plate
                Terrane.State = ETerraneState::Colliding;
                Terrane.TargetPlateID = Plate.PlateID;

                const double DistanceKm = MinDistanceToBoundary * (Parameters.PlanetRadius / 1000.0);
                UE_LOG(LogTemp, Log, TEXT("DetectTerraneCollisions: Terrane %d approaching plate %d (distance: %.1f km, threshold: %.1f km)"),
                    Terrane.TerraneID, Plate.PlateID, DistanceKm, CollisionThreshold_km);

                break; // Stop checking other plates for this terrane
            }
        }
    }
}

void UTectonicSimulationService::ProcessTerraneReattachments()
{
    // Milestone 6 Task 1.3: Automatically reattach colliding terranes
    // Note: Iterate backwards since ReattachTerrane removes terranes from array
    for (int32 i = Terranes.Num() - 1; i >= 0; --i)
    {
        FContinentalTerrane& Terrane = Terranes[i];

        if (Terrane.State == ETerraneState::Colliding)
        {
            if (Terrane.TargetPlateID == INDEX_NONE)
            {
                UE_LOG(LogTemp, Warning, TEXT("ProcessTerraneReattachments: Terrane %d in Colliding state but no target plate assigned, skipping"),
                    Terrane.TerraneID);
                continue;
            }

            const double TransportDuration = CurrentTimeMy - Terrane.ExtractionTimeMy;
            UE_LOG(LogTemp, Log, TEXT("ProcessTerraneReattachments: Auto-reattaching terrane %d to plate %d after %.2f My transport"),
                Terrane.TerraneID, Terrane.TargetPlateID, TransportDuration);

            // Attempt reattachment (will validate topology and rollback on failure)
            const bool bSuccess = ReattachTerrane(Terrane.TerraneID, Terrane.TargetPlateID);

            if (!bSuccess)
            {
                UE_LOG(LogTemp, Warning, TEXT("ProcessTerraneReattachments: Failed to reattach terrane %d, will retry next step"),
                    Terrane.TerraneID);
                // Keep terrane in Colliding state for retry next step
            }
            // Note: If successful, terrane was removed from Terranes array by ReattachTerrane
        }
    }
}

// ============================================================================
// Milestone 6 Task 2.1: Oceanic Amplification (Stage B)
// ============================================================================

// Forward declarations from OceanicAmplification.cpp
double ComputeGaborNoiseApproximation(const FVector3d& Position, const FVector3d& FaultDirection, double Frequency);
double ComputeOceanicAmplification(const FVector3d& Position, int32 PlateID, double CrustAge_My, double BaseElevation_m,
    const FVector3d& RidgeDirection, const TArray<FTectonicPlate>& Plates, const TMap<TPair<int32, int32>, FPlateBoundary>& Boundaries);

// Forward declarations from ContinentalAmplification.cpp
double ComputeContinentalAmplification(const FVector3d& Position, int32 PlateID, double BaseElevation_m,
    const TArray<FTectonicPlate>& Plates, const TMap<TPair<int32, int32>, FPlateBoundary>& Boundaries,
    double OrogenyAge_My, EBoundaryType NearestBoundaryType, const FString& ProjectContentDir, int32 Seed);

void UTectonicSimulationService::ComputeRidgeDirections()
{
    // Paper Section 5: "using the recorded parameters r_c, i.e. the local direction parallel to the ridge"
    // For each vertex, find nearest divergent boundary and compute tangent direction

    const int32 VertexCount = RenderVertices.Num();
    checkf(VertexRidgeDirections.Num() == VertexCount, TEXT("VertexRidgeDirections not initialized"));

    const auto IsPlateInBoundary = [](const TPair<int32, int32>& BoundaryKey, int32 PlateID)
    {
        return BoundaryKey.Key == PlateID || BoundaryKey.Value == PlateID;
    };

    const auto ComputeSegmentTangent = [](const FVector3d& PlaneNormal, const FVector3d& PointOnGreatCircle)
    {
        const FVector3d Tangent = FVector3d::CrossProduct(PlaneNormal, PointOnGreatCircle).GetSafeNormal();
        return Tangent.IsNearlyZero() ? FVector3d::ZAxisVector : Tangent;
    };

    const double SegmentAngleTolerance = 1e-3; // Radians

    for (int32 VertexIdx = 0; VertexIdx < VertexCount; ++VertexIdx)
    {
        const FVector3d& VertexPosition = RenderVertices[VertexIdx];
        const int32 PlateID = VertexPlateAssignments.IsValidIndex(VertexIdx) ? VertexPlateAssignments[VertexIdx] : INDEX_NONE;

        if (PlateID == INDEX_NONE || !Plates.IsValidIndex(PlateID))
        {
            VertexRidgeDirections[VertexIdx] = FVector3d::ZAxisVector; // Fallback
            continue;
        }

        const FTectonicPlate& Plate = Plates[PlateID];

        // Only compute ridge direction for oceanic crust
        if (Plate.CrustType != ECrustType::Oceanic)
        {
            VertexRidgeDirections[VertexIdx] = FVector3d::ZAxisVector; // Continental vertices ignored
            continue;
        }

        const FVector3d VertexNormal = VertexPosition.GetSafeNormal();

        // Find nearest divergent boundary involving this plate
        FVector3d NearestBoundaryTangent = FVector3d::ZAxisVector; // Default fallback
        double MinDistance = TNumericLimits<double>::Max();

        for (const auto& BoundaryPair : Boundaries)
        {
            const TPair<int32, int32>& BoundaryKey = BoundaryPair.Key;
            const FPlateBoundary& Boundary = BoundaryPair.Value;

            // Only consider divergent boundaries involving this plate
            if (Boundary.BoundaryType != EBoundaryType::Divergent)
                continue;

            if (!IsPlateInBoundary(BoundaryKey, PlateID))
                continue;

            // Boundary is defined by shared edge vertices
            if (Boundary.SharedEdgeVertices.Num() < 2)
                continue;

            for (int32 EdgeIdx = 0; EdgeIdx < Boundary.SharedEdgeVertices.Num() - 1; ++EdgeIdx)
            {
                const int32 EdgeV0Idx = Boundary.SharedEdgeVertices[EdgeIdx];
                const int32 EdgeV1Idx = Boundary.SharedEdgeVertices[EdgeIdx + 1];
                if (!SharedVertices.IsValidIndex(EdgeV0Idx) || !SharedVertices.IsValidIndex(EdgeV1Idx))
                {
                    continue;
                }

                const FVector3d EdgeV0 = SharedVertices[EdgeV0Idx].GetSafeNormal();
                const FVector3d EdgeV1 = SharedVertices[EdgeV1Idx].GetSafeNormal();

                const FVector3d PlaneNormal = FVector3d::CrossProduct(EdgeV0, EdgeV1).GetSafeNormal();
                if (PlaneNormal.IsNearlyZero())
                {
                    continue;
                }

                // Project vertex onto plane of great circle
                const double Projection = FVector3d::DotProduct(VertexNormal, PlaneNormal);
                const FVector3d Projected = (VertexNormal - Projection * PlaneNormal);
                if (Projected.IsNearlyZero())
                {
                    continue;
                }

                const FVector3d GreatCirclePoint = Projected.GetSafeNormal();

                const double ArcAB = FMath::Acos(FMath::Clamp(EdgeV0 | EdgeV1, -1.0, 1.0));
                const double ArcAC = FMath::Acos(FMath::Clamp(EdgeV0 | GreatCirclePoint, -1.0, 1.0));
                const double ArcCB = FMath::Acos(FMath::Clamp(GreatCirclePoint | EdgeV1, -1.0, 1.0));

                const bool bWithinSegment = (ArcAC + ArcCB) <= (ArcAB + SegmentAngleTolerance);

                auto ConsiderPoint = [&](const FVector3d& PointOnCircle)
                {
                    const double AngularDistance = FMath::Acos(FMath::Clamp(VertexNormal | PointOnCircle, -1.0, 1.0));
                    if (AngularDistance < MinDistance)
                    {
                        MinDistance = AngularDistance;
                        NearestBoundaryTangent = ComputeSegmentTangent(PlaneNormal, PointOnCircle);
                    }
                };

                if (bWithinSegment)
                {
                    ConsiderPoint(GreatCirclePoint);
                }
                else
                {
                    ConsiderPoint(EdgeV0);
                    ConsiderPoint(EdgeV1);
                }
            }
        }

        VertexRidgeDirections[VertexIdx] = NearestBoundaryTangent;
    }
}

void UTectonicSimulationService::ApplyOceanicAmplification()
{
    // Paper Section 5: Apply procedural amplification to oceanic crust
    // Transform faults + high-frequency detail

    const int32 VertexCount = RenderVertices.Num();
    checkf(VertexAmplifiedElevation.Num() == VertexCount, TEXT("VertexAmplifiedElevation not initialized"));
    checkf(VertexElevationValues.Num() == VertexCount, TEXT("VertexElevationValues not initialized (must run erosion first)"));
    checkf(VertexCrustAge.Num() == VertexCount, TEXT("VertexCrustAge not initialized (must run oceanic dampening first)"));
    checkf(VertexRidgeDirections.Num() == VertexCount, TEXT("VertexRidgeDirections not initialized (must run ComputeRidgeDirections first)"));

    int32 DebugMismatchCount = 0;
    for (int32 VertexIdx = 0; VertexIdx < VertexCount; ++VertexIdx)
    {
        const FVector3d& VertexPosition = RenderVertices[VertexIdx];
        const int32 PlateID = VertexPlateAssignments.IsValidIndex(VertexIdx) ? VertexPlateAssignments[VertexIdx] : INDEX_NONE;
        const double BaseElevation_m = VertexElevationValues[VertexIdx];
        const double CrustAge_My = VertexCrustAge[VertexIdx];
        const FVector3d& RidgeDirection = VertexRidgeDirections[VertexIdx];

        // Debug Step 1: Verify vertex→plate mapping sanity
        if (PlateID != INDEX_NONE && Plates.IsValidIndex(PlateID))
        {
            const FTectonicPlate& Plate = Plates[PlateID];

            // Flag vertices with oceanic depths (-3500m) but continental plate assignment
            if (Plate.CrustType != ECrustType::Oceanic &&
                BaseElevation_m < Parameters.SeaLevel - 10.0 &&
                DebugMismatchCount < 3)
            {
                UE_LOG(LogTemp, Warning, TEXT("StageB: vertex %d depth %.1f m but plate %d marked %s"),
                    VertexIdx, BaseElevation_m, PlateID,
                    Plate.CrustType == ECrustType::Continental ? TEXT("continental") : TEXT("other"));
                DebugMismatchCount++;
            }
        }

        // Call amplification function from OceanicAmplification.cpp
        const double AmplifiedElevation = ComputeOceanicAmplification(
            VertexPosition,
            PlateID,
            CrustAge_My,
            BaseElevation_m,
            RidgeDirection,
            Plates,
            Boundaries
        );

        VertexAmplifiedElevation[VertexIdx] = AmplifiedElevation;
    }

    // Milestone 4 Phase 4.2: Increment surface data version (elevation changed)
    SurfaceDataVersion++;
}

// ============================================================================
// Milestone 6 Task 2.2: Continental Amplification (Stage B)
// ============================================================================

void UTectonicSimulationService::ApplyContinentalAmplification()
{
    // Paper Section 5: Apply exemplar-based amplification to continental crust
    // Terrain type classification + heightfield blending

    const int32 VertexCount = RenderVertices.Num();
    checkf(VertexAmplifiedElevation.Num() == VertexCount, TEXT("VertexAmplifiedElevation not initialized"));
    checkf(VertexElevationValues.Num() == VertexCount, TEXT("VertexElevationValues not initialized (must run erosion first)"));
    checkf(VertexCrustAge.Num() == VertexCount, TEXT("VertexCrustAge not initialized"));

    // Initialize VertexAmplifiedElevation from base if oceanic amplification didn't run
    // (VertexAmplifiedElevation is SetNumZeroed during render mesh generation)
    if (!Parameters.bEnableOceanicAmplification)
    {
        for (int32 i = 0; i < VertexCount; ++i)
        {
            VertexAmplifiedElevation[i] = VertexElevationValues[i];
        }
    }

    // Get project content directory for loading exemplar data
    const FString ProjectContentDir = FPaths::ProjectContentDir();

    for (int32 VertexIdx = 0; VertexIdx < VertexCount; ++VertexIdx)
    {
        const FVector3d& VertexPosition = RenderVertices[VertexIdx];
        const int32 PlateID = VertexPlateAssignments.IsValidIndex(VertexIdx) ? VertexPlateAssignments[VertexIdx] : INDEX_NONE;
        const double BaseElevation_m = VertexAmplifiedElevation[VertexIdx]; // Use oceanic-amplified elevation as base
        const double CrustAge_My = VertexCrustAge[VertexIdx];

        // Compute orogeny age and nearest boundary type (simplified for now)
        // TODO: Track orogeny age per-vertex in future milestone
        const double OrogenyAge_My = CrustAge_My; // Approximate: use crust age as orogeny age
        EBoundaryType NearestBoundaryType = EBoundaryType::Transform; // Default

        // Find nearest convergent boundary (for orogeny detection)
        double MinDistanceToBoundary = TNumericLimits<double>::Max();
        for (const auto& BoundaryPair : Boundaries)
        {
            const TPair<int32, int32>& BoundaryKey = BoundaryPair.Key;
            const FPlateBoundary& Boundary = BoundaryPair.Value;

            if (BoundaryKey.Key != PlateID && BoundaryKey.Value != PlateID)
                continue;

            // Simplified distance check (would need proper geodesic distance)
            if (Boundary.SharedEdgeVertices.Num() > 0)
            {
                // Use first vertex as representative
                const int32 BoundaryVertexIdx = Boundary.SharedEdgeVertices[0];
                if (RenderVertices.IsValidIndex(BoundaryVertexIdx))
                {
                    const double Distance = FVector3d::Distance(VertexPosition, RenderVertices[BoundaryVertexIdx]);
                    if (Distance < MinDistanceToBoundary)
                    {
                        MinDistanceToBoundary = Distance;
                        NearestBoundaryType = Boundary.BoundaryType;
                    }
                }
            }
        }

        // Call amplification function from ContinentalAmplification.cpp
        const double AmplifiedElevation = ComputeContinentalAmplification(
            VertexPosition,
            PlateID,
            BaseElevation_m,
            Plates,
            Boundaries,
            OrogenyAge_My,
            NearestBoundaryType,
            ProjectContentDir,
            Parameters.Seed
        );

        VertexAmplifiedElevation[VertexIdx] = AmplifiedElevation;
    }

    // Milestone 4 Phase 4.2: Increment surface data version (elevation changed)
    SurfaceDataVersion++;
}
