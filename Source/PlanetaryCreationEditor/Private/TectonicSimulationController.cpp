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
        Service->AdvanceSteps(Steps);
        BuildAndUpdateMesh();
    }
}

void FTectonicSimulationController::RebuildPreview()
{
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

        UpdatePreviewMesh(MoveTemp(StreamSet), VertexCount, TriangleCount);
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
        AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, Snapshot, StartTime]()
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
            AsyncTask(ENamedThreads::GameThread, [this, StreamSet = MoveTemp(StreamSet), VertexCount, TriangleCount, BuildTimeMs, BackgroundThreadID]() mutable
            {
                const uint32 GameThreadID = FPlatformTLS::GetCurrentThreadId();
                LastMeshBuildTimeMs = BuildTimeMs;

                UE_LOG(LogTemp, Log, TEXT("✅ [ASYNC] Mesh build completed: %d verts, %d tris, %.2fms (Background: %u → Game: %u)"),
                    VertexCount, TriangleCount, LastMeshBuildTimeMs, BackgroundThreadID, GameThreadID);

                UpdatePreviewMesh(MoveTemp(StreamSet), VertexCount, TriangleCount);
                bAsyncMeshBuildInProgress.store(false);
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
        DrawBoundaryLines(); // Refresh boundary overlay
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

    // Milestone 3 Task 3.2: Draw boundary overlay after mesh update
    DrawBoundaryLines();
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
