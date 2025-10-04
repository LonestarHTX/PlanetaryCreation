#pragma once

#include "CoreMinimal.h"
#include "Subsystems/UnrealEditorSubsystem.h"
#include "TectonicSimulationService.generated.h"

/** Crust type for a tectonic plate (from paper). */
UENUM()
enum class ECrustType : uint8
{
    Oceanic,
    Continental
};

/** Represents a single tectonic plate with double-precision state. */
USTRUCT()
struct FTectonicPlate
{
    GENERATED_BODY()

    /** Unique plate identifier. */
    UPROPERTY()
    int32 PlateID = INDEX_NONE;

    /** Centroid position on the unit sphere (double-precision). */
    FVector3d Centroid = FVector3d::ZeroVector;

    /** Euler pole axis (normalized) for rotation. */
    FVector3d EulerPoleAxis = FVector3d::ZAxisVector;

    /** Angular velocity around Euler pole (radians per My). */
    double AngularVelocity = 0.0;

    /** Crust type (oceanic vs continental). */
    UPROPERTY()
    ECrustType CrustType = ECrustType::Oceanic;

    /** Static crust thickness in km (deferred: dynamic updates in Milestone 3). */
    double CrustThickness = 7.0; // Default oceanic crust ~7km

    /** Indices of vertices forming this plate's polygon (into shared vertex array). */
    TArray<int32> VertexIndices;
};

/** Boundary classification based on relative velocity (from paper Section 3). */
UENUM()
enum class EBoundaryType : uint8
{
    Divergent,   // Ridge - plates separating
    Convergent,  // Subduction zone - plates colliding
    Transform    // Shear - plates sliding past
};

/** Boundary metadata between two plates. */
USTRUCT()
struct FPlateBoundary
{
    GENERATED_BODY()

    /** Shared edge vertex indices (2 vertices for icosphere edge). */
    TArray<int32> SharedEdgeVertices;

    /** Current boundary classification (updated each step). */
    UPROPERTY()
    EBoundaryType BoundaryType = EBoundaryType::Transform;

    /** Relative velocity magnitude at boundary (for logging/debug). */
    double RelativeVelocity = 0.0;

    /**
     * Milestone 3 Task 2.3: Accumulated stress at boundary (MPa, double precision).
     * COSMETIC VISUALIZATION ONLY - simplified model, not physically accurate.
     * - Convergent boundaries: accumulates stress (capped at 100 MPa)
     * - Divergent boundaries: decays toward zero (τ = 10 My)
     * - Transform boundaries: minimal accumulation
     */
    double AccumulatedStress = 0.0;
};

/** Simulation parameters (Phase 3 - UI integration). */
USTRUCT()
struct FTectonicSimulationParameters
{
    GENERATED_BODY()

    /** Random seed for deterministic plate generation. */
    UPROPERTY()
    int32 Seed = 42;

    /**
     * Plate subdivision level (0-3). Controls number of tectonic plates generated from icosahedron:
     * Level 0: 20 plates (baseline from paper, ~Earth's 7-15 major/minor plates)
     * Level 1: 80 plates (experimental high-resolution mode)
     * Level 2: 320 plates (experimental ultra-high resolution)
     * Level 3: 1280 plates (experimental maximum resolution)
     * Default: 0 (20 plates, aligns with Milestone 2 target of ~12-20 plates)
     */
    UPROPERTY()
    int32 SubdivisionLevel = 0;

    /** Render mesh subdivision level (0-6). Level 0=20, 1=80, 2=320, 3=1280, 4=5120, 5=20480, 6=81920 faces. */
    UPROPERTY()
    int32 RenderSubdivisionLevel = 0;

    /**
     * Milestone 3 Task 2.4: Elevation displacement scale.
     * Controls magnitude of geometric displacement from stress field.
     * 1.0 = realistic scale (100 MPa → ~10km elevation), 0.0 = flat (color only).
     */
    UPROPERTY()
    double ElevationScale = 1.0;

    /**
     * Milestone 3 Task 3.1: Lloyd relaxation iterations.
     * Number of iterations to evenly distribute plate centroids (0-10).
     * Default 8 typically achieves convergence. 0 = disabled.
     */
    UPROPERTY()
    int32 LloydIterations = 8;

