#pragma once

#include "CoreMinimal.h"

class UTectonicSimulationService;
namespace RealtimeMesh
{
    struct FRealtimeMeshStreamSet;
}

/** Milestone 3 Task 2.4: Elevation visualization mode. */
enum class EElevationMode : uint8
{
    Flat,       // Color-only heatmap (no geometric displacement)
    Displaced   // Geometric displacement + color heatmap
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

    /** Set boundary overlay visibility (Milestone 3 Task 3.2). */
    void SetBoundariesVisible(bool bVisible);

    /** Get boundary overlay visibility. */
    bool AreBoundariesVisible() const { return bShowBoundaries; }

private:
    UTectonicSimulationService* GetService() const;
    void EnsurePreviewActor() const;
    void UpdatePreviewMesh(RealtimeMesh::FRealtimeMeshStreamSet&& StreamSet, int32 VertexCount, int32 TriangleCount);
    void BuildAndUpdateMesh();
    void DrawBoundaryLines();

    mutable TWeakObjectPtr<UTectonicSimulationService> CachedService;
    mutable TWeakObjectPtr<class ARealtimeMeshActor> PreviewActor;
    mutable TWeakObjectPtr<class URealtimeMeshSimple> PreviewMesh;
    mutable bool bPreviewInitialized = false;

    /** Milestone 3 Task 2.2: Visualization mode toggle. */
    bool bShowVelocityField = false;

    /** Milestone 3 Task 2.4: Elevation visualization mode. */
    EElevationMode CurrentElevationMode = EElevationMode::Flat;

    /** Milestone 3 Task 3.2: Boundary overlay visibility. */
    bool bShowBoundaries = false;
};
