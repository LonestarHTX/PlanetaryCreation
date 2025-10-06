#include "TectonicSimulationController.h"

#include "PlanetaryCreationLogging.h"

#include "Async/Async.h"
#include "Async/Future.h"
#include "Async/ParallelFor.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "RealtimeMeshComponent/Public/RealtimeMeshActor.h"
#include "RealtimeMeshComponent/Public/RealtimeMeshComponent.h"
#include "RealtimeMeshComponent/Public/RealtimeMeshSimple.h"
#include "RealtimeMeshComponent/Public/Interface/Core/RealtimeMeshBuilder.h"
#include "RealtimeMeshComponent/Public/Interface/Core/RealtimeMeshSectionConfig.h"
#include "RealtimeMeshComponent/Public/Interface/Core/RealtimeMeshStreamRange.h"
#include "UObject/UObjectGlobals.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "TectonicSimulationService.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/LineBatchComponent.h"
#include "Math/Quat.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#include "EditorViewportClient.h"
#include "HAL/PlatformTime.h"
#include "HeightmapColorPalette.h"

FTectonicSimulationController::FTectonicSimulationController() = default;
FTectonicSimulationController::~FTectonicSimulationController()
{
    Shutdown();
}

void FTectonicSimulationController::Initialize()
{
    CachedService = GetService();
}

void FTectonicSimulationController::Shutdown()
{
    bShutdownRequested.store(true, std::memory_order_relaxed);

    const double WaitStart = FPlatformTime::Seconds();
    while (ActiveAsyncTasks.load(std::memory_order_relaxed) > 0)
    {
        FPlatformProcess::Sleep(0.001f);

        // Safety: break after 5 seconds to avoid deadlock
        if ((FPlatformTime::Seconds() - WaitStart) > 5.0)
        {
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("[ASYNC] Shutdown timed out waiting for %d pending tasks"),
                ActiveAsyncTasks.load(std::memory_order_relaxed));
            break;
        }
    }

#if WITH_EDITOR
    if (PreviewActor.IsValid())
    {
        if (UWorld* PreviewWorld = PreviewActor->GetWorld())
        {
            PreviewWorld->DestroyActor(PreviewActor.Get());
        }
        PreviewActor.Reset();
    }
#endif

    PreviewMesh.Reset();
    bAsyncMeshBuildInProgress.store(false);
    ActiveAsyncTasks.store(0, std::memory_order_relaxed);
    CachedService.Reset();
    bShutdownRequested.store(false, std::memory_order_relaxed);
}

void FTectonicSimulationController::StepSimulation(int32 Steps)
{
    if (UTectonicSimulationService* Service = GetService())
    {
        // Milestone 4 Phase 4.1: Update LOD before stepping (camera may have moved)
        UpdateLOD();

        Service->AdvanceSteps(Steps);
        BuildAndUpdateMesh();
    }
}

void FTectonicSimulationController::RebuildPreview()
{
    // Milestone 4 Phase 4.1: Update LOD before rebuilding
    UpdateLOD();

    BuildAndUpdateMesh();
}

bool FTectonicSimulationController::RefreshPreviewColors()
{
    if (bAsyncMeshBuildInProgress.load(std::memory_order_relaxed))
    {
        return false;
    }

    UTectonicSimulationService* Service = GetService();
    if (!Service)
    {
        return false;
    }

    EnsurePreviewActor();
    if (!PreviewMesh.IsValid())
    {
        return false;
    }

    const int32 RenderLevel = Service->GetParameters().RenderSubdivisionLevel;
    const int32 CurrentTopologyVersion = Service->GetTopologyVersion();
    const int32 CurrentSurfaceVersion = Service->GetSurfaceDataVersion();

    FMeshBuildSnapshot Snapshot = CreateMeshBuildSnapshot();

    RealtimeMesh::FRealtimeMeshStreamSet StreamSet;
    int32 VertexCount = 0;
    int32 TriangleCount = 0;
    TArray<float> PositionX, PositionY, PositionZ;
    TArray<float> NormalX, NormalY, NormalZ;
    TArray<float> TangentX, TangentY, TangentZ;
    TArray<FColor> Colors;
    TArray<FVector2f> UVs;
    TArray<uint32> Indices;
    BuildMeshFromSnapshot(RenderLevel, CurrentTopologyVersion, Snapshot, StreamSet, VertexCount, TriangleCount,
        PositionX, PositionY, PositionZ,
        NormalX, NormalY, NormalZ,
        TangentX, TangentY, TangentZ,
        Colors, UVs, Indices);

    UpdatePreviewMesh(MoveTemp(StreamSet), VertexCount, TriangleCount);

    // Keep cache aligned with the latest visualization state so future pre-warm hits stay consistent.
    CacheLODMesh(RenderLevel, CurrentTopologyVersion, CurrentSurfaceVersion, Snapshot, VertexCount, TriangleCount,
        MoveTemp(PositionX), MoveTemp(PositionY), MoveTemp(PositionZ),
        MoveTemp(NormalX), MoveTemp(NormalY), MoveTemp(NormalZ),
        MoveTemp(TangentX), MoveTemp(TangentY), MoveTemp(TangentZ),
        MoveTemp(Colors), MoveTemp(UVs), MoveTemp(Indices));

    return true;
}

FMeshBuildSnapshot FTectonicSimulationController::CreateMeshBuildSnapshot() const
{
    FMeshBuildSnapshot Snapshot;

    if (UTectonicSimulationService* Service = GetService())
    {
        // Deep-copy render state from service (thread-safe snapshot)
        Snapshot.RenderVertices = Service->GetRenderVertices();
        Snapshot.RenderTriangles = Service->GetRenderTriangles();
        Snapshot.VertexPlateAssignments = Service->GetVertexPlateAssignments();
        Snapshot.VertexVelocities = Service->GetVertexVelocities();
        Snapshot.VertexStressValues = Service->GetVertexStressValues();
        Snapshot.VertexElevationValues = Service->GetVertexElevationValues(); // M5 Phase 3.7: Use actual elevations from erosion
        Snapshot.VertexAmplifiedElevation = Service->GetVertexAmplifiedElevation(); // M6 Task 2.1: Stage B amplified elevation
        Snapshot.ElevationScale = Service->GetParameters().ElevationScale;
        Snapshot.PlanetRadius = Service->GetParameters().PlanetRadius; // M5 Phase 3: For unit conversion

        // DEBUG: Log elevation array state
#if !UE_BUILD_SHIPPING
        if (Snapshot.VertexElevationValues.Num() > 0)
        {
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("[DEBUG] Snapshot has %d elevations, first 3: [%.2f, %.2f, %.2f]"),
                Snapshot.VertexElevationValues.Num(),
                Snapshot.VertexElevationValues[0],
                Snapshot.VertexElevationValues.Num() > 1 ? Snapshot.VertexElevationValues[1] : 0.0,
                Snapshot.VertexElevationValues.Num() > 2 ? Snapshot.VertexElevationValues[2] : 0.0);
        }
        else
        {
            UE_LOG(LogPlanetaryCreation, Error, TEXT("[DEBUG] Snapshot VertexElevationValues is EMPTY!"));
        }
#endif
        // M6 Task 2.3: Enable amplified elevation if EITHER oceanic OR continental amplification is active
        Snapshot.bUseAmplifiedElevation = (Service->GetParameters().bEnableOceanicAmplification ||
                                           Service->GetParameters().bEnableContinentalAmplification) &&
                                          Service->GetParameters().RenderSubdivisionLevel >= Service->GetParameters().MinAmplificationLOD;
        Snapshot.Parameters = Service->GetParameters(); // M6 Task 2.3: For heightmap visualization mode
        Snapshot.bHighlightSeaLevel = Service->IsHighlightSeaLevelEnabled();

        // DEBUG: Log heightmap visualization state
#if !UE_BUILD_SHIPPING
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("[DEBUG] CreateMeshBuildSnapshot: bEnableHeightmapVisualization = %s"),
            Snapshot.Parameters.bEnableHeightmapVisualization ? TEXT("TRUE") : TEXT("FALSE"));
#endif
    }

    // Capture visualization state from controller
    Snapshot.bShowVelocityField = bShowVelocityField;
    Snapshot.ElevationMode = CurrentElevationMode;

    return Snapshot;
}

