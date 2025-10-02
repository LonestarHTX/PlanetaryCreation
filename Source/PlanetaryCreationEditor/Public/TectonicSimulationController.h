#pragma once

#include "CoreMinimal.h"
#include "RealtimeMeshStreamRange.h"
#include "RealtimeMeshStreamSet.h"

class UTectonicSimulationService;

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

    /** Builds a placeholder mesh using the current simulation samples. */
    struct FPreviewMeshPayload
    {
        RealtimeMesh::FRealtimeMeshStreamSet StreamSet;
        int32 VertexCount = 0;
        int32 TriangleCount = 0;
    };

    FPreviewMeshPayload BuildPreviewMesh(float RadiusKm) const;

private:
    UTectonicSimulationService* GetService() const;
    void EnsurePreviewActor() const;
    void UpdatePreviewMesh(FPreviewMeshPayload&& Payload);

    mutable TWeakObjectPtr<UTectonicSimulationService> CachedService;
    mutable TWeakObjectPtr<class ARealtimeMeshActor> PreviewActor;
    mutable TWeakObjectPtr<class URealtimeMeshSimple> PreviewMesh;
    mutable bool bPreviewInitialized = false;
};