    /**
     * Milestone 3 Task 3.3: Dynamic re-tessellation threshold (degrees).
     * When plate centroid drifts >N degrees from initial position, log warning.
     * Full re-tessellation implementation deferred to M4.
     */
    UPROPERTY()
    double RetessellationThresholdDegrees = 30.0;

    /**
     * Milestone 3 Task 3.3: Enable dynamic re-tessellation (framework stub).
     * Default false for M3. When true (M4+), triggers mesh rebuild when plates drift.
     */
    UPROPERTY()
    bool bEnableDynamicRetessellation = false;

    /** Mantle viscosity coefficient (placeholder - used in Milestone 3). */
    UPROPERTY()
    double MantleViscosity = 1.0;

    /** Thermal diffusion constant (placeholder - used in Milestone 3). */
    UPROPERTY()
    double ThermalDiffusion = 1.0;
};

/**
 * Editor-only subsystem that holds the canonical tectonic simulation state.
 * The state uses double precision so long-running editor sessions avoid drift.
 */
UCLASS()
class UTectonicSimulationService : public UUnrealEditorSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    /** Resets the simulation to the initial baseline. */
    void ResetSimulation();

    /** Advance the simulation by the requested number of steps (each 2 My). */
    void AdvanceSteps(int32 StepCount);

    /** Returns the accumulated tectonic time in mega-years. */
    double GetCurrentTimeMy() const { return CurrentTimeMy; }

    /** Returns the last step time in milliseconds (Milestone 3 Task 4.5). */
    double GetLastStepTimeMs() const { return LastStepTimeMs; }

    /** Accessor for the base sphere samples used to visualize placeholder geometry. */
    const TArray<FVector3d>& GetBaseSphereSamples() const { return BaseSphereSamples; }

    /** Accessor for plates (Milestone 2). */
    const TArray<FTectonicPlate>& GetPlates() const { return Plates; }

    /** Accessor for shared vertex pool (Milestone 2). */
    const TArray<FVector3d>& GetSharedVertices() const { return SharedVertices; }

    /** Accessor for render mesh vertices (Milestone 3 - separate from simulation vertices). */
    const TArray<FVector3d>& GetRenderVertices() const { return RenderVertices; }

    /** Accessor for render mesh triangle indices (Milestone 3). */
    const TArray<int32>& GetRenderTriangles() const { return RenderTriangles; }

    /** Accessor for vertex-to-plate assignments (Milestone 3 Phase 2). */
    const TArray<int32>& GetVertexPlateAssignments() const { return VertexPlateAssignments; }

    /** Accessor for per-vertex velocity vectors (Milestone 3 Task 2.2). */
    const TArray<FVector3d>& GetVertexVelocities() const { return VertexVelocities; }

    /** Accessor for per-vertex stress values (Milestone 3 Task 2.3, cosmetic). */
    const TArray<double>& GetVertexStressValues() const { return VertexStressValues; }

    /** Accessor for boundary adjacency map (Milestone 2). */
    const TMap<TPair<int32, int32>, FPlateBoundary>& GetBoundaries() const { return Boundaries; }

    /** Accessor for simulation parameters (Milestone 2). */
    const FTectonicSimulationParameters& GetParameters() const { return Parameters; }

    /** Update simulation parameters and reset (Milestone 2 - Phase 3). */
    void SetParameters(const FTectonicSimulationParameters& NewParams);

    /** Export current simulation metrics to CSV (Milestone 2 - Phase 4). */
    void ExportMetricsToCSV();

    /** Milestone 4 Task 1.1: Re-tessellation public API. */

    /** Re-tessellation snapshot structure for rollback. */
    struct FRetessellationSnapshot
    {
        TArray<FVector3d> SharedVertices;
        TArray<FVector3d> RenderVertices;
        TArray<int32> RenderTriangles;
        TArray<int32> VertexPlateAssignments;
        TMap<TPair<int32, int32>, FPlateBoundary> Boundaries;
        double TimestampMy;

        FRetessellationSnapshot() : TimestampMy(0.0) {}
    };

    /** Captures current state for rollback. */
    FRetessellationSnapshot CaptureRetessellationSnapshot() const;

    /** Restores state from snapshot after failed rebuild. */
    void RestoreRetessellationSnapshot(const FRetessellationSnapshot& Snapshot);

    /** Performs incremental re-tessellation for drifted plates. Returns true if successful. */
    bool PerformRetessellation();

    /** Validates re-tessellation result against snapshot. */
    bool ValidateRetessellation(const FRetessellationSnapshot& Snapshot) const;

    /** Milestone 4 Task 1.1: Re-tessellation performance tracking (public for tests). */
    double LastRetessellationTimeMs = 0.0;
    int32 RetessellationCount = 0;