void FTectonicSimulationController::BuildAndUpdateMesh()
{
    UTectonicSimulationService* Service = GetService();
    if (!Service)
    {
        return;
    }

    EnsurePreviewActor();

    const int32 RenderLevel = Service->GetParameters().RenderSubdivisionLevel;
    const int32 CurrentTopologyVersion = Service->GetTopologyVersion();
    const int32 CurrentSurfaceVersion = Service->GetSurfaceDataVersion();

    // Milestone 4 Phase 4.2: Check cache first
    const FCachedLODMesh* CachedMesh = GetCachedLOD(RenderLevel, CurrentTopologyVersion, CurrentSurfaceVersion);
    if (CachedMesh)
    {
        UE_LOG(LogPlanetaryCreation, Log, TEXT("[LOD Cache] Using cached L%d: %d verts, %d tris (cache hit)"),
            RenderLevel, CachedMesh->VertexCount, CachedMesh->TriangleCount);

        // Rebuild from cached snapshot (fast, already computed)
        RealtimeMesh::FRealtimeMeshStreamSet StreamSet;
        int32 VertexCount = 0;
        int32 TriangleCount = 0;
        BuildMeshFromCache(*CachedMesh, StreamSet, VertexCount, TriangleCount);
        UpdatePreviewMesh(MoveTemp(StreamSet), VertexCount, TriangleCount);

        // Pre-warm neighboring LODs opportunistically
        PreWarmNeighboringLODs();

        return;
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("[LOD Cache] L%d not cached, building... (Topo:%d, Surface:%d)"),
        RenderLevel, CurrentTopologyVersion, CurrentSurfaceVersion);

    // Milestone 3 Task 4.3: Threshold check - async only for level 3+ (1280+ triangles)
    // Level 0-2 use synchronous path (fast enough, not worth threading overhead)
    if (RenderLevel <= 2)
    {
        // Synchronous path for low-density meshes
        const uint32 ThreadID = FPlatformTLS::GetCurrentThreadId();
        const double StartTime = FPlatformTime::Seconds();

        FMeshBuildSnapshot Snapshot = CreateMeshBuildSnapshot();
        RealtimeMesh::FRealtimeMeshStreamSet StreamSet;
        int32 VertexCount = 0;
        int32 TriangleCount = 0;

        TArray<float> PositionX, PositionY, PositionZ;
        TArray<float> NormalX, NormalY, NormalZ;
        TArray<float> TangentX, TangentY, TangentZ;
        TArray<FColor> Colors;
        TArray<FVector2f> UVs;
        TArray<uint32> Indices;

        BuildMeshFromSnapshot(RenderLevel, CurrentTopologyVersion, Snapshot, StreamSet, VertexCount, TriangleCount,
            PositionX, PositionY, PositionZ,
            NormalX, NormalY, NormalZ,
            TangentX, TangentY, TangentZ,
            Colors, UVs, Indices);

        const double EndTime = FPlatformTime::Seconds();
        LastMeshBuildTimeMs = (EndTime - StartTime) * 1000.0;

        UE_LOG(LogPlanetaryCreation, Log, TEXT("[SYNC] Mesh build: %d verts, %d tris, %.2fms (ThreadID: %u, level %d)"),
            VertexCount, TriangleCount, LastMeshBuildTimeMs, ThreadID, RenderLevel);

        // Milestone 4 Phase 4.2: Cache the snapshot before using it
        CacheLODMesh(RenderLevel, CurrentTopologyVersion, CurrentSurfaceVersion, Snapshot, VertexCount, TriangleCount,
            MoveTemp(PositionX), MoveTemp(PositionY), MoveTemp(PositionZ),
            MoveTemp(NormalX), MoveTemp(NormalY), MoveTemp(NormalZ),
            MoveTemp(TangentX), MoveTemp(TangentY), MoveTemp(TangentZ),
            MoveTemp(Colors), MoveTemp(UVs), MoveTemp(Indices));

        UpdatePreviewMesh(MoveTemp(StreamSet), VertexCount, TriangleCount);

        // Pre-warm neighboring LODs opportunistically
        PreWarmNeighboringLODs();
    }
    else
    {
        // Asynchronous path for high-density meshes (level 3+)
        if (bAsyncMeshBuildInProgress.load())
        {
            UE_LOG(LogPlanetaryCreation, Warning, TEXT("[ASYNC] Skipping mesh rebuild - async build already in progress (rapid stepping detected)"));
            return; // Skip if already building
        }

        bAsyncMeshBuildInProgress.store(true);
        const double StartTime = FPlatformTime::Seconds();

        // Create snapshot on game thread (captures current simulation state)
        FMeshBuildSnapshot Snapshot = CreateMeshBuildSnapshot();
        ActiveAsyncTasks.fetch_add(1, std::memory_order_relaxed);

        // Kick off async mesh build on background thread
        AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, Snapshot, StartTime, CurrentTopologyVersion, CurrentSurfaceVersion, RenderLevel]() mutable
        {
            if (bShutdownRequested.load(std::memory_order_relaxed))
            {
                ActiveAsyncTasks.fetch_sub(1, std::memory_order_relaxed);
                bAsyncMeshBuildInProgress.store(false);
                return;
            }

            const uint32 BackgroundThreadID = FPlatformTLS::GetCurrentThreadId();
            UE_LOG(LogPlanetaryCreation, Log, TEXT("[ASYNC] Building mesh on background thread (ThreadID: %u)"), BackgroundThreadID);

            // Build mesh on background thread (thread-safe, no UObject access)
            RealtimeMesh::FRealtimeMeshStreamSet StreamSet;
            int32 VertexCount = 0;
            int32 TriangleCount = 0;

            TArray<float> PositionX, PositionY, PositionZ;
            TArray<float> NormalX, NormalY, NormalZ;
            TArray<float> TangentX, TangentY, TangentZ;
            TArray<FColor> Colors;
            TArray<FVector2f> UVs;
            TArray<uint32> Indices;

            BuildMeshFromSnapshot(RenderLevel, CurrentTopologyVersion, Snapshot, StreamSet, VertexCount, TriangleCount,
                PositionX, PositionY, PositionZ,
                NormalX, NormalY, NormalZ,
                TangentX, TangentY, TangentZ,
                Colors, UVs, Indices);

            const double EndTime = FPlatformTime::Seconds();
            const double BuildTimeMs = (EndTime - StartTime) * 1000.0;

            // Return to game thread to apply mesh update
            AsyncTask(ENamedThreads::GameThread, [this,
                StreamSet = MoveTemp(StreamSet),
                PositionX = MoveTemp(PositionX), PositionY = MoveTemp(PositionY), PositionZ = MoveTemp(PositionZ),
                NormalX = MoveTemp(NormalX), NormalY = MoveTemp(NormalY), NormalZ = MoveTemp(NormalZ),
                TangentX = MoveTemp(TangentX), TangentY = MoveTemp(TangentY), TangentZ = MoveTemp(TangentZ),
                Colors = MoveTemp(Colors), UVs = MoveTemp(UVs), Indices = MoveTemp(Indices),
                VertexCount, TriangleCount, BuildTimeMs, BackgroundThreadID, Snapshot, CurrentTopologyVersion, CurrentSurfaceVersion, RenderLevel]() mutable
            {
                const uint32 GameThreadID = FPlatformTLS::GetCurrentThreadId();

                if (bShutdownRequested.load(std::memory_order_relaxed))
                {
                    bAsyncMeshBuildInProgress.store(false);
                    ActiveAsyncTasks.fetch_sub(1, std::memory_order_relaxed);
                    return;
                }

                LastMeshBuildTimeMs = BuildTimeMs;

                UE_LOG(LogPlanetaryCreation, Log, TEXT("[ASYNC] Mesh build completed: %d verts, %d tris, %.2fms (Background: %u -> Game: %u)"),
                    VertexCount, TriangleCount, LastMeshBuildTimeMs, BackgroundThreadID, GameThreadID);

                // Milestone 4 Phase 4.2: Cache the snapshot before using it
                CacheLODMesh(RenderLevel, CurrentTopologyVersion, CurrentSurfaceVersion, Snapshot, VertexCount, TriangleCount,
                    MoveTemp(PositionX), MoveTemp(PositionY), MoveTemp(PositionZ),
                    MoveTemp(NormalX), MoveTemp(NormalY), MoveTemp(NormalZ),
                    MoveTemp(TangentX), MoveTemp(TangentY), MoveTemp(TangentZ),
                    MoveTemp(Colors), MoveTemp(UVs), MoveTemp(Indices));

                UpdatePreviewMesh(MoveTemp(StreamSet), VertexCount, TriangleCount);
                bAsyncMeshBuildInProgress.store(false);

                // Pre-warm neighboring LODs opportunistically
                PreWarmNeighboringLODs();

                ActiveAsyncTasks.fetch_sub(1, std::memory_order_relaxed);
            });
        });

        const uint32 MainThreadID = FPlatformTLS::GetCurrentThreadId();
        UE_LOG(LogPlanetaryCreation, Log, TEXT("[ASYNC] Mesh build dispatched from game thread (ThreadID: %u, level %d)"), MainThreadID, RenderLevel);
    }
}

double FTectonicSimulationController::GetCurrentTimeMy() const
{
    if (const UTectonicSimulationService* Service = GetService())
    {
        return Service->GetCurrentTimeMy();
    }
    return 0.0;
}

UTectonicSimulationService* FTectonicSimulationController::GetSimulationService() const
{
    return GetService();
}

uint64 FTectonicSimulationController::MakeLODCacheKey(int32 LODLevel, int32 TopologyVersion) const
{
    return (static_cast<uint64>(TopologyVersion) << 32) | static_cast<uint32>(LODLevel);
}

