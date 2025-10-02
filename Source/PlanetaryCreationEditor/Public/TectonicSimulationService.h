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
};

/** Simulation parameters (Phase 3 - UI integration). */
USTRUCT()
struct FTectonicSimulationParameters
{
    GENERATED_BODY()

    /** Random seed for deterministic plate generation. */
    UPROPERTY()
    int32 Seed = 42;

    // TODO(Milestone 3): Add PlateCount parameter once icosphere subdivision is implemented

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

    /** Accessor for the base sphere samples used to visualize placeholder geometry. */
    const TArray<FVector3d>& GetBaseSphereSamples() const { return BaseSphereSamples; }

    /** Accessor for plates (Milestone 2). */
    const TArray<FTectonicPlate>& GetPlates() const { return Plates; }

    /** Accessor for shared vertex pool (Milestone 2). */
    const TArray<FVector3d>& GetSharedVertices() const { return SharedVertices; }

    /** Accessor for boundary adjacency map (Milestone 2). */
    const TMap<TPair<int32, int32>, FPlateBoundary>& GetBoundaries() const { return Boundaries; }

    /** Accessor for simulation parameters (Milestone 2). */
    const FTectonicSimulationParameters& GetParameters() const { return Parameters; }

    /** Update simulation parameters and reset (Milestone 2 - Phase 3). */
    void SetParameters(const FTectonicSimulationParameters& NewParams);

    /** Export current simulation metrics to CSV (Milestone 2 - Phase 4). */
    void ExportMetricsToCSV();

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

    /** Phase 1 Task 1 helper: Validate solid angle coverage ≈ 4π. */
    void ValidateSolidAngleCoverage();

    /** Phase 2 Task 4: Migrate plate centroids using Euler pole rotations. */
    void MigratePlateCentroids(double DeltaTimeMy);

    /** Phase 2 Task 5: Update boundary classifications based on relative velocities. */
    void UpdateBoundaryClassifications();

    double CurrentTimeMy = 0.0;
    TArray<FVector3d> BaseSphereSamples;

    /** Milestone 2 state (Phase 1). */
    UPROPERTY()
    FTectonicSimulationParameters Parameters;

    TArray<FTectonicPlate> Plates;
    TArray<FVector3d> SharedVertices; // Shared vertex pool for plate polygons
    TMap<TPair<int32, int32>, FPlateBoundary> Boundaries; // Key: (PlateA_ID, PlateB_ID), sorted
};
