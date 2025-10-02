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

    double GetCurrentTimeMy() const;

    /** Access to underlying simulation service (for UI parameter binding). */
    UTectonicSimulationService* GetSimulationService() const;

    /** Builds a placeholder mesh using the current simulation samples. */
private:
    UTectonicSimulationService* GetService() const;
    void EnsurePreviewActor() const;
    void UpdatePreviewMesh(RealtimeMesh::FRealtimeMeshStreamSet&& StreamSet, int32 VertexCount, int32 TriangleCount);

    mutable TWeakObjectPtr<UTectonicSimulationService> CachedService;
    mutable TWeakObjectPtr<class ARealtimeMeshActor> PreviewActor;
    mutable TWeakObjectPtr<class URealtimeMeshSimple> PreviewMesh;
    mutable bool bPreviewInitialized = false;
};