const FTectonicSimulationController::FStaticLODData& FTectonicSimulationController::GetOrBuildStaticLODData(int32 LODLevel, int32 TopologyVersion, const TArray<FVector3d>& RenderVertices) const
{
    const uint64 CacheKey = MakeLODCacheKey(LODLevel, TopologyVersion);
    TUniquePtr<FStaticLODData>& DataPtr = StaticLODDataCache.FindOrAdd(CacheKey);

    if (!DataPtr.IsValid())
    {
        DataPtr = MakeUnique<FStaticLODData>();
    }

    FStaticLODData& Data = *DataPtr;
    if (Data.UVs.Num() != RenderVertices.Num())
    {
        const int32 VertexCount = RenderVertices.Num();
        Data.UnitNormals.SetNum(VertexCount);
        Data.UVs.SetNum(VertexCount);
        Data.TangentX.SetNum(VertexCount);

        const TArray<float>* CachedPosX = nullptr;
        const TArray<float>* CachedPosY = nullptr;
        const TArray<float>* CachedPosZ = nullptr;
        const TArray<float>* CachedNormalX = nullptr;
        const TArray<float>* CachedNormalY = nullptr;
        const TArray<float>* CachedNormalZ = nullptr;
        const TArray<float>* CachedTangentXSoA = nullptr;
        const TArray<float>* CachedTangentYSoA = nullptr;
        const TArray<float>* CachedTangentZSoA = nullptr;

        if (const UTectonicSimulationService* Service = GetService())
        {
            Service->GetRenderVertexFloatSoA(CachedPosX, CachedPosY, CachedPosZ,
                CachedNormalX, CachedNormalY, CachedNormalZ,
                CachedTangentXSoA, CachedTangentYSoA, CachedTangentZSoA);
        }

        const bool bUseCachedNormals = CachedNormalX && CachedNormalY && CachedNormalZ &&
            CachedNormalX->Num() == VertexCount &&
            CachedNormalY->Num() == VertexCount &&
            CachedNormalZ->Num() == VertexCount;
        const bool bUseCachedTangents = CachedTangentXSoA && CachedTangentYSoA && CachedTangentZSoA &&
            CachedTangentXSoA->Num() == VertexCount &&
            CachedTangentYSoA->Num() == VertexCount &&
            CachedTangentZSoA->Num() == VertexCount;

        for (int32 Index = 0; Index < VertexCount; ++Index)
        {
            const FVector3d& Vertex = RenderVertices[Index];
            FVector3f UnitNormal;
            if (bUseCachedNormals && CachedNormalX->IsValidIndex(Index) && CachedNormalY->IsValidIndex(Index) && CachedNormalZ->IsValidIndex(Index))
            {
                UnitNormal = FVector3f((*CachedNormalX)[Index], (*CachedNormalY)[Index], (*CachedNormalZ)[Index]);
            }
            else
            {
                UnitNormal = FVector3f(Vertex.GetSafeNormal());
            }

            if (UnitNormal.IsNearlyZero())
            {
                const FVector3f SafeNormal = FVector3f(Vertex.GetSafeNormal());
                UnitNormal = SafeNormal.IsNearlyZero() ? FVector3f::ZAxisVector : SafeNormal;
            }

            Data.UnitNormals[Index] = UnitNormal;

            const double UAngle = FMath::Atan2(static_cast<double>(UnitNormal.Y), static_cast<double>(UnitNormal.X));
            const double VAngle = FMath::Asin(FMath::Clamp(static_cast<double>(UnitNormal.Z), -1.0, 1.0));
            const float U = 0.5f + static_cast<float>(UAngle / (2.0 * PI));
            const float V = 0.5f - static_cast<float>(VAngle / PI);
            Data.UVs[Index] = FVector2f(U, V);

            const FVector3f UpVector = (FMath::Abs(UnitNormal.Z) > 0.99f) ? FVector3f(1.0f, 0.0f, 0.0f) : FVector3f(0.0f, 0.0f, 1.0f);
            FVector3f Tangent = FVector3f::CrossProduct(UnitNormal, UpVector).GetSafeNormal();
            if (Tangent.IsNearlyZero())
            {
                Tangent = FVector3f(1.0f, 0.0f, 0.0f);
            }
            Data.TangentX[Index] = Tangent;
        }
    }

    return Data;
}

void FTectonicSimulationController::SetVelocityVisualizationEnabled(bool bEnabled)
{
    if (bShowVelocityField != bEnabled)
    {
        bShowVelocityField = bEnabled;
        if (!RefreshPreviewColors())
        {
            RebuildPreview(); // Fallback when geometry/LOD state forces rebuild
        }
        DrawVelocityVectorField(); // Milestone 4 Task 3.2: Draw velocity arrows
    }
}

void FTectonicSimulationController::SetElevationMode(EElevationMode Mode)
{
    if (CurrentElevationMode != Mode)
    {
        CurrentElevationMode = Mode;
        RebuildPreview(); // Refresh mesh with new elevation mode
    }
}

void FTectonicSimulationController::SetBoundariesVisible(bool bVisible)
{
    if (bShowBoundaries != bVisible)
    {
        bShowBoundaries = bVisible;
        DrawHighResolutionBoundaryOverlay(); // Milestone 4 Task 3.1: High-res overlay
    }
}

UTectonicSimulationService* FTectonicSimulationController::GetService() const
{
    if (CachedService.IsValid())
    {
        return CachedService.Get();
    }

#if WITH_EDITOR
    if (GEditor)
    {
        UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
        if (Service)
        {
            CachedService = Service;
            return Service;
        }
    }
#endif

    return nullptr;
}

void FTectonicSimulationController::EnsurePreviewActor() const
{
    if (PreviewActor.IsValid() && PreviewMesh.IsValid())
    {
        return;
    }

#if WITH_EDITOR
    if (!GEditor)
    {
        return;
    }

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        return;
    }

    // Clean up stale actor if it exists but weak pointer is invalid
    if (!PreviewActor.IsValid())
    {
        for (TActorIterator<ARealtimeMeshActor> It(World); It; ++It)
        {
            if (It->GetActorLabel() == TEXT("TectonicPreviewActor"))
            {
                World->DestroyActor(*It);
                break;
            }
        }
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.Name = MakeUniqueObjectName(World->PersistentLevel, ARealtimeMeshActor::StaticClass(), TEXT("TectonicPreviewActor"));
    SpawnParams.ObjectFlags = RF_Transient;
    SpawnParams.OverrideLevel = World->PersistentLevel;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    ARealtimeMeshActor* Actor = World->SpawnActor<ARealtimeMeshActor>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    if (!Actor)
    {
        return;
    }

    Actor->SetActorHiddenInGame(true);
    Actor->SetIsTemporarilyHiddenInEditor(false);
    Actor->SetActorLabel(TEXT("TectonicPreviewActor"));

    PreviewActor = Actor;

    // Milestone 5 Task 1.2: Initialize orbital camera controller with planet radius
    UTectonicSimulationService* Service = GetService();
    const double PlanetRadiusMeters = Service ? Service->GetParameters().PlanetRadius : 127400.0;
    CameraController.Initialize(Actor, PlanetRadiusMeters);

    if (URealtimeMeshComponent* Component = Actor->GetRealtimeMeshComponent())
    {
        Component->SetMobility(EComponentMobility::Movable);

        // Disable raytracing and expensive rendering features for editor preview
        Component->SetCastShadow(false);
        Component->SetVisibleInRayTracing(false);
        Component->SetAffectDistanceFieldLighting(false);
        Component->SetAffectDynamicIndirectLighting(false);

        if (URealtimeMeshSimple* Mesh = Component->InitializeRealtimeMesh<URealtimeMeshSimple>())
        {
            Mesh->SetupMaterialSlot(0, TEXT("TectonicPreview"));

            // Create simple unlit material that displays vertex colors
            UMaterial* VertexColorMaterial = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
            VertexColorMaterial->MaterialDomain = EMaterialDomain::MD_Surface;
            VertexColorMaterial->SetShadingModel(EMaterialShadingModel::MSM_Unlit);

            UMaterialExpressionVertexColor* VertexColorNode = NewObject<UMaterialExpressionVertexColor>(VertexColorMaterial);
            VertexColorMaterial->GetExpressionCollection().AddExpression(VertexColorNode);
            VertexColorMaterial->GetEditorOnlyData()->EmissiveColor.Expression = VertexColorNode;

            VertexColorMaterial->PostEditChange();

            Component->SetMaterial(0, VertexColorMaterial);

            PreviewMesh = Mesh;
            bPreviewInitialized = false;
        }
    }
#endif
}

