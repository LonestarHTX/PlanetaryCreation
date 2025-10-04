#include "TectonicSimulationController.h"

#include "Async/Async.h"
#include "Async/Future.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "RealtimeMeshComponent/Public/RealtimeMeshActor.h"
#include "RealtimeMeshComponent/Public/RealtimeMeshComponent.h"
#include "RealtimeMeshComponent/Public/RealtimeMeshSimple.h"
#include "RealtimeMeshComponent/Public/Interface/Core/RealtimeMeshBuilder.h"
#include "RealtimeMeshComponent/Public/Interface/Core/RealtimeMeshSectionConfig.h"
#include "RealtimeMeshComponent/Public/Interface/Core/RealtimeMeshStreamRange.h"
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

FTectonicSimulationController::FTectonicSimulationController() = default;
FTectonicSimulationController::~FTectonicSimulationController() = default;

void FTectonicSimulationController::Initialize()
{
    CachedService = GetService();
}

void FTectonicSimulationController::Shutdown()
{
    CachedService.Reset();
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
        Snapshot.ElevationScale = Service->GetParameters().ElevationScale;
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
        UE_LOG(LogTemp, Log, TEXT("💾 [LOD Cache] Using cached L%d: %d verts, %d tris (cache hit)"),
            RenderLevel, CachedMesh->VertexCount, CachedMesh->TriangleCount);

        // Rebuild from cached snapshot (fast, already computed)
        RealtimeMesh::FRealtimeMeshStreamSet StreamSet;
        int32 VertexCount = 0;
        int32 TriangleCount = 0;
        BuildMeshFromSnapshot(CachedMesh->Snapshot, StreamSet, VertexCount, TriangleCount);
        UpdatePreviewMesh(MoveTemp(StreamSet), VertexCount, TriangleCount);

        // Pre-warm neighboring LODs opportunistically
        PreWarmNeighboringLODs();

        return;
    }

    UE_LOG(LogTemp, Log, TEXT("🔨 [LOD Cache] L%d not cached, building... (Topo:%d, Surface:%d)"),
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

        BuildMeshFromSnapshot(Snapshot, StreamSet, VertexCount, TriangleCount);

        const double EndTime = FPlatformTime::Seconds();
        LastMeshBuildTimeMs = (EndTime - StartTime) * 1000.0;

        UE_LOG(LogTemp, Log, TEXT("⚡ [SYNC] Mesh build: %d verts, %d tris, %.2fms (ThreadID: %u, level %d)"),
            VertexCount, TriangleCount, LastMeshBuildTimeMs, ThreadID, RenderLevel);

        // Milestone 4 Phase 4.2: Cache the snapshot before using it
        CacheLODMesh(RenderLevel, CurrentTopologyVersion, CurrentSurfaceVersion, Snapshot, VertexCount, TriangleCount);

        UpdatePreviewMesh(MoveTemp(StreamSet), VertexCount, TriangleCount);

        // Pre-warm neighboring LODs opportunistically
        PreWarmNeighboringLODs();
    }
    else
    {
        // Asynchronous path for high-density meshes (level 3+)
        if (bAsyncMeshBuildInProgress.load())
        {
            UE_LOG(LogTemp, Warning, TEXT("⏸️ [ASYNC] Skipping mesh rebuild - async build already in progress (rapid stepping detected)"));
            return; // Skip if already building
        }

        bAsyncMeshBuildInProgress.store(true);
        const double StartTime = FPlatformTime::Seconds();

        // Create snapshot on game thread (captures current simulation state)
        FMeshBuildSnapshot Snapshot = CreateMeshBuildSnapshot();

        // Kick off async mesh build on background thread
        AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, Snapshot, StartTime, CurrentTopologyVersion, CurrentSurfaceVersion, RenderLevel]()
        {
            const uint32 BackgroundThreadID = FPlatformTLS::GetCurrentThreadId();
            UE_LOG(LogTemp, Log, TEXT("⚙️ [ASYNC] Building mesh on background thread (ThreadID: %u)"), BackgroundThreadID);

            // Build mesh on background thread (thread-safe, no UObject access)
            RealtimeMesh::FRealtimeMeshStreamSet StreamSet;
            int32 VertexCount = 0;
            int32 TriangleCount = 0;

            BuildMeshFromSnapshot(Snapshot, StreamSet, VertexCount, TriangleCount);

            const double EndTime = FPlatformTime::Seconds();
            const double BuildTimeMs = (EndTime - StartTime) * 1000.0;

            // Return to game thread to apply mesh update
            AsyncTask(ENamedThreads::GameThread, [this, StreamSet = MoveTemp(StreamSet), VertexCount, TriangleCount, BuildTimeMs, BackgroundThreadID, Snapshot, CurrentTopologyVersion, CurrentSurfaceVersion, RenderLevel]() mutable
            {
                const uint32 GameThreadID = FPlatformTLS::GetCurrentThreadId();
                LastMeshBuildTimeMs = BuildTimeMs;

                UE_LOG(LogTemp, Log, TEXT("✅ [ASYNC] Mesh build completed: %d verts, %d tris, %.2fms (Background: %u → Game: %u)"),
                    VertexCount, TriangleCount, LastMeshBuildTimeMs, BackgroundThreadID, GameThreadID);

                // Milestone 4 Phase 4.2: Cache the snapshot before using it
                CacheLODMesh(RenderLevel, CurrentTopologyVersion, CurrentSurfaceVersion, Snapshot, VertexCount, TriangleCount);

                UpdatePreviewMesh(MoveTemp(StreamSet), VertexCount, TriangleCount);
                bAsyncMeshBuildInProgress.store(false);

                // Pre-warm neighboring LODs opportunistically
                PreWarmNeighboringLODs();
            });
        });

        const uint32 MainThreadID = FPlatformTLS::GetCurrentThreadId();
        UE_LOG(LogTemp, Log, TEXT("🚀 [ASYNC] Mesh build dispatched from game thread (ThreadID: %u, level %d)"), MainThreadID, RenderLevel);
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

