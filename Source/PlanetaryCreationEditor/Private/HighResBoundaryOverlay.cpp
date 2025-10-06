// Milestone 4 Task 3.1: High-Resolution Boundary Overlay

#include "PlanetaryCreationLogging.h"
#include "TectonicSimulationController.h"
#include "TectonicSimulationService.h"
#include "RealtimeMeshComponent/Public/RealtimeMeshSimple.h"
#include "RealtimeMeshComponent/Public/Interface/Core/RealtimeMeshBuilder.h"
#include "RealtimeMeshComponent/Public/Interface/Core/RealtimeMeshSectionConfig.h"
#include "RealtimeMeshComponent/Public/Interface/Core/RealtimeMeshStreamRange.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Components/LineBatchComponent.h"

using namespace RealtimeMesh;

void FTectonicSimulationController::DrawHighResolutionBoundaryOverlay()
{
#if WITH_EDITOR
    // Clear legacy batched lines so stale overlays from older builds disappear
    if (GEditor)
    {
        if (UWorld* World = GEditor->GetEditorWorldContext().World())
        {
            if (ULineBatchComponent* LineBatcher = World->PersistentLineBatcher ? World->PersistentLineBatcher : World->LineBatcher)
            {
                constexpr uint32 HighResBoundaryBatchId = 0x48524253; // 'HRBS'
                LineBatcher->ClearBatch(HighResBoundaryBatchId);
            }
        }
    }

    if (!PreviewMesh.IsValid())
    {
        return;
    }

    URealtimeMeshSimple* Mesh = PreviewMesh.Get();
    const FRealtimeMeshSectionGroupKey OverlayGroupKey = FRealtimeMeshSectionGroupKey::Create(0, FName(TEXT("PlateBoundaries")));
    const FRealtimeMeshSectionKey OverlaySectionKey = FRealtimeMeshSectionKey::CreateForPolyGroup(OverlayGroupKey, 0);

    auto HideOverlaySection = [&]()
    {
        if (bBoundaryOverlayInitialized)
        {
            Mesh->SetSectionVisibility(OverlaySectionKey, false);
        }
    };

    if (!bShowBoundaries)
    {
        HideOverlaySection();
        return;
    }

    UTectonicSimulationService* Service = GetService();
    if (!Service)
    {
        HideOverlaySection();
        return;
    }

    const TArray<FVector3d>& RenderVertices = Service->GetRenderVertices();
    const TArray<int32>& RenderTriangles = Service->GetRenderTriangles();
    const TArray<int32>& VertexPlateAssignments = Service->GetVertexPlateAssignments();
    const TMap<TPair<int32, int32>, FPlateBoundary>& Boundaries = Service->GetBoundaries();

    if (RenderVertices.Num() == 0 || RenderTriangles.Num() == 0 || VertexPlateAssignments.Num() != RenderVertices.Num())
    {
        HideOverlaySection();
        return;
    }

    struct FBoundaryEdge
    {
        int32 V0 = INDEX_NONE;
        int32 V1 = INDEX_NONE;
        EBoundaryType BoundaryType = EBoundaryType::Transform;
        EBoundaryState BoundaryState = EBoundaryState::Nascent;
        double Stress = 0.0;
        double RiftWidth = 0.0;
    };

    TArray<FBoundaryEdge> BoundaryEdges;
    BoundaryEdges.Reserve(RenderTriangles.Num() / 2); // Rough upper bound

    TSet<TPair<int32, int32>> ProcessedEdges;
    ProcessedEdges.Reserve(RenderTriangles.Num());

    for (int32 TriIdx = 0; TriIdx < RenderTriangles.Num(); TriIdx += 3)
    {
        const int32 V0 = RenderTriangles[TriIdx];
        const int32 V1 = RenderTriangles[TriIdx + 1];
        const int32 V2 = RenderTriangles[TriIdx + 2];

        if (!VertexPlateAssignments.IsValidIndex(V0) ||
            !VertexPlateAssignments.IsValidIndex(V1) ||
            !VertexPlateAssignments.IsValidIndex(V2))
        {
            continue;
        }

        const int32 Plate0 = VertexPlateAssignments[V0];
        const int32 Plate1 = VertexPlateAssignments[V1];
        const int32 Plate2 = VertexPlateAssignments[V2];

        auto TryAddEdge = [&](int32 VA, int32 VB, int32 PlateA, int32 PlateB)
        {
            if (PlateA == PlateB || PlateA == INDEX_NONE || PlateB == INDEX_NONE)
            {
                return;
            }

            const TPair<int32, int32> EdgeKey(FMath::Min(VA, VB), FMath::Max(VA, VB));
            if (ProcessedEdges.Contains(EdgeKey))
            {
                return;
            }

            ProcessedEdges.Add(EdgeKey);

            const TPair<int32, int32> BoundaryKey(FMath::Min(PlateA, PlateB), FMath::Max(PlateA, PlateB));
            const FPlateBoundary* Boundary = Boundaries.Find(BoundaryKey);

            FBoundaryEdge Edge;
            Edge.V0 = VA;
            Edge.V1 = VB;
            Edge.BoundaryType = Boundary ? Boundary->BoundaryType : EBoundaryType::Transform;
            Edge.BoundaryState = Boundary ? Boundary->BoundaryState : EBoundaryState::Nascent;
            Edge.Stress = Boundary ? Boundary->AccumulatedStress : 0.0;
            Edge.RiftWidth = Boundary ? Boundary->RiftWidthMeters : 0.0;

            BoundaryEdges.Add(Edge);
        };

        TryAddEdge(V0, V1, Plate0, Plate1);
        TryAddEdge(V1, V2, Plate1, Plate2);
        TryAddEdge(V2, V0, Plate2, Plate0);
    }

    if (BoundaryEdges.Num() == 0)
    {
        HideOverlaySection();
        UE_LOG(LogPlanetaryCreation, Verbose, TEXT("[HighResBoundary] No plate boundary edges detected for current mesh"));
        return;
    }

    const FTectonicSimulationParameters& Parameters = Service->GetParameters();
    const float RadiusUE = MetersToUE(Parameters.PlanetRadius);
    if (RadiusUE <= KINDA_SMALL_NUMBER)
    {
        HideOverlaySection();
        UE_LOG(LogPlanetaryCreation, Warning, TEXT("[HighResBoundary] Planet radius invalid (%.3f UE) - skipping overlay"), RadiusUE);
        return;
    }

    constexpr double OverlayOffsetMeters = 12000.0; // Lift ~12 km to clear displaced peaks
    constexpr double BaseHalfWidthMeters = 2500.0;   // ~5 km wide ribbons

    const float OverlayOffsetUE = MetersToUE(OverlayOffsetMeters);
    const float BaseHalfWidthUE = MetersToUE(BaseHalfWidthMeters);

    auto GetBoundaryColor = [](EBoundaryType Type, EBoundaryState State, double Stress) -> FColor
    {
        FColor BaseColor;
        switch (Type)
        {
            case EBoundaryType::Convergent: BaseColor = FColor::Red; break;
            case EBoundaryType::Divergent:  BaseColor = FColor::Green; break;
            case EBoundaryType::Transform:  BaseColor = FColor::Yellow; break;
            default:                         BaseColor = FColor::White; break;
        }

        float Intensity = 1.0f;
        switch (State)
        {
            case EBoundaryState::Nascent: Intensity = 0.5f; break;
            case EBoundaryState::Active:  Intensity = 1.0f; break;
            case EBoundaryState::Dormant: Intensity = 0.3f; break;
            case EBoundaryState::Rifting: return FColor::Cyan; // Highlight active rifts
        }

        return FColor(
            static_cast<uint8>(BaseColor.R * Intensity),
            static_cast<uint8>(BaseColor.G * Intensity),
            static_cast<uint8>(BaseColor.B * Intensity),
            255
        );
    };

    auto ComputeHalfWidthUE = [&](double Stress, double RiftWidth) -> float
    {
        double WidthMeters = BaseHalfWidthMeters + FMath::Clamp(Stress, 0.0, 120.0) * 20.0; // up to +2.4 km from stress
        if (RiftWidth > 10000.0)
        {
            WidthMeters += FMath::Clamp(RiftWidth * 0.02, 0.0, 10000.0); // widen rifts proportionally (max +10 km)
        }
        return MetersToUE(WidthMeters);
    };

    RealtimeMesh::FRealtimeMeshStreamSet OverlayStreams;
    TRealtimeMeshBuilderLocal<uint32, FPackedNormal, FVector2DHalf, 1> Builder(OverlayStreams);
    Builder.EnableTangents();
    Builder.EnableTexCoords();
    Builder.EnableColors();

    int32 VertexCount = 0;
    int32 TriangleCount = 0;

    for (const FBoundaryEdge& Edge : BoundaryEdges)
    {
        if (!RenderVertices.IsValidIndex(Edge.V0) || !RenderVertices.IsValidIndex(Edge.V1))
        {
            continue;
        }

        const FVector3d& Pos0 = RenderVertices[Edge.V0];
        const FVector3d& Pos1 = RenderVertices[Edge.V1];

        if (Pos0.IsNearlyZero() || Pos1.IsNearlyZero())
        {
            continue;
        }

        const FVector3d EdgeDirection = (Pos1 - Pos0).GetSafeNormal();
        if (EdgeDirection.IsNearlyZero())
        {
            continue;
        }

        const FVector3d FaceNormal = ((Pos0 + Pos1) * 0.5).GetSafeNormal();
        if (FaceNormal.IsNearlyZero())
        {
            continue;
        }

        FVector3d RibbonRight = FVector3d::CrossProduct(FaceNormal, EdgeDirection).GetSafeNormal();
        if (RibbonRight.IsNearlyZero())
        {
            continue;
        }

        const float HalfWidthUE = FMath::Max(ComputeHalfWidthUE(Edge.Stress, Edge.RiftWidth), BaseHalfWidthUE);

        const FVector3d Base0 = Pos0 * static_cast<double>(RadiusUE);
        const FVector3d Base1 = Pos1 * static_cast<double>(RadiusUE);
        const FVector3d NormalOffset = FaceNormal * static_cast<double>(OverlayOffsetUE);
        const FVector3d WidthOffset = RibbonRight * static_cast<double>(HalfWidthUE);

        const FVector3d P0 = Base0 + NormalOffset + WidthOffset;
        const FVector3d P1 = Base0 + NormalOffset - WidthOffset;
        const FVector3d P2 = Base1 + NormalOffset + WidthOffset;
        const FVector3d P3 = Base1 + NormalOffset - WidthOffset;

        const FVector3f Normal = FVector3f(FaceNormal);
        const FVector3f Tangent = FVector3f(EdgeDirection);
        const FColor OverlayColor = GetBoundaryColor(Edge.BoundaryType, Edge.BoundaryState, Edge.Stress);

        const int32 V0Index = Builder.AddVertex(FVector3f(P0))
            .SetNormalAndTangent(Normal, Tangent)
            .SetColor(OverlayColor)
            .SetTexCoord(FVector2f(0.0f, 0.0f));

        const int32 V1Index = Builder.AddVertex(FVector3f(P1))
            .SetNormalAndTangent(Normal, Tangent)
            .SetColor(OverlayColor)
            .SetTexCoord(FVector2f(0.0f, 1.0f));

        const int32 V2Index = Builder.AddVertex(FVector3f(P2))
            .SetNormalAndTangent(Normal, Tangent)
            .SetColor(OverlayColor)
            .SetTexCoord(FVector2f(1.0f, 0.0f));

        const int32 V3Index = Builder.AddVertex(FVector3f(P3))
            .SetNormalAndTangent(Normal, Tangent)
            .SetColor(OverlayColor)
            .SetTexCoord(FVector2f(1.0f, 1.0f));

        Builder.AddTriangle(V0Index, V2Index, V1Index);
        Builder.AddTriangle(V0Index, V3Index, V2Index);

        VertexCount += 4;
        TriangleCount += 2;
    }

    if (VertexCount == 0 || TriangleCount == 0)
    {
        HideOverlaySection();
        UE_LOG(LogPlanetaryCreation, Verbose, TEXT("[HighResBoundary] Generated overlay produced no geometry (filtered degenerate edges)"));
        return;
    }

    if (!bBoundaryOverlayInitialized)
    {
        Mesh->CreateSectionGroup(OverlayGroupKey, MoveTemp(OverlayStreams));

        FRealtimeMeshSectionConfig OverlayConfig(0);
        OverlayConfig.bCastsShadow = false;
        OverlayConfig.bIsMainPassRenderable = true;
        OverlayConfig.bForceOpaque = false;

        Mesh->UpdateSectionConfig(OverlaySectionKey, OverlayConfig);
        Mesh->SetSectionCastShadow(OverlaySectionKey, false);

        bBoundaryOverlayInitialized = true;
    }
    else
    {
        Mesh->UpdateSectionGroup(OverlayGroupKey, MoveTemp(OverlayStreams));
    }

    const FRealtimeMeshStreamRange Range(0, VertexCount, 0, TriangleCount * 3);
    Mesh->UpdateSectionRange(OverlaySectionKey, Range);
    Mesh->SetSectionVisibility(OverlaySectionKey, true);

    UE_LOG(LogPlanetaryCreation, Verbose, TEXT("[HighResBoundary] Drew %d ribbons (%d verts, %d tris)"), BoundaryEdges.Num(), VertexCount, TriangleCount);
#endif
}