void FTectonicSimulationController::UpdatePreviewMesh(RealtimeMesh::FRealtimeMeshStreamSet&& StreamSet, int32 VertexCount, int32 TriangleCount)
{
    if (!PreviewMesh.IsValid())
    {
        return;
    }

    URealtimeMeshSimple* Mesh = PreviewMesh.Get();
    const FRealtimeMeshSectionGroupKey GroupKey = FRealtimeMeshSectionGroupKey::Create(0, FName(TEXT("TectonicPreview")));
    const FRealtimeMeshSectionKey SectionKey = FRealtimeMeshSectionKey::CreateForPolyGroup(GroupKey, 0);

    if (!bPreviewInitialized)
    {
        Mesh->CreateSectionGroup(GroupKey, MoveTemp(StreamSet));
        Mesh->UpdateSectionConfig(SectionKey, FRealtimeMeshSectionConfig(0));
        bPreviewInitialized = true;
    }
    else
    {
        Mesh->UpdateSectionGroup(GroupKey, MoveTemp(StreamSet));
    }

    const int32 ClampedVertices = FMath::Max(VertexCount, 0);
    const int32 ClampedTriangles = FMath::Max(TriangleCount, 0);
    const FRealtimeMeshStreamRange Range(0, ClampedVertices, 0, ClampedTriangles * 3);
    Mesh->UpdateSectionRange(SectionKey, Range);

    // Milestone 4 Task 3.1: Refresh high-res boundary overlay after mesh update
    DrawHighResolutionBoundaryOverlay();

    // Milestone 4 Task 3.2: Refresh velocity vector field after mesh update
    DrawVelocityVectorField();
}

void FTectonicSimulationController::DrawBoundaryLines()
{
#if WITH_EDITOR
    // Clear existing lines if boundaries are hidden or no world/service available
    if (!GEditor)
    {
        return;
    }

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        return;
    }

    ULineBatchComponent* LineBatcher = World->PersistentLineBatcher;
    if (!LineBatcher)
    {
        LineBatcher = World->LineBatcher;
    }
    if (!LineBatcher)
    {
        LineBatcher = NewObject<ULineBatchComponent>(World);
        LineBatcher->RegisterComponentWithWorld(World);
        World->PersistentLineBatcher = LineBatcher;
    }
    if (!LineBatcher)
    {
        return;
    }

    // Clear previous boundary lines for our batch only (avoid nuking other debug layers)
    constexpr uint32 BoundaryBatchId = 0x42544F4C; // 'BTOL' (Boundary Toggle Overlay Lines)
    LineBatcher->ClearBatch(BoundaryBatchId);

    if (!bShowBoundaries)
    {
        return; // Overlay hidden – nothing more to draw
    }

    UTectonicSimulationService* Service = GetService();
    if (!Service)
    {
        return;
    }

    const TArray<FTectonicPlate>& Plates = Service->GetPlates();
    const TMap<TPair<int32, int32>, FPlateBoundary>& Boundaries = Service->GetBoundaries();
    const TArray<FVector3d>& SharedVertices = Service->GetSharedVertices();
    const double CurrentTimeMy = Service->GetCurrentTimeMy();

    UE_LOG(LogPlanetaryCreation, Verbose, TEXT("Drawing %d boundaries at time %.2f My"), Boundaries.Num(), CurrentTimeMy);

    // M5 Phase 3: Convert planet radius from meters to UE centimeters
    const float RadiusUE = MetersToUE(Service->GetParameters().PlanetRadius);
    constexpr float LineThickness = 20.0f; // Thick lines for visibility
    constexpr float LineDuration = 0.0f; // Persistent (cleared manually)

    auto GetBoundaryColor = [](EBoundaryType Type) -> FColor
    {
        switch (Type)
        {
        case EBoundaryType::Convergent: return FColor::Red;
        case EBoundaryType::Divergent:  return FColor::Green;
        case EBoundaryType::Transform:  return FColor::Yellow;
        default:                         return FColor::White;
        }
    };

    auto RotateVertex = [](const FVector3d& Vertex, const FVector3d& Axis, double AngleRadians) -> FVector3d
    {
        if (Axis.IsNearlyZero())
        {
            return Vertex;
        }

        const UE::Math::TQuat<double> Rotation(Axis.GetSafeNormal(), AngleRadians);
        return Rotation.RotateVector(Vertex);
    };

    for (const auto& BoundaryPair : Boundaries)
    {
        const TPair<int32, int32>& Key = BoundaryPair.Key;
        const FPlateBoundary& Boundary = BoundaryPair.Value;

        if (Boundary.SharedEdgeVertices.Num() < 2)
        {
            continue;
        }

        const int32 V0Index = Boundary.SharedEdgeVertices[0];
        const int32 V1Index = Boundary.SharedEdgeVertices[1];
        if (!SharedVertices.IsValidIndex(V0Index) || !SharedVertices.IsValidIndex(V1Index))
        {
            continue;
        }

        const FTectonicPlate* PlateA = Plates.FindByPredicate([&Key](const FTectonicPlate& P) { return P.PlateID == Key.Key; });
        const FTectonicPlate* PlateB = Plates.FindByPredicate([&Key](const FTectonicPlate& P) { return P.PlateID == Key.Value; });
        if (!PlateA || !PlateB)
        {
            continue;
        }

        // Milestone 3 Task 3.2: Draw boundary as centroid→midpoint→centroid segments
        // (per plan: "Draw line segment from PlateA centroid → midpoint → PlateB centroid")
        const FVector3d& V0Original = SharedVertices[V0Index];
        const FVector3d& V1Original = SharedVertices[V1Index];

        const double RotationAngleA = PlateA->AngularVelocity * CurrentTimeMy;
        const double RotationAngleB = PlateB->AngularVelocity * CurrentTimeMy;

        const FVector3d V0FromA = RotateVertex(V0Original, PlateA->EulerPoleAxis, RotationAngleA);
        const FVector3d V1FromA = RotateVertex(V1Original, PlateA->EulerPoleAxis, RotationAngleA);
        const FVector3d V0FromB = RotateVertex(V0Original, PlateB->EulerPoleAxis, RotationAngleB);
        const FVector3d V1FromB = RotateVertex(V1Original, PlateB->EulerPoleAxis, RotationAngleB);

        // Average both plate rotations so the overlay sits between them.
        const FVector3d V0Current = ((V0FromA + V0FromB) * 0.5).GetSafeNormal();
        const FVector3d V1Current = ((V1FromA + V1FromB) * 0.5).GetSafeNormal();

        if (V0Current.IsNearlyZero() || V1Current.IsNearlyZero())
        {
            continue;
        }

        const FVector3d BoundaryMidpoint = ((V0Current + V1Current) * 0.5).GetSafeNormal();
        if (BoundaryMidpoint.IsNearlyZero())
        {
            continue;
        }

        // Offset boundaries slightly above mesh surface to prevent z-fighting
        // Use max elevation (10 km = 10000 m) + small buffer to ensure visibility above displaced geometry
        constexpr float BoundaryOffsetMeters = 15000.0f; // 15 km in meters
        const float BoundaryOffsetUE = MetersToUE(BoundaryOffsetMeters);
        const FVector CentroidA = FVector(PlateA->Centroid * (RadiusUE + BoundaryOffsetUE));
        const FVector Midpoint = FVector(BoundaryMidpoint * (RadiusUE + BoundaryOffsetUE));
        const FVector CentroidB = FVector(PlateB->Centroid * (RadiusUE + BoundaryOffsetUE));

        const FColor LineColor = GetBoundaryColor(Boundary.BoundaryType);

        // Draw two segments: PlateA centroid → midpoint, midpoint → PlateB centroid
        LineBatcher->DrawLine(CentroidA, Midpoint, LineColor, SDPG_World, LineThickness, LineDuration, BoundaryBatchId);
        LineBatcher->DrawLine(Midpoint, CentroidB, LineColor, SDPG_World, LineThickness, LineDuration, BoundaryBatchId);
    }
#endif
}