void FTectonicSimulationController::SetVelocityVisualizationEnabled(bool bEnabled)
{
    if (bShowVelocityField != bEnabled)
    {
        bShowVelocityField = bEnabled;
        RebuildPreview(); // Refresh mesh with new visualization mode
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
    SpawnParams.Name = TEXT("TectonicPreviewActor");
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

    // Milestone 5 Task 1.2: Initialize orbital camera controller
    CameraController.Initialize(Actor);

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

    UE_LOG(LogTemp, Verbose, TEXT("Drawing %d boundaries at time %.2f My"), Boundaries.Num(), CurrentTimeMy);

    constexpr float RadiusUnits = 6370.0f; // Match mesh scale (1 unit = 1 km)
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
        // Use max elevation (10km) + small buffer to ensure visibility above displaced geometry
        constexpr float BoundaryOffsetKm = 15.0f;
        const FVector CentroidA = FVector(PlateA->Centroid * (RadiusUnits + BoundaryOffsetKm));
        const FVector Midpoint = FVector(BoundaryMidpoint * (RadiusUnits + BoundaryOffsetKm));
        const FVector CentroidB = FVector(PlateB->Centroid * (RadiusUnits + BoundaryOffsetKm));

        const FColor LineColor = GetBoundaryColor(Boundary.BoundaryType);

        // Draw two segments: PlateA centroid → midpoint, midpoint → PlateB centroid
        LineBatcher->DrawLine(CentroidA, Midpoint, LineColor, SDPG_World, LineThickness, LineDuration, BoundaryBatchId);
        LineBatcher->DrawLine(Midpoint, CentroidB, LineColor, SDPG_World, LineThickness, LineDuration, BoundaryBatchId);
    }
#endif
}

