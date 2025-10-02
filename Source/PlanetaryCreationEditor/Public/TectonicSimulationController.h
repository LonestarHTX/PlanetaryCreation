#pragma once

#include "CoreMinimal.h"

class UTectonicSimulationService;
namespace RealtimeMesh
{
    struct FRealtimeMeshStreamSet;
}

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

private:
    UTectonicSimulationService* GetService() const;
    void EnsurePreviewActor() const;
    void UpdatePreviewMesh(RealtimeMesh::FRealtimeMeshStreamSet&& StreamSet, int32 VertexCount, int32 TriangleCount);
    void BuildAndUpdateMesh();

    mutable TWeakObjectPtr<UTectonicSimulationService> CachedService;
    mutable TWeakObjectPtr<class ARealtimeMeshActor> PreviewActor;
    mutable TWeakObjectPtr<class URealtimeMeshSimple> PreviewMesh;
    mutable bool bPreviewInitialized = false;
};