void FTectonicSimulationController::BuildMeshFromSnapshot(int32 LODLevel, int32 TopologyVersion, const FMeshBuildSnapshot& Snapshot,
    RealtimeMesh::FRealtimeMeshStreamSet& OutStreamSet, int32& OutVertexCount, int32& OutTriangleCount,
    TArray<float>& OutPositionX, TArray<float>& OutPositionY, TArray<float>& OutPositionZ,
    TArray<float>& OutNormalX, TArray<float>& OutNormalY, TArray<float>& OutNormalZ,
    TArray<float>& OutTangentX, TArray<float>& OutTangentY, TArray<float>& OutTangentZ,
    TArray<FColor>& OutColors, TArray<FVector2f>& OutUVs, TArray<uint32>& OutIndices)
{
    using namespace RealtimeMesh;

    OutVertexCount = 0;
    OutTriangleCount = 0;

    OutPositionX.Reset();
    OutPositionY.Reset();
    OutPositionZ.Reset();
    OutNormalX.Reset();
    OutNormalY.Reset();
    OutNormalZ.Reset();
    OutTangentX.Reset();
    OutTangentY.Reset();
    OutTangentZ.Reset();
    OutColors.Reset();
    OutUVs.Reset();
    OutIndices.Reset();

    const TArray<FVector3d>& RenderVertices = Snapshot.RenderVertices;
    const TArray<int32>& RenderTriangles = Snapshot.RenderTriangles;
    const TArray<int32>& VertexPlateAssignments = Snapshot.VertexPlateAssignments;
    const TArray<FVector3d>& VertexVelocities = Snapshot.VertexVelocities;
    const TArray<double>& VertexStressValues = Snapshot.VertexStressValues;

    const int32 SourceVertexCount = RenderVertices.Num();
    if (SourceVertexCount == 0 || RenderTriangles.Num() == 0 || VertexPlateAssignments.Num() != SourceVertexCount)
    {
        return;
    }

    TRealtimeMeshBuilderLocal<uint32, FPackedNormal, FVector2DHalf, 1> Builder(OutStreamSet);
    Builder.EnableTangents();
    Builder.EnableTexCoords();
    Builder.EnableColors();

    const float RadiusUE = MetersToUE(Snapshot.PlanetRadius);
    const FStaticLODData& StaticData = GetOrBuildStaticLODData(LODLevel, TopologyVersion, RenderVertices);
    const TArray<FVector2f>& CachedUVs = StaticData.UVs;
    const TArray<FVector3f>& CachedTangents = StaticData.TangentX;
    const TArray<FVector3f>& CachedNormals = StaticData.UnitNormals;

    auto GetPlateColor = [](int32 PlateID) -> FColor
    {
        constexpr float GoldenRatio = 0.618033988749895f;
        const float Hue = FMath::Fmod(PlateID * GoldenRatio, 1.0f);
        FLinearColor HSV(Hue * 360.0f, 1.0f, 1.0f);
        return HSV.HSVToLinearRGB().ToFColor(false);
    };

    auto GetVelocityColor = [](const FVector3d& Velocity) -> FColor
    {
        const double Magnitude = Velocity.Length();
        const double Normalized = FMath::Clamp((Magnitude - 0.01) / (0.1 - 0.01), 0.0, 1.0);
        const float Hue = FMath::Lerp(240.0f, 0.0f, static_cast<float>(Normalized));
        FLinearColor HSV(Hue, 0.8f, 0.9f);
        return HSV.HSVToLinearRGB().ToFColor(false);
    };

    auto GetStressColor = [](double StressMPa) -> FColor
    {
        const double Normalized = FMath::Clamp(StressMPa / 100.0, 0.0, 1.0);
        const float Hue = FMath::Lerp(120.0f, 0.0f, static_cast<float>(Normalized));
        FLinearColor HSV(Hue, 0.8f, 0.9f);
        return HSV.HSVToLinearRGB().ToFColor(false);
    };

    const TArray<double>* EffectiveElevations = nullptr;
    if (Snapshot.bUseAmplifiedElevation && Snapshot.VertexAmplifiedElevation.Num() == SourceVertexCount)
    {
        EffectiveElevations = &Snapshot.VertexAmplifiedElevation;
    }
    else if (Snapshot.VertexElevationValues.Num() == SourceVertexCount)
    {
        EffectiveElevations = &Snapshot.VertexElevationValues;
    }

    double MinElevationMeters = TNumericLimits<double>::Max();
    double MaxElevationMeters = TNumericLimits<double>::Lowest();
    if (EffectiveElevations)
    {
        for (double Elevation : *EffectiveElevations)
        {
            if (!FMath::IsFinite(Elevation))
            {
                continue;
            }

            MinElevationMeters = FMath::Min(MinElevationMeters, Elevation);
            MaxElevationMeters = FMath::Max(MaxElevationMeters, Elevation);
        }
    }

    if (MinElevationMeters > MaxElevationMeters)
    {
        MinElevationMeters = PaperElevationConstants::AbyssalPlainDepth_m;
        MaxElevationMeters = 2000.0;
    }

    double ElevationRangeMeters = MaxElevationMeters - MinElevationMeters;
    if (ElevationRangeMeters < KINDA_SMALL_NUMBER)
    {
        ElevationRangeMeters = 1.0;
    }

    static const FLinearColor HeightGradientStops[] = {
        FLinearColor(0.015f, 0.118f, 0.341f),
        FLinearColor(0.000f, 0.392f, 0.729f),
        FLinearColor(0.137f, 0.702f, 0.467f),
        FLinearColor(0.933f, 0.894f, 0.298f),
        FLinearColor(0.957f, 0.643f, 0.376f)
    };

    auto GetElevationColor = [&](double ElevationMeters) -> FColor
    {
        const double Normalized = FMath::Clamp((ElevationMeters - MinElevationMeters) / ElevationRangeMeters, 0.0, 1.0);
        const int32 StopCount = UE_ARRAY_COUNT(HeightGradientStops);
        const double Scaled = Normalized * (StopCount - 1);
        const int32 IndexLow = FMath::Clamp(static_cast<int32>(Scaled), 0, StopCount - 1);
        const int32 IndexHigh = FMath::Clamp(IndexLow + 1, 0, StopCount - 1);
        const float Alpha = static_cast<float>(Scaled - IndexLow);
        return FLinearColor::LerpUsingHSV(HeightGradientStops[IndexLow], HeightGradientStops[IndexHigh], Alpha).ToFColor(false);
    };

    constexpr double MaxDisplacementMeters = 10000.0;

    struct FPreviewMeshVertex
    {
        FVector3f Position = FVector3f::ZeroVector;
        FVector3f Normal = FVector3f::ZAxisVector;
        FVector3f TangentX = FVector3f::XAxisVector;
        FColor Color = FColor::Black;
        FVector2f UV = FVector2f::ZeroVector;
        int32 PrimaryIndex = INDEX_NONE;
        int32 SeamWrappedIndex = INDEX_NONE;
    };

    TArray<FPreviewMeshVertex> PreviewVertices;
    PreviewVertices.SetNum(SourceVertexCount);

    const bool bShowVelocity = Snapshot.bShowVelocityField;
    const bool bElevColor = Snapshot.Parameters.bEnableHeightmapVisualization || Snapshot.VertexElevationValues.Num() > 0;
    const bool bHighlightSeaLevel = Snapshot.bHighlightSeaLevel;
    const double SeaLevelMeters = Snapshot.Parameters.SeaLevel;

    const TArray<float>* SoANormalX = nullptr;
    const TArray<float>* SoANormalY = nullptr;
    const TArray<float>* SoANormalZ = nullptr;
    const TArray<float>* SoATangentX = nullptr;
    const TArray<float>* SoATangentY = nullptr;
    const TArray<float>* SoATangentZ = nullptr;

    if (const UTectonicSimulationService* Service = GetService())
    {
        const TArray<float>* DummyPosX = nullptr;
        const TArray<float>* DummyPosY = nullptr;
        const TArray<float>* DummyPosZ = nullptr;
        Service->GetRenderVertexFloatSoA(DummyPosX, DummyPosY, DummyPosZ,
            SoANormalX, SoANormalY, SoANormalZ,
            SoATangentX, SoATangentY, SoATangentZ);
    }

    const bool bUseSoANormals = SoANormalX && SoANormalY && SoANormalZ &&
        SoANormalX->Num() == SourceVertexCount &&
        SoANormalY->Num() == SourceVertexCount &&
        SoANormalZ->Num() == SourceVertexCount;
    const bool bUseSoATangents = SoATangentX && SoATangentY && SoATangentZ &&
        SoATangentX->Num() == SourceVertexCount &&
        SoATangentY->Num() == SourceVertexCount &&
        SoATangentZ->Num() == SourceVertexCount;

    ParallelFor(SourceVertexCount, [&](int32 Index)
    {
        FPreviewMeshVertex& VertexOut = PreviewVertices[Index];
        VertexOut.PrimaryIndex = INDEX_NONE;
        VertexOut.SeamWrappedIndex = INDEX_NONE;

        FVector3f UnitNormal;
        if (bUseSoANormals && SoANormalX->IsValidIndex(Index) && SoANormalY->IsValidIndex(Index) && SoANormalZ->IsValidIndex(Index))
        {
            UnitNormal = FVector3f((*SoANormalX)[Index], (*SoANormalY)[Index], (*SoANormalZ)[Index]);
        }
        else if (CachedNormals.IsValidIndex(Index))
        {
            UnitNormal = CachedNormals[Index];
        }
        else
        {
            UnitNormal = FVector3f(RenderVertices[Index].GetSafeNormal());
        }
        if (UnitNormal.IsNearlyZero())
        {
            UnitNormal = FVector3f::ZAxisVector;
        }

        FVector3f Position = UnitNormal * RadiusUE;
        FVector3f Normal = UnitNormal;

        const FVector2f UV = CachedUVs.IsValidIndex(Index) ? CachedUVs[Index] : FVector2f::ZeroVector;
        FVector3f Tangent;
        if (bUseSoATangents && SoATangentX->IsValidIndex(Index) && SoATangentY->IsValidIndex(Index) && SoATangentZ->IsValidIndex(Index))
        {
            Tangent = FVector3f((*SoATangentX)[Index], (*SoATangentY)[Index], (*SoATangentZ)[Index]);
        }
        else if (CachedTangents.IsValidIndex(Index))
        {
            Tangent = CachedTangents[Index];
        }
        else
        {
            Tangent = FVector3f::XAxisVector;
        }

        double ElevationMeters = 0.0;
        if (Snapshot.bUseAmplifiedElevation && Snapshot.VertexAmplifiedElevation.IsValidIndex(Index))
        {
            ElevationMeters = Snapshot.VertexAmplifiedElevation[Index];
        }
        else if (Snapshot.VertexElevationValues.IsValidIndex(Index))
        {
            ElevationMeters = Snapshot.VertexElevationValues[Index];
        }

        if (Snapshot.ElevationMode == EElevationMode::Displaced)
        {
            const double ClampedElevation = FMath::Clamp(ElevationMeters, -MaxDisplacementMeters, MaxDisplacementMeters);
            const float DisplacementUE = MetersToUE(ClampedElevation);
            Position += Normal * DisplacementUE;
            Normal = Position.GetSafeNormal();
        }

        FColor VertexColor = FColor::Black;
        const int32 PlateID = VertexPlateAssignments[Index];

        if (bShowVelocity && VertexVelocities.IsValidIndex(Index))
        {
            VertexColor = GetVelocityColor(VertexVelocities[Index]);
        }
        else if (bElevColor)
        {
            VertexColor = GetElevationColor(ElevationMeters);
            if (bHighlightSeaLevel && FMath::Abs(ElevationMeters - SeaLevelMeters) <= 50.0)
            {
                VertexColor = FColor::White;
            }
        }
        else if (VertexStressValues.IsValidIndex(Index))
        {
            VertexColor = GetStressColor(VertexStressValues[Index]);
        }
        else
        {
            VertexColor = GetPlateColor(PlateID);
        }

        VertexOut.Position = Position;
        VertexOut.Normal = Normal;
        VertexOut.TangentX = Tangent;
        VertexOut.Color = VertexColor;
        VertexOut.UV = UV;
    });

    const int32 EstimatedVertexCapacity = SourceVertexCount * 2;
    OutPositionX.Reserve(EstimatedVertexCapacity);
    OutPositionY.Reserve(EstimatedVertexCapacity);
    OutPositionZ.Reserve(EstimatedVertexCapacity);
    OutNormalX.Reserve(EstimatedVertexCapacity);
    OutNormalY.Reserve(EstimatedVertexCapacity);
    OutNormalZ.Reserve(EstimatedVertexCapacity);
    OutTangentX.Reserve(EstimatedVertexCapacity);
    OutTangentY.Reserve(EstimatedVertexCapacity);
    OutTangentZ.Reserve(EstimatedVertexCapacity);
    OutColors.Reserve(EstimatedVertexCapacity);
    OutUVs.Reserve(EstimatedVertexCapacity);
    OutIndices.Reserve(RenderTriangles.Num());

    auto EmitVertex = [&](FPreviewMeshVertex& VertexData) -> int32
    {
        const int32 VertexId = Builder.AddVertex(VertexData.Position)
            .SetNormalAndTangent(VertexData.Normal, VertexData.TangentX)
            .SetColor(VertexData.Color)
            .SetTexCoord(VertexData.UV);

        OutPositionX.Add(VertexData.Position.X);
        OutPositionY.Add(VertexData.Position.Y);
        OutPositionZ.Add(VertexData.Position.Z);

        OutNormalX.Add(VertexData.Normal.X);
        OutNormalY.Add(VertexData.Normal.Y);
        OutNormalZ.Add(VertexData.Normal.Z);

        OutTangentX.Add(VertexData.TangentX.X);
        OutTangentY.Add(VertexData.TangentX.Y);
        OutTangentZ.Add(VertexData.TangentX.Z);

        OutColors.Add(VertexData.Color);
        OutUVs.Add(VertexData.UV);

        OutVertexCount++;
        return VertexId;
    };

    for (int32 Index = 0; Index < SourceVertexCount; ++Index)
    {
        FPreviewMeshVertex& VertexData = PreviewVertices[Index];
        VertexData.PrimaryIndex = EmitVertex(VertexData);
    }

    constexpr float UVSeamWrapThreshold = 0.5f;
    constexpr float UVSeamSplitReference = 0.5f;

    auto ResolveSeamVertexIndex = [&](FPreviewMeshVertex& VertexData) -> int32
    {
        if (VertexData.SeamWrappedIndex != INDEX_NONE)
        {
            return VertexData.SeamWrappedIndex;
        }

        FPreviewMeshVertex WrappedVertex = VertexData;
        WrappedVertex.UV.X += 1.0f;
        VertexData.SeamWrappedIndex = EmitVertex(WrappedVertex);
        return VertexData.SeamWrappedIndex;
    };

    for (int32 TriangleIdx = 0; TriangleIdx < RenderTriangles.Num(); TriangleIdx += 3)
    {
        const int32 SourceIndex0 = RenderTriangles[TriangleIdx];
        const int32 SourceIndex1 = RenderTriangles[TriangleIdx + 1];
        const int32 SourceIndex2 = RenderTriangles[TriangleIdx + 2];

        FPreviewMeshVertex& Vertex0 = PreviewVertices[SourceIndex0];
        FPreviewMeshVertex& Vertex1 = PreviewVertices[SourceIndex1];
        FPreviewMeshVertex& Vertex2 = PreviewVertices[SourceIndex2];

        const float MaxU = FMath::Max3(Vertex0.UV.X, Vertex1.UV.X, Vertex2.UV.X);
        const float MinU = FMath::Min3(Vertex0.UV.X, Vertex1.UV.X, Vertex2.UV.X);
        const bool bCrossesSeam = (MaxU - MinU) > UVSeamWrapThreshold;

        auto GetTriangleVertexIndex = [&](FPreviewMeshVertex& VertexData) -> int32
        {
            if (!bCrossesSeam || VertexData.UV.X >= UVSeamSplitReference)
            {
                return VertexData.PrimaryIndex;
            }

            return ResolveSeamVertexIndex(VertexData);
        };

        const int32 V0 = GetTriangleVertexIndex(Vertex0);
        const int32 V1 = GetTriangleVertexIndex(Vertex1);
        const int32 V2 = GetTriangleVertexIndex(Vertex2);

        Builder.AddTriangle(V0, V2, V1);
        OutIndices.Add(static_cast<uint32>(V0));
        OutIndices.Add(static_cast<uint32>(V2));
        OutIndices.Add(static_cast<uint32>(V1));
        OutTriangleCount++;
    }
}