void FTectonicSimulationController::BuildMeshFromSnapshot(const FMeshBuildSnapshot& Snapshot, RealtimeMesh::FRealtimeMeshStreamSet& OutStreamSet, int32& OutVertexCount, int32& OutTriangleCount)
{
    using namespace RealtimeMesh;

    OutVertexCount = 0;
    OutTriangleCount = 0;

    const TArray<FVector3d>& RenderVertices = Snapshot.RenderVertices;
    const TArray<int32>& RenderTriangles = Snapshot.RenderTriangles;
    const TArray<int32>& VertexPlateAssignments = Snapshot.VertexPlateAssignments;
    const TArray<FVector3d>& VertexVelocities = Snapshot.VertexVelocities;
    const TArray<double>& VertexStressValues = Snapshot.VertexStressValues;

    if (RenderVertices.Num() == 0 || RenderTriangles.Num() == 0 || VertexPlateAssignments.Num() != RenderVertices.Num())
    {
        return;
    }

    TRealtimeMeshBuilderLocal<uint32, FPackedNormal, FVector2DHalf, 1> Builder(OutStreamSet);
    Builder.EnableTangents();
    Builder.EnableTexCoords();
    Builder.EnableColors();

    constexpr float RadiusUnits = 6370.0f; // 1 Unreal unit == 1 km

    // Helper to generate stable color from plate ID
    auto GetPlateColor = [](int32 PlateID) -> FColor
    {
        constexpr float GoldenRatio = 0.618033988749895f;
        const float Hue = FMath::Fmod(PlateID * GoldenRatio, 1.0f);
        FLinearColor HSV(Hue * 360.0f, 0.7f, 0.9f);
        FLinearColor RGB = HSV.HSVToLinearRGB();
        return RGB.ToFColor(false);
    };

    // Helper to map velocity magnitude to color (blue=slow, red=fast)
    auto GetVelocityColor = [](const FVector3d& Velocity) -> FColor
    {
        const double VelMagnitude = Velocity.Length(); // radians/My
        // Typical plate velocities: 0.01-0.1 rad/My
        // Map to hue: 240° (blue) at 0.01 rad/My → 0° (red) at 0.1 rad/My
        const double NormalizedVel = FMath::Clamp((VelMagnitude - 0.01) / (0.1 - 0.01), 0.0, 1.0);
        const float Hue = FMath::Lerp(240.0f, 0.0f, static_cast<float>(NormalizedVel)); // Blue → Red
        FLinearColor HSV(Hue, 0.8f, 0.9f); // High saturation, high brightness
        FLinearColor RGB = HSV.HSVToLinearRGB();
        return RGB.ToFColor(false);
    };

    // Helper to map stress to color (green=0 → yellow → red=100 MPa)
    auto GetStressColor = [](double StressMPa) -> FColor
    {
        const double NormalizedStress = FMath::Clamp(StressMPa / 100.0, 0.0, 1.0);
        // Hue: 120° (green) at 0 MPa → 60° (yellow) → 0° (red) at 100 MPa
        const float Hue = FMath::Lerp(120.0f, 0.0f, static_cast<float>(NormalizedStress));
        FLinearColor HSV(Hue, 0.8f, 0.9f);
        FLinearColor RGB = HSV.HSVToLinearRGB();
        return RGB.ToFColor(false);
    };

    // Build vertices with elevation displacement and visualization
    TArray<int32> VertexToBuilderIndex;
    VertexToBuilderIndex.SetNumUninitialized(RenderVertices.Num());

    // Store displaced positions for normal recalculation
    TArray<FVector3f> DisplacedPositions;
    DisplacedPositions.SetNumUninitialized(RenderVertices.Num());

    // Milestone 3 Task 2.4: Compression modulus for stress-to-elevation conversion
    constexpr double CompressionModulus = 1.0; // 1 MPa = 1 km elevation (simplified)
    constexpr double MaxElevationKm = 10.0; // ±10km clamp

    for (int32 i = 0; i < RenderVertices.Num(); ++i)
    {
        const FVector3d& Vertex = RenderVertices[i];
        const int32 PlateID = VertexPlateAssignments[i];
        const double StressMPa = VertexStressValues.IsValidIndex(i) ? VertexStressValues[i] : 0.0;

        // Choose color based on visualization mode
        FColor VertexColor;
        if (Snapshot.bShowVelocityField && VertexVelocities.IsValidIndex(i))
        {
            VertexColor = GetVelocityColor(VertexVelocities[i]);
        }
        else if (Snapshot.ElevationMode != EElevationMode::Flat)
        {
            VertexColor = GetStressColor(StressMPa); // Stress heatmap in elevation mode
        }
        else
        {
            VertexColor = GetPlateColor(PlateID); // Default plate colors
        }

        // Base position on sphere
        FVector3f Position = FVector3f(Vertex * RadiusUnits);

        // Milestone 3 Task 2.4: Elevation displacement (only in Displaced mode)
        if (Snapshot.ElevationMode == EElevationMode::Displaced)
        {
            const FVector3f Normal = Position.GetSafeNormal();
            const double ElevationKm = (StressMPa / CompressionModulus) * Snapshot.ElevationScale;
            const double ClampedElevation = FMath::Clamp(ElevationKm, -MaxElevationKm, MaxElevationKm);
            Position += Normal * static_cast<float>(ClampedElevation);
        }

        DisplacedPositions[i] = Position;

        // Normal (will be recalculated if displaced)
        const FVector3f Normal = Position.GetSafeNormal();

        // Calculate tangent for proper lighting
        const FVector3f UpVector = (FMath::Abs(Normal.Z) > 0.99f) ? FVector3f(1.0f, 0.0f, 0.0f) : FVector3f(0.0f, 0.0f, 1.0f);
        const FVector3f TangentX = FVector3f::CrossProduct(Normal, UpVector).GetSafeNormal();
        const FVector2f TexCoord((Normal.X + 1.0f) * 0.5f, (Normal.Y + 1.0f) * 0.5f);

        const int32 VertexId = Builder.AddVertex(Position)
            .SetNormalAndTangent(Normal, TangentX)
            .SetColor(VertexColor)
            .SetTexCoord(TexCoord);

        VertexToBuilderIndex[i] = VertexId;
        OutVertexCount++;
    }

    // Build triangles (groups of 3 indices)
    for (int32 i = 0; i < RenderTriangles.Num(); i += 3)
    {
        const int32 V0 = VertexToBuilderIndex[RenderTriangles[i]];
        const int32 V1 = VertexToBuilderIndex[RenderTriangles[i + 1]];
        const int32 V2 = VertexToBuilderIndex[RenderTriangles[i + 2]];

        // CCW winding when viewed from outside
        Builder.AddTriangle(V0, V2, V1);
        OutTriangleCount++;
    }
}

