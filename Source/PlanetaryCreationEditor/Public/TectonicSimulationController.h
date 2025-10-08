#pragma once

#include "CoreMinimal.h"
#include "RealtimeMeshComponent/Public/Interface/Core/RealtimeMeshDataStream.h"
#include "OrbitCameraController.h"
#include "TectonicSimulationService.h"
#include "UObject/StrongObjectPtr.h"

class UTectonicSimulationService;
class UTexture2D;

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
    EElevationMode ElevationMode;
    ETectonicVisualizationMode VisualizationMode = ETectonicVisualizationMode::PlateColors;
    bool bUseAmplifiedElevation; // M6 Task 2.1: Use Stage B amplification for visualization

    /** Milestone 5 Phase 3: Planet radius in meters (for unit conversion to UE centimeters). */
    double PlanetRadius = 127400.0;

    /** Milestone 6 Task 2.3: Simulation parameters (needed for heightmap visualization toggle). */
    FTectonicSimulationParameters Parameters;

    /** Visualization flag: highlight sea level isoline. */
    bool bHighlightSeaLevel = false;

    /** Profiling data captured for the most recent Stage B amplification pass. */
    FStageBProfile StageBProfile;
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

    /** Precomputed vertex streams (SoA) captured after the initial build. */
    TArray<float> PositionX;
    TArray<float> PositionY;
    TArray<float> PositionZ;

    TArray<float> NormalX;
    TArray<float> NormalY;
    TArray<float> NormalZ;

    TArray<float> TangentX;
    TArray<float> TangentY;
    TArray<float> TangentZ;

    TArray<FColor> VertexColors;
    TArray<FVector2f> UVs;

    /** Final index buffer (post seam-duplication) recorded as uint32 values. */
    TArray<uint32> Indices;

    /** Mapping from final vertex buffer index back to source render vertex index (handles seam duplicates). */
    TArray<int32> SourceVertexIndices;
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

    /** Set primary visualization overlay. */
    void SetVisualizationMode(ETectonicVisualizationMode Mode);

    /** Get current visualization overlay. */
    ETectonicVisualizationMode GetVisualizationMode() const;

    /** Set elevation visualization mode (Milestone 3 Task 2.4). */
    void SetElevationMode(EElevationMode Mode);

    /** Get current elevation mode. */
    EElevationMode GetElevationMode() const { return CurrentElevationMode; }

    /** Set GPU preview mode (Milestone 6 - GPU displacement optimization). */
    void SetGPUPreviewMode(bool bEnabled);

    /** Get current GPU preview mode state. */
    bool IsGPUPreviewModeEnabled() const { return bUseGPUPreviewMode; }

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
    void BuildMeshFromSnapshot(int32 LODLevel, int32 TopologyVersion, const FMeshBuildSnapshot& Snapshot,
        RealtimeMesh::FRealtimeMeshStreamSet& OutStreamSet, int32& OutVertexCount, int32& OutTriangleCount,
        TArray<float>& OutPositionX, TArray<float>& OutPositionY, TArray<float>& OutPositionZ,
        TArray<float>& OutNormalX, TArray<float>& OutNormalY, TArray<float>& OutNormalZ,
        TArray<float>& OutTangentX, TArray<float>& OutTangentY, TArray<float>& OutTangentZ,
        TArray<FColor>& OutColors, TArray<FVector2f>& OutUVs, TArray<uint32>& OutIndices,
        TArray<int32>& OutSourceIndices);

    /** Milestone 4 Phase 4.2: Check if LOD mesh is cached and valid. */
    bool IsLODCached(int32 LODLevel, int32 TopologyVersion, int32 SurfaceDataVersion) const;

    /** Milestone 4 Phase 4.2: Get cached LOD mesh (returns nullptr if not cached). */
    const FCachedLODMesh* GetCachedLOD(int32 LODLevel, int32 TopologyVersion, int32 SurfaceDataVersion) const;
    FCachedLODMesh* GetMutableCachedLOD(int32 LODLevel);

    /** Milestone 4 Phase 4.2: Store built mesh snapshot in cache. */
    void CacheLODMesh(int32 LODLevel, int32 TopologyVersion, int32 SurfaceDataVersion,
        const FMeshBuildSnapshot& Snapshot, int32 VertexCount, int32 TriangleCount,
        TArray<float>&& PositionX, TArray<float>&& PositionY, TArray<float>&& PositionZ,
        TArray<float>&& NormalX, TArray<float>&& NormalY, TArray<float>&& NormalZ,
        TArray<float>&& TangentX, TArray<float>&& TangentY, TArray<float>&& TangentZ,
        TArray<FColor>&& VertexColors, TArray<FVector2f>&& UVs, TArray<uint32>&& Indices,
        TArray<int32>&& SourceVertexIndices);

    /** Milestone 4 Phase 4.2: Invalidate cache on topology change. */
    void InvalidateLODCache();

    /** Milestone 4 Phase 4.2: Pre-warm neighboring LOD levels. */
    void PreWarmNeighboringLODs();

    void BuildMeshFromCache(const FCachedLODMesh& CachedMesh,
        RealtimeMesh::FRealtimeMeshStreamSet& OutStreamSet, int32& OutVertexCount, int32& OutTriangleCount);

    void BuildMeshFromCacheWithColorRefresh(const FCachedLODMesh& CachedMesh,
        const FMeshBuildSnapshot& FreshSnapshot,
        RealtimeMesh::FRealtimeMeshStreamSet& OutStreamSet, int32& OutVertexCount, int32& OutTriangleCount);

    bool UpdateCachedMeshFromSnapshot(FCachedLODMesh& CachedMesh, const FMeshBuildSnapshot& Snapshot, int32 NewSurfaceDataVersion);
    void ApplyCachedMeshToPreview(const FCachedLODMesh& CachedMesh);

    void EnsureGPUPreviewTextureAsset() const;
    void CopyHeightTextureToPreviewResource() const;

    mutable TWeakObjectPtr<UTectonicSimulationService> CachedService;
    mutable TWeakObjectPtr<class ARealtimeMeshActor> PreviewActor;
    mutable TWeakObjectPtr<class URealtimeMeshSimple> PreviewMesh;
    mutable bool bPreviewInitialized = false;
    mutable bool bBoundaryOverlayInitialized = false;

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

    /** Milestone 6: GPU preview mode state (eliminates CPU readback stall). */
    mutable bool bUseGPUPreviewMode = false;
    mutable FTextureRHIRef GPUHeightTexture;
    mutable FIntPoint HeightTextureSize = FIntPoint(2048, 1024);
    mutable TStrongObjectPtr<UTexture2D> GPUHeightTextureAsset;

    struct FStaticLODData
    {
        TArray<FVector3f> UnitNormals;
        TArray<FVector2f> UVs;
        TArray<FVector3f> TangentX;
    };

    uint64 MakeLODCacheKey(int32 LODLevel, int32 TopologyVersion) const;
    const FStaticLODData& GetOrBuildStaticLODData(int32 LODLevel, int32 TopologyVersion, const TArray<FVector3d>& RenderVertices) const;

    mutable TMap<uint64, TUniquePtr<FStaticLODData>> StaticLODDataCache;
};