// Milestone 4 Phase 4.1: Global LOD selection based on camera distance
void FTectonicSimulationController::UpdateLOD()
{
    // Check if automatic LOD is enabled
    UTectonicSimulationService* Service = GetService();
    if (Service && !Service->GetParameters().bEnableAutomaticLOD)
    {
        return; // Manual LOD mode - respect user-set render subdivision level
    }

    // Get editor viewport camera
    if (!GEditor)
    {
        return;
    }

    FViewport* Viewport = GEditor->GetActiveViewport();
    if (!Viewport || !Viewport->GetClient())
    {
        return;
    }

    FEditorViewportClient* ViewportClient = static_cast<FEditorViewportClient*>(Viewport->GetClient());
    if (!ViewportClient)
    {
        return;
    }

    // Get camera location
    const FVector CameraLocation = ViewportClient->GetViewLocation();

    // Planet is centered at origin with radius 6370 km (1 UE unit = 1 km)
    constexpr double PlanetRadius = 6370.0;
    const double CameraDistance = CameraLocation.Length();
    const double dOverR = CameraDistance / PlanetRadius;

    // LOD selection (from Milestone4_LOD_Design.md):
    // - Close: d/R < 3 → L7 (≈327,680 tris)
    // - Medium: 3 ≤ d/R < 10 → L5 (≈20,480 tris)
    // - Far: d/R ≥ 10 → L4 (≈5,120 tris)

    int32 NewTargetLOD = 4; // Default far

    if (dOverR < 3.0)
    {
        NewTargetLOD = 7; // Close
    }
    else if (dOverR < 10.0)
    {
        NewTargetLOD = 5; // Medium
    }

    // Hysteresis: 10% around boundaries to reduce thrashing
    constexpr double Hysteresis = 0.1;

    if (NewTargetLOD != TargetLODLevel)
    {
        // Check if we're within hysteresis zone
        bool bApplyChange = true;

        if (TargetLODLevel == 7 && NewTargetLOD == 5)
        {
            // Transitioning from L7 to L5: require d/R > 3.0 * (1 + hysteresis)
            bApplyChange = (dOverR > 3.0 * (1.0 + Hysteresis));
        }
        else if (TargetLODLevel == 5 && NewTargetLOD == 7)
        {
            // Transitioning from L5 to L7: require d/R < 3.0 * (1 - hysteresis)
            bApplyChange = (dOverR < 3.0 * (1.0 - Hysteresis));
        }
        else if (TargetLODLevel == 5 && NewTargetLOD == 4)
        {
            // Transitioning from L5 to L4: require d/R > 10.0 * (1 + hysteresis)
            bApplyChange = (dOverR > 10.0 * (1.0 + Hysteresis));
        }
        else if (TargetLODLevel == 4 && NewTargetLOD == 5)
        {
            // Transitioning from L4 to L5: require d/R < 10.0 * (1 - hysteresis)
            bApplyChange = (dOverR < 10.0 * (1.0 - Hysteresis));
        }

        if (bApplyChange)
        {
            TargetLODLevel = NewTargetLOD;
            UE_LOG(LogPlanetaryCreation, Log, TEXT("[LOD] Target LOD changed: L%d (d/R=%.2f, distance=%.0f km)"),
                TargetLODLevel, dOverR, CameraDistance);

            // Trigger mesh rebuild at new LOD if different from current
            if (TargetLODLevel != CurrentLODLevel && Service)
            {
                // Milestone 4 Phase 4.1: Use non-destructive LOD update (preserves simulation state)
                Service->SetRenderSubdivisionLevel(TargetLODLevel);

                // Trigger rebuild
                CurrentLODLevel = TargetLODLevel;
                BuildAndUpdateMesh();
            }
        }
    }

    LastCameraDistance = CameraDistance;
}