// Milestone 4 Phase 4.1: Global LOD selection based on camera distance
void FTectonicSimulationController::UpdateLOD()
{
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
            UE_LOG(LogTemp, Log, TEXT("[LOD] Target LOD changed: L%d (d/R=%.2f, distance=%.0f km)"),
                TargetLODLevel, dOverR, CameraDistance);

            // Trigger mesh rebuild at new LOD if different from current
            if (TargetLODLevel != CurrentLODLevel)
            {
                UTectonicSimulationService* Service = GetService();
                if (Service)
                {
                    // Milestone 4 Phase 4.1: Use non-destructive LOD update (preserves simulation state)
                    Service->SetRenderSubdivisionLevel(TargetLODLevel);

                    // Trigger rebuild
                    CurrentLODLevel = TargetLODLevel;
                    BuildAndUpdateMesh();
                }
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
    const FMeshBuildSnapshot& Snapshot, int32 VertexCount, int32 TriangleCount)
{
    TUniquePtr<FCachedLODMesh> NewCache = MakeUnique<FCachedLODMesh>();
    NewCache->Snapshot = Snapshot;
    NewCache->VertexCount = VertexCount;
    NewCache->TriangleCount = TriangleCount;
    NewCache->TopologyVersion = TopologyVersion;
    NewCache->SurfaceDataVersion = SurfaceDataVersion;
    NewCache->CacheTimestamp = FPlatformTime::Seconds();

    // Use Emplace() to handle potential race conditions (overwrites if key exists)
    LODCache.Emplace(LODLevel, MoveTemp(NewCache));

    UE_LOG(LogTemp, Log, TEXT("[LOD Cache] Cached L%d: %d verts, %d tris (Topo:%d, Surface:%d)"),
        LODLevel, VertexCount, TriangleCount, TopologyVersion, SurfaceDataVersion);
}

void FTectonicSimulationController::InvalidateLODCache()
{
    const int32 NumCached = LODCache.Num();
    LODCache.Empty();

    UE_LOG(LogTemp, Warning, TEXT("[LOD Cache] Invalidated %d cached LOD meshes (topology changed)"), NumCached);
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
            UE_LOG(LogTemp, Verbose, TEXT("[LOD Cache] L%d already cached, skipping pre-warm"), LODLevel);
            continue;
        }

        if (bAsyncMeshBuildInProgress.load())
        {
            UE_LOG(LogTemp, Verbose, TEXT("[LOD Cache] Async build in progress, deferring pre-warm of L%d"), LODLevel);
            continue; // Don't queue multiple builds
        }

        UE_LOG(LogTemp, Log, TEXT("[LOD Cache] Pre-warming L%d..."), LODLevel);

        // Trigger async build for this LOD
        const int32 PreviousRenderLevel = Service->GetParameters().RenderSubdivisionLevel;
        Service->SetRenderSubdivisionLevel(LODLevel);

        // Build mesh snapshot
        FMeshBuildSnapshot Snapshot = CreateMeshBuildSnapshot();

        // Restore previous render level
        Service->SetRenderSubdivisionLevel(PreviousRenderLevel);

        // Kick off async build
        bAsyncMeshBuildInProgress.store(true);
        AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, Snapshot, LODLevel, CurrentTopologyVersion, CurrentSurfaceVersion]()
        {
            RealtimeMesh::FRealtimeMeshStreamSet StreamSet;
            int32 VertexCount = 0;
            int32 TriangleCount = 0;

            BuildMeshFromSnapshot(Snapshot, StreamSet, VertexCount, TriangleCount);

            // Return to game thread to cache result
            AsyncTask(ENamedThreads::GameThread, [this, Snapshot, VertexCount, TriangleCount, LODLevel, CurrentTopologyVersion, CurrentSurfaceVersion]()
            {
                CacheLODMesh(LODLevel, CurrentTopologyVersion, CurrentSurfaceVersion, Snapshot, VertexCount, TriangleCount);
                bAsyncMeshBuildInProgress.store(false);
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
            // Rough estimate: each vertex ~60 bytes (position, normal, tangent, UV, color)
            // Each triangle: 12 bytes (3 × uint32 indices)
            const int32 VertexBytes = CachePair.Value->VertexCount * 60;
            const int32 IndexBytes = CachePair.Value->TriangleCount * 12;
            OutTotalCacheSize += VertexBytes + IndexBytes;
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
