#pragma once

#include "CoreMinimal.h"
#include "RealtimeMeshComponent/Public/Interface/Core/RealtimeMeshDataStream.h"
#include "OrbitCameraController.h"
#include "TectonicSimulationService.h"

class UTectonicSimulationService;

/** Milestone 3 Task 2.4: Elevation visualization mode. */
enum class EElevationMode : uint8
{
    Flat,       // Color-only heatmap (no geometric displacement)
    Displaced   // Geometric displacement + color heatmap
};

/** Milestone 3 Task 4.3: Snapshot of simulation state for async mesh build. */
struct FMeshBuildSnapshot
{
    TArray<FVector3d> RenderVertices;
    TArray<int32> RenderTriangles;
    TArray<int32> VertexPlateAssignments;
    TArray<FVector3d> VertexVelocities;
    TArray<double> VertexStressValues;
    TArray<double> VertexElevationValues; // M5 Phase 3.7: Actual elevations from erosion system
    TArray<double> VertexAmplifiedElevation; // M6 Task 2.1: Stage B amplified elevation (with transform faults)
    double ElevationScale;
    bool bShowVelocityField;
    EElevationMode ElevationMode;
    bool bUseAmplifiedElevation; // M6 Task 2.1: Use Stage B amplification for visualization

    /** Milestone 5 Phase 3: Planet radius in meters (for unit conversion to UE centimeters). */
    double PlanetRadius = 127400.0;

    /** Milestone 6 Task 2.3: Simulation parameters (needed for heightmap visualization toggle). */
    FTectonicSimulationParameters Parameters;

    /** Visualization flag: highlight sea level isoline. */
    bool bHighlightSeaLevel = false;
};

/** Milestone 4 Phase 4.2: Cached LOD mesh snapshot (snapshot of simulation state, not StreamSet). */
struct FCachedLODMesh
{
    FMeshBuildSnapshot Snapshot;
    int32 VertexCount = 0;
    int32 TriangleCount = 0;
    int32 TopologyVersion = 0;
    int32 SurfaceDataVersion = 0;
    double CacheTimestamp = 0.0; // For LRU eviction if needed
};

/** Encapsulates higher-level control over the tectonic simulation and mesh conversion. */
class FTectonicSimulationController
{
public:
    FTectonicSimulationController();
    ~FTectonicSimulationController();

    void Initialize();
    void Shutdown();

    void StepSimulation(int32 Steps);

    /** Rebuild the preview mesh without advancing simulation time. */
    void RebuildPreview();

    double GetCurrentTimeMy() const;

    /** Access to underlying simulation service (for UI parameter binding). */
    UTectonicSimulationService* GetSimulationService() const;

    /** Set visualization mode: false = plate colors, true = velocity field. */
    void SetVelocityVisualizationEnabled(bool bEnabled);

    /** Get current visualization mode. */
    bool IsVelocityVisualizationEnabled() const { return bShowVelocityField; }

    /** Set elevation visualization mode (Milestone 3 Task 2.4). */
    void SetElevationMode(EElevationMode Mode);

    /** Get current elevation mode. */
    EElevationMode GetElevationMode() const { return CurrentElevationMode; }

    /** Refresh preview colors without forcing a full geometry rebuild (used by UI toggles). */
    bool RefreshPreviewColors();

    /** Set boundary overlay visibility (Milestone 3 Task 3.2). */
    void SetBoundariesVisible(bool bVisible);

    /** Get boundary overlay visibility. */
    bool AreBoundariesVisible() const { return bShowBoundaries; }

    /** Milestone 4 Phase 4.1: Update camera distance and recompute target LOD. */
    void UpdateLOD();

    /** Milestone 4 Phase 4.1: Get current target LOD level. */
    int32 GetTargetLODLevel() const { return TargetLODLevel; }

    /** Milestone 4 Phase 4.2: Get cache statistics for debugging. */
    void GetCacheStats(int32& OutCachedLODs, int32& OutTotalCacheSize) const;

    /** Milestone 5 Task 1.2: Camera control methods. */
    void RotateCamera(float DeltaYaw, float DeltaPitch);
    void ZoomCamera(float DeltaDistance);
    void ResetCamera();
    void TickCamera(float DeltaTime);
    FVector2D GetCameraAngles() const;
    float GetCameraDistance() const;