// Milestone 4 Phase 4.2: LOD Caching Implementation

bool FTectonicSimulationController::IsLODCached(int32 LODLevel, int32 TopologyVersion, int32 SurfaceDataVersion) const
{
    const TUniquePtr<FCachedLODMesh>* CachedMesh = LODCache.Find(LODLevel);
    if (!CachedMesh || !CachedMesh->IsValid())
    {
        return false;
    }

    // Check if cached version matches current versions
    return (*CachedMesh)->TopologyVersion == TopologyVersion &&
           (*CachedMesh)->SurfaceDataVersion == SurfaceDataVersion;
}

const FCachedLODMesh* FTectonicSimulationController::GetCachedLOD(int32 LODLevel, int32 TopologyVersion, int32 SurfaceDataVersion) const
{
    if (!IsLODCached(LODLevel, TopologyVersion, SurfaceDataVersion))
    {
        return nullptr;
    }

    const TUniquePtr<FCachedLODMesh>* CachedMesh = LODCache.Find(LODLevel);
    return CachedMesh ? CachedMesh->Get() : nullptr;
}

void FTectonicSimulationController::CacheLODMesh(int32 LODLevel, int32 TopologyVersion, int32 SurfaceDataVersion,
    const FMeshBuildSnapshot& Snapshot, int32 VertexCount, int32 TriangleCount,
    TArray<float>&& PositionX, TArray<float>&& PositionY, TArray<float>&& PositionZ,
    TArray<float>&& NormalX, TArray<float>&& NormalY, TArray<float>&& NormalZ,
    TArray<float>&& TangentX, TArray<float>&& TangentY, TArray<float>&& TangentZ,
    TArray<FColor>&& VertexColors, TArray<FVector2f>&& UVs, TArray<uint32>&& Indices)
{
    if (bShutdownRequested.load())
    {
        UE_LOG(LogPlanetaryCreation, Verbose, TEXT("[LOD Cache] Skipping cache update for L%d (controller shutting down)"), LODLevel);
        return;
    }

    TUniquePtr<FCachedLODMesh> NewCache = MakeUnique<FCachedLODMesh>();
    NewCache->Snapshot = Snapshot;
    NewCache->VertexCount = VertexCount;
    NewCache->TriangleCount = TriangleCount;
    NewCache->TopologyVersion = TopologyVersion;
    NewCache->SurfaceDataVersion = SurfaceDataVersion;
    NewCache->CacheTimestamp = FPlatformTime::Seconds();

    NewCache->PositionX = MoveTemp(PositionX);
    NewCache->PositionY = MoveTemp(PositionY);
    NewCache->PositionZ = MoveTemp(PositionZ);

    NewCache->NormalX = MoveTemp(NormalX);
    NewCache->NormalY = MoveTemp(NormalY);
    NewCache->NormalZ = MoveTemp(NormalZ);

    NewCache->TangentX = MoveTemp(TangentX);
    NewCache->TangentY = MoveTemp(TangentY);
    NewCache->TangentZ = MoveTemp(TangentZ);

    NewCache->VertexColors = MoveTemp(VertexColors);
    NewCache->UVs = MoveTemp(UVs);
    NewCache->Indices = MoveTemp(Indices);

    const bool bValidStreamSizes =
        NewCache->PositionX.Num() == VertexCount &&
        NewCache->PositionY.Num() == VertexCount &&
        NewCache->PositionZ.Num() == VertexCount &&
        NewCache->NormalX.Num() == VertexCount &&
        NewCache->NormalY.Num() == VertexCount &&
        NewCache->NormalZ.Num() == VertexCount &&
        NewCache->TangentX.Num() == VertexCount &&
        NewCache->TangentY.Num() == VertexCount &&
        NewCache->TangentZ.Num() == VertexCount &&
        NewCache->VertexColors.Num() == VertexCount &&
        NewCache->UVs.Num() == VertexCount &&
        NewCache->Indices.Num() == TriangleCount * 3;

    if (!bValidStreamSizes)
    {
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("[LOD Cache] Stream size mismatch when caching L%d (verts=%d, positions=%d, indices=%d)"),
            LODLevel, VertexCount, NewCache->PositionX.Num(), NewCache->Indices.Num());
    }

    // Use Emplace() to handle potential race conditions (overwrites if key exists)
    LODCache.Emplace(LODLevel, MoveTemp(NewCache));

    UE_LOG(LogPlanetaryCreation, Log, TEXT("[LOD Cache] Cached L%d: %d verts, %d tris (Topo:%d, Surface:%d)"),
        LODLevel, VertexCount, TriangleCount, TopologyVersion, SurfaceDataVersion);
}

void FTectonicSimulationController::BuildMeshFromCache(const FCachedLODMesh& CachedMesh,
    RealtimeMesh::FRealtimeMeshStreamSet& OutStreamSet, int32& OutVertexCount, int32& OutTriangleCount)
{
    using namespace RealtimeMesh;

    OutVertexCount = 0;
    OutTriangleCount = 0;

    const int32 VertexCount = CachedMesh.VertexCount;
    if (VertexCount == 0 || CachedMesh.Indices.Num() == 0)
    {
        return;
    }

    const bool bValidStreams =
        CachedMesh.PositionX.Num() == VertexCount &&
        CachedMesh.PositionY.Num() == VertexCount &&
        CachedMesh.PositionZ.Num() == VertexCount &&
        CachedMesh.NormalX.Num() == VertexCount &&
        CachedMesh.NormalY.Num() == VertexCount &&
        CachedMesh.NormalZ.Num() == VertexCount &&
        CachedMesh.TangentX.Num() == VertexCount &&
        CachedMesh.TangentY.Num() == VertexCount &&
        CachedMesh.TangentZ.Num() == VertexCount &&
        CachedMesh.VertexColors.Num() == VertexCount &&
        CachedMesh.UVs.Num() == VertexCount;

    if (!bValidStreams)
    {
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("[LOD Cache] Cached stream sizes invalid (L%d). Rebuilding from snapshot instead."), CachedMesh.Snapshot.Parameters.RenderSubdivisionLevel);
        TArray<float> PositionX, PositionY, PositionZ;
        TArray<float> NormalX, NormalY, NormalZ;
        TArray<float> TangentX, TangentY, TangentZ;
        TArray<FColor> Colors;
        TArray<FVector2f> UVs;
        TArray<uint32> Indices;
        const int32 LODLevel = CachedMesh.Snapshot.Parameters.RenderSubdivisionLevel;
        BuildMeshFromSnapshot(LODLevel, CachedMesh.TopologyVersion, CachedMesh.Snapshot,
            OutStreamSet, OutVertexCount, OutTriangleCount,
            PositionX, PositionY, PositionZ,
            NormalX, NormalY, NormalZ,
            TangentX, TangentY, TangentZ,
            Colors, UVs, Indices);

        CacheLODMesh(LODLevel, CachedMesh.TopologyVersion, CachedMesh.SurfaceDataVersion, CachedMesh.Snapshot,
            OutVertexCount, OutTriangleCount,
            MoveTemp(PositionX), MoveTemp(PositionY), MoveTemp(PositionZ),
            MoveTemp(NormalX), MoveTemp(NormalY), MoveTemp(NormalZ),
            MoveTemp(TangentX), MoveTemp(TangentY), MoveTemp(TangentZ),
            MoveTemp(Colors), MoveTemp(UVs), MoveTemp(Indices));
        return;
    }

    TRealtimeMeshBuilderLocal<uint32, FPackedNormal, FVector2DHalf, 1> Builder(OutStreamSet);
    Builder.EnableTangents();
    Builder.EnableTexCoords();
    Builder.EnableColors();

    for (int32 Index = 0; Index < VertexCount; ++Index)
    {
        const FVector3f Position(CachedMesh.PositionX[Index], CachedMesh.PositionY[Index], CachedMesh.PositionZ[Index]);
        FVector3f Normal(CachedMesh.NormalX[Index], CachedMesh.NormalY[Index], CachedMesh.NormalZ[Index]);
        if (!Normal.IsNearlyZero())
        {
            Normal = Normal.GetSafeNormal();
        }
        else
        {
            Normal = FVector3f::ZAxisVector;
        }

        FVector3f Tangent(CachedMesh.TangentX[Index], CachedMesh.TangentY[Index], CachedMesh.TangentZ[Index]);
        if (Tangent.IsNearlyZero())
        {
            Tangent = FVector3f::XAxisVector;
        }

        const FColor VertexColor = CachedMesh.VertexColors.IsValidIndex(Index) ? CachedMesh.VertexColors[Index] : FColor::Black;
        const FVector2f UV = CachedMesh.UVs.IsValidIndex(Index) ? CachedMesh.UVs[Index] : FVector2f::ZeroVector;

        Builder.AddVertex(Position)
            .SetNormalAndTangent(Normal, Tangent)
            .SetColor(VertexColor)
            .SetTexCoord(UV);

        OutVertexCount++;
    }

    for (int32 Index = 0; Index + 2 < CachedMesh.Indices.Num(); Index += 3)
    {
        const uint32 I0 = CachedMesh.Indices[Index];
        const uint32 I1 = CachedMesh.Indices[Index + 1];
        const uint32 I2 = CachedMesh.Indices[Index + 2];
        Builder.AddTriangle(static_cast<int32>(I0), static_cast<int32>(I1), static_cast<int32>(I2));
        OutTriangleCount++;
    }
}