private:
    void GenerateDefaultSphereSamples();

    /** Phase 1 Task 1: Generate icosphere-based plate tessellation. */
    void GenerateIcospherePlates();

    /** Phase 1 Task 2: Assign Euler poles to plates. */
    void InitializeEulerPoles();

    /** Phase 1 Task 3: Build boundary adjacency map from icosphere topology. */
    void BuildBoundaryAdjacencyMap();

    /** Phase 1 Task 1 helper: Subdivide icosphere to target plate count. */
    void SubdivideIcosphere(int32 SubdivisionLevel);

    /** Milestone 3 Task 1.1: Generate high-density render mesh from base icosphere. */
    void GenerateRenderMesh();

    /** Milestone 3 Task 1.1 helper: Subdivide a triangle by splitting edges. */
    int32 GetMidpointIndex(int32 V0, int32 V1, TMap<TPair<int32, int32>, int32>& MidpointCache, TArray<FVector3d>& Vertices);

    /** Milestone 3 Task 2.1: Build Voronoi mapping from render vertices to plates. */
    void BuildVoronoiMapping();

    /** Milestone 3 Task 2.2: Compute per-vertex velocity field (v = ω × r). */
    void ComputeVelocityField();

    /** Milestone 3 Task 2.3: Update stress at boundaries (cosmetic visualization). */
    void UpdateBoundaryStress(double DeltaTimeMy);

    /** Milestone 3 Task 2.3: Interpolate boundary stress to render vertices (Gaussian falloff). */
    void InterpolateStressToVertices();

    /** Milestone 3 Task 3.1: Apply Lloyd relaxation to evenly distribute plate centroids. */
    void ApplyLloydRelaxation();

    /** Milestone 3 Task 3.3: Check if plates have drifted beyond re-tessellation threshold. */
    void CheckRetessellationNeeded();

    /** Phase 1 Task 1 helper: Validate solid angle coverage ≈ 4π. */
    void ValidateSolidAngleCoverage();

    /** Phase 2 Task 4: Migrate plate centroids using Euler pole rotations. */
    void MigratePlateCentroids(double DeltaTimeMy);

    /** Phase 2 Task 5: Update boundary classifications based on relative velocities. */
    void UpdateBoundaryClassifications();

    double CurrentTimeMy = 0.0;
    double LastStepTimeMs = 0.0; // Milestone 3 Task 4.5: Performance tracking
    TArray<FVector3d> BaseSphereSamples;

    /** Milestone 2 state (Phase 1). */
    UPROPERTY()
    FTectonicSimulationParameters Parameters;

    TArray<FTectonicPlate> Plates;
    TArray<FVector3d> SharedVertices; // Shared vertex pool for plate polygons (simulation)
    TMap<TPair<int32, int32>, FPlateBoundary> Boundaries; // Key: (PlateA_ID, PlateB_ID), sorted

    /** Milestone 3: Separate render mesh geometry (high-density, independent from simulation). */
    TArray<FVector3d> RenderVertices;
    TArray<int32> RenderTriangles; // Triplets of indices into RenderVertices
    TArray<int32> VertexPlateAssignments; // Maps each RenderVertex index to a Plate ID (Voronoi cell)
    TArray<FVector3d> VertexVelocities; // Velocity vector (v = ω × r) for each RenderVertex (Task 2.2)
    TArray<double> VertexStressValues; // Interpolated stress (MPa) for each RenderVertex (Task 2.3, cosmetic)

    /** Milestone 3 Task 3.3: Initial plate centroid positions (captured after Lloyd relaxation). */
    TArray<FVector3d> InitialPlateCentroids;
};