    /** Milestone 3 Task 4.3: Create snapshot for async mesh build (public for testing). */
    FMeshBuildSnapshot CreateMeshBuildSnapshot() const;

private:
    UTectonicSimulationService* GetService() const;
    void EnsurePreviewActor() const;
    void UpdatePreviewMesh(RealtimeMesh::FRealtimeMeshStreamSet&& StreamSet, int32 VertexCount, int32 TriangleCount);
    void BuildAndUpdateMesh();
    void DrawBoundaryLines();

    /** Milestone 4 Task 3.1: Draw high-resolution boundary overlay tracing render mesh seams. */
    void DrawHighResolutionBoundaryOverlay();

    /** Milestone 4 Task 3.2: Draw velocity vector field at plate centroids. */
    void DrawVelocityVectorField();

    /** Milestone 3 Task 4.3: Build mesh StreamSet from snapshot (thread-safe). */
    static void BuildMeshFromSnapshot(const FMeshBuildSnapshot& Snapshot, RealtimeMesh::FRealtimeMeshStreamSet& OutStreamSet, int32& OutVertexCount, int32& OutTriangleCount);

    /** Milestone 4 Phase 4.2: Check if LOD mesh is cached and valid. */
    bool IsLODCached(int32 LODLevel, int32 TopologyVersion, int32 SurfaceDataVersion) const;

    /** Milestone 4 Phase 4.2: Get cached LOD mesh (returns nullptr if not cached). */
    const FCachedLODMesh* GetCachedLOD(int32 LODLevel, int32 TopologyVersion, int32 SurfaceDataVersion) const;

    /** Milestone 4 Phase 4.2: Store built mesh snapshot in cache. */
    void CacheLODMesh(int32 LODLevel, int32 TopologyVersion, int32 SurfaceDataVersion, const FMeshBuildSnapshot& Snapshot, int32 VertexCount, int32 TriangleCount);

    /** Milestone 4 Phase 4.2: Invalidate cache on topology change. */
    void InvalidateLODCache();

    /** Milestone 4 Phase 4.2: Pre-warm neighboring LOD levels. */
    void PreWarmNeighboringLODs();

    mutable TWeakObjectPtr<UTectonicSimulationService> CachedService;
    mutable TWeakObjectPtr<class ARealtimeMeshActor> PreviewActor;
    mutable TWeakObjectPtr<class URealtimeMeshSimple> PreviewMesh;
    mutable bool bPreviewInitialized = false;
    mutable bool bBoundaryOverlayInitialized = false;

    /** Milestone 3 Task 2.2: Visualization mode toggle. */
    bool bShowVelocityField = false;

    /** Milestone 3 Task 2.4: Elevation visualization mode. */
    EElevationMode CurrentElevationMode = EElevationMode::Flat;

    /** Milestone 3 Task 3.2: Boundary overlay visibility. */
    bool bShowBoundaries = false;

    /** Milestone 3 Task 4.3: Async mesh build state. */
    mutable std::atomic<bool> bAsyncMeshBuildInProgress{false};
    mutable std::atomic<int32> ActiveAsyncTasks{0};
    mutable std::atomic<bool> bShutdownRequested{false};
    mutable double LastMeshBuildTimeMs = 0.0;

    /** Milestone 4 Phase 4.1: LOD state. */
    int32 CurrentLODLevel = 2;  // Start at Level 2 (current default)
    int32 TargetLODLevel = 2;
    double LastCameraDistance = 0.0;

    /** Milestone 4 Phase 4.2: LOD mesh cache (key: LODLevel). */
    mutable TMap<int32, TUniquePtr<FCachedLODMesh>> LODCache;

    /** Milestone 4 Phase 4.2: Topology/surface version tracking for cache invalidation. */
    mutable int32 CachedTopologyVersion = 0;
    mutable int32 CachedSurfaceDataVersion = 0;

    /** Milestone 5 Task 1.2: Orbital camera controller. */
    mutable FOrbitCameraController CameraController;
};