void FTectonicSimulationController::InvalidateLODCache()
{
    const int32 NumCached = LODCache.Num();
    LODCache.Empty();
    StaticLODDataCache.Empty();

    UE_LOG(LogPlanetaryCreation, Warning, TEXT("[LOD Cache] Invalidated %d cached LOD meshes (topology changed)"), NumCached);
}

void FTectonicSimulationController::PreWarmNeighboringLODs()
{
    UTectonicSimulationService* Service = GetService();
    if (!Service)
    {
        return;
    }

    const int32 CurrentTopologyVersion = Service->GetTopologyVersion();
    const int32 CurrentSurfaceVersion = Service->GetSurfaceDataVersion();

    // Determine which LODs to pre-warm based on current target
    TArray<int32> LODsToPreWarm;

    if (TargetLODLevel == 4)
    {
        // At L4 (far), pre-warm L5 (medium)
        LODsToPreWarm.Add(5);
    }
    else if (TargetLODLevel == 5)
    {
        // At L5 (medium), pre-warm both L4 (far) and L7 (close)
        LODsToPreWarm.Add(4);
        LODsToPreWarm.Add(7);
    }
    else if (TargetLODLevel == 7)
    {
        // At L7 (close), pre-warm L5 (medium)
        LODsToPreWarm.Add(5);
    }

    // Build any uncached neighboring LODs asynchronously
    for (int32 LODLevel : LODsToPreWarm)
    {
        if (IsLODCached(LODLevel, CurrentTopologyVersion, CurrentSurfaceVersion))
        {
            UE_LOG(LogPlanetaryCreation, Verbose, TEXT("[LOD Cache] L%d already cached, skipping pre-warm"), LODLevel);
            continue;
        }

        if (bAsyncMeshBuildInProgress.load())
        {
            UE_LOG(LogPlanetaryCreation, Verbose, TEXT("[LOD Cache] Async build in progress, deferring pre-warm of L%d"), LODLevel);
            continue; // Don't queue multiple builds
        }

        UE_LOG(LogPlanetaryCreation, Log, TEXT("[LOD Cache] Pre-warming L%d..."), LODLevel);

        // Trigger async build for this LOD
        const int32 PreviousRenderLevel = Service->GetParameters().RenderSubdivisionLevel;
        Service->SetRenderSubdivisionLevel(LODLevel);

        // Build mesh snapshot
        FMeshBuildSnapshot Snapshot = CreateMeshBuildSnapshot();

        // Restore previous render level
        Service->SetRenderSubdivisionLevel(PreviousRenderLevel);

        // Kick off async build
        bAsyncMeshBuildInProgress.store(true);
        ActiveAsyncTasks.fetch_add(1, std::memory_order_relaxed);
        AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, Snapshot, LODLevel, CurrentTopologyVersion, CurrentSurfaceVersion]() mutable
        {
            if (bShutdownRequested.load(std::memory_order_relaxed))
            {
                ActiveAsyncTasks.fetch_sub(1, std::memory_order_relaxed);
                bAsyncMeshBuildInProgress.store(false);
                return;
            }

            RealtimeMesh::FRealtimeMeshStreamSet StreamSet;
            int32 VertexCount = 0;
            int32 TriangleCount = 0;

            TArray<float> PositionX, PositionY, PositionZ;
            TArray<float> NormalX, NormalY, NormalZ;
            TArray<float> TangentX, TangentY, TangentZ;
            TArray<FColor> Colors;
            TArray<FVector2f> UVs;
            TArray<uint32> Indices;

            BuildMeshFromSnapshot(LODLevel, CurrentTopologyVersion, Snapshot, StreamSet, VertexCount, TriangleCount,
                PositionX, PositionY, PositionZ,
                NormalX, NormalY, NormalZ,
                TangentX, TangentY, TangentZ,
                Colors, UVs, Indices);

            // Return to game thread to cache result
            AsyncTask(ENamedThreads::GameThread, [this, Snapshot, VertexCount, TriangleCount, LODLevel, CurrentTopologyVersion, CurrentSurfaceVersion,
                PositionX = MoveTemp(PositionX), PositionY = MoveTemp(PositionY), PositionZ = MoveTemp(PositionZ),
                NormalX = MoveTemp(NormalX), NormalY = MoveTemp(NormalY), NormalZ = MoveTemp(NormalZ),
                TangentX = MoveTemp(TangentX), TangentY = MoveTemp(TangentY), TangentZ = MoveTemp(TangentZ),
                Colors = MoveTemp(Colors), UVs = MoveTemp(UVs), Indices = MoveTemp(Indices)]() mutable
            {
                if (!bShutdownRequested.load(std::memory_order_relaxed))
                {
                    CacheLODMesh(LODLevel, CurrentTopologyVersion, CurrentSurfaceVersion, Snapshot, VertexCount, TriangleCount,
                        MoveTemp(PositionX), MoveTemp(PositionY), MoveTemp(PositionZ),
                        MoveTemp(NormalX), MoveTemp(NormalY), MoveTemp(NormalZ),
                        MoveTemp(TangentX), MoveTemp(TangentY), MoveTemp(TangentZ),
                        MoveTemp(Colors), MoveTemp(UVs), MoveTemp(Indices));
                }

                bAsyncMeshBuildInProgress.store(false);
                ActiveAsyncTasks.fetch_sub(1, std::memory_order_relaxed);
            });
        });

        // Only pre-warm one LOD at a time to avoid overwhelming the system
        break;
    }
}

void FTectonicSimulationController::GetCacheStats(int32& OutCachedLODs, int32& OutTotalCacheSize) const
{
    OutCachedLODs = LODCache.Num();
    OutTotalCacheSize = 0;

    for (const auto& CachePair : LODCache)
    {
        if (CachePair.Value.IsValid())
        {
            const FCachedLODMesh& CachedMesh = *CachePair.Value;
            size_t AccumulatedBytes = 0;
            AccumulatedBytes += CachedMesh.PositionX.GetAllocatedSize();
            AccumulatedBytes += CachedMesh.PositionY.GetAllocatedSize();
            AccumulatedBytes += CachedMesh.PositionZ.GetAllocatedSize();
            AccumulatedBytes += CachedMesh.NormalX.GetAllocatedSize();
            AccumulatedBytes += CachedMesh.NormalY.GetAllocatedSize();
            AccumulatedBytes += CachedMesh.NormalZ.GetAllocatedSize();
            AccumulatedBytes += CachedMesh.TangentX.GetAllocatedSize();
            AccumulatedBytes += CachedMesh.TangentY.GetAllocatedSize();
            AccumulatedBytes += CachedMesh.TangentZ.GetAllocatedSize();
            AccumulatedBytes += CachedMesh.VertexColors.GetAllocatedSize();
            AccumulatedBytes += CachedMesh.UVs.GetAllocatedSize();
            AccumulatedBytes += CachedMesh.Indices.GetAllocatedSize();
            OutTotalCacheSize += static_cast<int32>(AccumulatedBytes);
        }
    }
}

// ============================================================================
// Milestone 5 Task 1.2: Camera Control Methods
// ============================================================================

void FTectonicSimulationController::RotateCamera(float DeltaYaw, float DeltaPitch)
{
    CameraController.Rotate(DeltaYaw, DeltaPitch);
}

void FTectonicSimulationController::ZoomCamera(float DeltaDistance)
{
    CameraController.Zoom(DeltaDistance);
}

void FTectonicSimulationController::ResetCamera()
{
    CameraController.ResetToDefault();
}

void FTectonicSimulationController::TickCamera(float DeltaTime)
{
    CameraController.Tick(DeltaTime);
}

FVector2D FTectonicSimulationController::GetCameraAngles() const
{
    return CameraController.GetOrbitAngles();
}

float FTectonicSimulationController::GetCameraDistance() const
{
    return CameraController.GetCurrentDistance();
}
