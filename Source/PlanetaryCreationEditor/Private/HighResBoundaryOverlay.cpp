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

namespace PlanetaryCreation::OverlayDiagnostics
{
bool DetectNonManifoldAdjacency(const TMap<int32, TArray<int32>>& Adjacency, int32& OutVertex, int32& OutDegree)
{
    OutVertex = INDEX_NONE;
    OutDegree = 0;

    for (const TPair<int32, TArray<int32>>& Pair : Adjacency)
    {
        TSet<int32> UniqueNeighbors;
        UniqueNeighbors.Append(Pair.Value);
        UniqueNeighbors.Remove(Pair.Key);

        const int32 Degree = UniqueNeighbors.Num();
        if (Degree > 2)
        {
            OutVertex = Pair.Key;
            OutDegree = Degree;
            return true;
        }
    }

    return false;
}
}

static int32 GHighResBoundarySimplifierBranchWarnings = 0;
static constexpr int32 GHighResBoundarySimplifierMaxWarnings = 5;

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
        int32 PlateA = INDEX_NONE;
        int32 PlateB = INDEX_NONE;
        EBoundaryType BoundaryType = EBoundaryType::Transform;
        EBoundaryState BoundaryState = EBoundaryState::Nascent;
        double Stress = 0.0;
        double RiftWidth = 0.0;
    };

    struct FBoundaryPolyline
    {
        TArray<int32> Vertices;
        TArray<FBoundaryEdge> Segments;
        FBoundaryEdge Representative;
    };

    auto SortedEdgeKey = [](int32 A, int32 B)
    {
        return FIntPoint(FMath::Min(A, B), FMath::Max(A, B));
    };

    TArray<FBoundaryEdge> BoundaryEdges;
    BoundaryEdges.Reserve(RenderTriangles.Num() / 2); // Rough upper bound

    TSet<TPair<int32, int32>> ProcessedEdges;
    ProcessedEdges.Reserve(RenderTriangles.Num());
    TMap<FIntPoint, FBoundaryEdge> EdgeLookup;
    TMap<TPair<int32, int32>, TArray<FBoundaryEdge>> BoundaryMap;

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
            Edge.PlateA = BoundaryKey.Key;
            Edge.PlateB = BoundaryKey.Value;
            Edge.BoundaryType = Boundary ? Boundary->BoundaryType : EBoundaryType::Transform;
            Edge.BoundaryState = Boundary ? Boundary->BoundaryState : EBoundaryState::Nascent;
            Edge.Stress = Boundary ? Boundary->AccumulatedStress : 0.0;
            Edge.RiftWidth = Boundary ? Boundary->RiftWidthMeters : 0.0;

            BoundaryEdges.Add(Edge);
            EdgeLookup.Add(SortedEdgeKey(Edge.V0, Edge.V1), Edge);
            BoundaryMap.FindOrAdd(BoundaryKey).Add(Edge);
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
    const int32 OverlayMode = GetBoundaryOverlayMode();
    const bool bSimplifiedMode = OverlayMode == 1;
    const float SimplifiedHalfWidthUE = MetersToUE(400.0); // ~0.8 km total width for seam polylines

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
    int32 SegmentCount = 0;

    auto AddRibbonSegment = [&](int32 VStart, int32 VEnd, const FBoundaryEdge& Edge)
    {
        if (VStart == VEnd)
        {
            return;
        }

        if (!RenderVertices.IsValidIndex(VStart) || !RenderVertices.IsValidIndex(VEnd))
        {
            return;
        }

        const FVector3d& Pos0 = RenderVertices[VStart];
        const FVector3d& Pos1 = RenderVertices[VEnd];

        if (Pos0.IsNearlyZero() || Pos1.IsNearlyZero())
        {
            return;
        }

        const FVector3d EdgeDirection = (Pos1 - Pos0).GetSafeNormal();
        if (EdgeDirection.IsNearlyZero())
        {
            return;
        }

        const FVector3d FaceNormal = ((Pos0 + Pos1) * 0.5).GetSafeNormal();
        if (FaceNormal.IsNearlyZero())
        {
            return;
        }

        FVector3d RibbonRight = FVector3d::CrossProduct(FaceNormal, EdgeDirection).GetSafeNormal();
        if (RibbonRight.IsNearlyZero())
        {
            return;
        }

        const FVector3d Base0 = Pos0 * static_cast<double>(RadiusUE);
        const FVector3d Base1 = Pos1 * static_cast<double>(RadiusUE);
        const double SegmentLengthUE = (Base1 - Base0).Length();

        float HalfWidthUE = bSimplifiedMode
            ? SimplifiedHalfWidthUE
            : FMath::Max(ComputeHalfWidthUE(Edge.Stress, Edge.RiftWidth), BaseHalfWidthUE);

        if (SegmentLengthUE > KINDA_SMALL_NUMBER)
        {
            const float MaxHalfWidthFromLength = static_cast<float>(SegmentLengthUE * 0.35);
            HalfWidthUE = FMath::Min(HalfWidthUE, MaxHalfWidthFromLength);

            const float MinHalfWidthUE = bSimplifiedMode
                ? FMath::Min(SimplifiedHalfWidthUE, MaxHalfWidthFromLength)
                : FMath::Min(BaseHalfWidthUE * 0.3f, MaxHalfWidthFromLength);

            HalfWidthUE = FMath::Max(HalfWidthUE, MinHalfWidthUE);
        }

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
        ++SegmentCount;
    };

    // Continuous ribbon strip for simplified mode - renders entire polyline as connected strip
    auto AddRibbonStrip = [&](const FBoundaryPolyline& Polyline)
    {
        const TArray<int32>& PolylineVertices = Polyline.Vertices;
        const TArray<FBoundaryEdge>& SegmentEdges = Polyline.Segments;
        const FBoundaryEdge& Representative = Polyline.Representative;

        if (PolylineVertices.Num() < 2)
        {
            return;
        }

        // Quick validation - skip invalid polylines
        for (int32 VertexIdx : PolylineVertices)
        {
            if (!RenderVertices.IsValidIndex(VertexIdx))
            {
                UE_LOG(LogPlanetaryCreation, Warning, TEXT("[HighResBoundary] Invalid vertex index %d in polyline, skipping"), VertexIdx);
                return;
            }
            if (RenderVertices[VertexIdx].IsNearlyZero())
            {
                UE_LOG(LogPlanetaryCreation, Warning, TEXT("[HighResBoundary] Zero-length vertex %d in polyline, skipping"), VertexIdx);
                return;
            }
        }

        const float HalfWidthUE = SimplifiedHalfWidthUE;

        struct FRibbonVertexData
        {
            FVector3f Position;
            FVector3f Normal;
            FVector3f Tangent;
            FColor Color;
            FVector2f UV;
        };

        TArray<FRibbonVertexData> LeftVertexData;
        TArray<FRibbonVertexData> RightVertexData;
        LeftVertexData.Reserve(PolylineVertices.Num());
        RightVertexData.Reserve(PolylineVertices.Num());

        bool bStripValid = true;

        for (int32 i = 0; i < PolylineVertices.Num(); ++i)
        {
            const int32 CurrentVertex = PolylineVertices[i];
            const FVector3d CurrentPos = RenderVertices[CurrentVertex];
            const FVector3d CurrentBase = CurrentPos * static_cast<double>(RadiusUE);
            const FVector3d FaceNormal = CurrentPos.GetSafeNormal();

            if (FaceNormal.IsNearlyZero())
            {
                UE_LOG(LogPlanetaryCreation, Warning, TEXT("[HighResBoundary] Invalid face normal at vertex %d, skipping polyline"), i);
                bStripValid = false;
                break;
            }

            // Compute smooth tangent at this point
            FVector3d EdgeTangent = FVector3d::ZeroVector;

            if (i == 0 && PolylineVertices.Num() > 1)
            {
                // First vertex: use outgoing edge direction
                const FVector3d NextPos = RenderVertices[PolylineVertices[i + 1]];
                EdgeTangent = (NextPos - CurrentPos).GetSafeNormal();
            }
            else if (i == PolylineVertices.Num() - 1 && i > 0)
            {
                // Last vertex: use incoming edge direction
                const FVector3d PrevPos = RenderVertices[PolylineVertices[i - 1]];
                EdgeTangent = (CurrentPos - PrevPos).GetSafeNormal();
            }
            else if (i > 0 && i < PolylineVertices.Num() - 1)
            {
                // Interior vertex: check angle to decide between smooth blend or sharp miter
                const FVector3d PrevPos = RenderVertices[PolylineVertices[i - 1]];
                const FVector3d NextPos = RenderVertices[PolylineVertices[i + 1]];
                const FVector3d IncomingDir = (CurrentPos - PrevPos).GetSafeNormal();
                const FVector3d OutgoingDir = (NextPos - CurrentPos).GetSafeNormal();

                // If either direction is zero, fall back to the other
                if (IncomingDir.IsNearlyZero() && !OutgoingDir.IsNearlyZero())
                {
                    EdgeTangent = OutgoingDir;
                }
                else if (!IncomingDir.IsNearlyZero() && OutgoingDir.IsNearlyZero())
                {
                    EdgeTangent = IncomingDir;
                }
                else if (!IncomingDir.IsNearlyZero() && !OutgoingDir.IsNearlyZero())
                {
                    // Check the angle between incoming and outgoing
                    const double DotProduct = FVector3d::DotProduct(IncomingDir, OutgoingDir);
                    const double AngleDegrees = FMath::Acos(FMath::Clamp(DotProduct, -1.0, 1.0)) * (180.0 / PI);

                    // For sharp turns (>90 degrees), use the outgoing direction to avoid spikes
                    // For gentle turns (<90 degrees), blend for smooth appearance
                    if (AngleDegrees > 90.0)
                    {
                        // Sharp turn - just use outgoing direction to avoid weird miter geometry
                        EdgeTangent = OutgoingDir;
                    }
                    else
                    {
                        // Gentle turn - average for smooth curve
                        EdgeTangent = ((IncomingDir + OutgoingDir) * 0.5).GetSafeNormal();
                    }
                }
            }

            if (EdgeTangent.IsNearlyZero())
            {
                UE_LOG(LogPlanetaryCreation, Warning, TEXT("[HighResBoundary] Invalid edge tangent at vertex %d, skipping polyline"), i);
                bStripValid = false;
                break;
            }

            // Compute ribbon perpendicular direction
            FVector3d RibbonRight = FVector3d::CrossProduct(FaceNormal, EdgeTangent).GetSafeNormal();
            if (RibbonRight.IsNearlyZero())
            {
                UE_LOG(LogPlanetaryCreation, Warning, TEXT("[HighResBoundary] Invalid ribbon direction at vertex %d, skipping polyline"), i);
                bStripValid = false;
                break;
            }

            const int32 SegmentIndex = SegmentEdges.Num() > 0
                ? FMath::Clamp(i, 0, SegmentEdges.Num() - 1)
                : 0;

            const FBoundaryEdge& SegmentEdge = SegmentEdges.IsValidIndex(SegmentIndex)
                ? SegmentEdges[SegmentIndex]
                : Representative;

            const FColor OverlayColor = GetBoundaryColor(SegmentEdge.BoundaryType, SegmentEdge.BoundaryState, SegmentEdge.Stress);

            const FVector3d NormalOffset = FaceNormal * static_cast<double>(OverlayOffsetUE);
            const FVector3d WidthOffset = RibbonRight * static_cast<double>(HalfWidthUE);

            const FVector3d LeftPos = CurrentBase + NormalOffset + WidthOffset;
            const FVector3d RightPos = CurrentBase + NormalOffset - WidthOffset;

            const FVector3f Normal = FVector3f(FaceNormal);
            const FVector3f Tangent = FVector3f(EdgeTangent);
            const float UCoord = (PolylineVertices.Num() > 1) ? static_cast<float>(i) / static_cast<float>(PolylineVertices.Num() - 1) : 0.0f;

            LeftVertexData.Add({FVector3f(LeftPos), Normal, Tangent, OverlayColor, FVector2f(UCoord, 0.0f)});
            RightVertexData.Add({FVector3f(RightPos), Normal, Tangent, OverlayColor, FVector2f(UCoord, 1.0f)});
        }

        if (!bStripValid)
        {
            return;
        }

        // Build continuous strip with shared vertices between segments
        TArray<int32> LeftEdgeVertices;
        TArray<int32> RightEdgeVertices;
        LeftEdgeVertices.Reserve(LeftVertexData.Num());
        RightEdgeVertices.Reserve(RightVertexData.Num());

        for (int32 i = 0; i < LeftVertexData.Num(); ++i)
        {
            const FRibbonVertexData& LeftData = LeftVertexData[i];
            const FRibbonVertexData& RightData = RightVertexData[i];

            const int32 LeftIndex = Builder.AddVertex(LeftData.Position)
                .SetNormalAndTangent(LeftData.Normal, LeftData.Tangent)
                .SetColor(LeftData.Color)
                .SetTexCoord(LeftData.UV);

            const int32 RightIndex = Builder.AddVertex(RightData.Position)
                .SetNormalAndTangent(RightData.Normal, RightData.Tangent)
                .SetColor(RightData.Color)
                .SetTexCoord(RightData.UV);

            LeftEdgeVertices.Add(LeftIndex);
            RightEdgeVertices.Add(RightIndex);

            VertexCount += 2;
        }

        // Build triangle strip connecting left and right edges
        for (int32 i = 0; i < PolylineVertices.Num() - 1; ++i)
        {
            const int32 L0 = LeftEdgeVertices[i];
            const int32 R0 = RightEdgeVertices[i];
            const int32 L1 = LeftEdgeVertices[i + 1];
            const int32 R1 = RightEdgeVertices[i + 1];

            // Two triangles per quad segment
            Builder.AddTriangle(L0, L1, R0);
            Builder.AddTriangle(L1, R1, R0);

            TriangleCount += 2;
        }

        ++SegmentCount;
    };

    if (OverlayMode == 1)
    {
        TArray<FBoundaryPolyline> Polylines;

        for (const TPair<TPair<int32, int32>, TArray<FBoundaryEdge>>& Entry : BoundaryMap)
        {
            const TArray<FBoundaryEdge>& EdgesForBoundary = Entry.Value;
            if (EdgesForBoundary.Num() == 0)
            {
                continue;
            }

            TMap<int32, TArray<int32>> Adjacency;
            TSet<FIntPoint> RemainingEdges;

            for (const FBoundaryEdge& Edge : EdgesForBoundary)
            {
                Adjacency.FindOrAdd(Edge.V0).Add(Edge.V1);
                Adjacency.FindOrAdd(Edge.V1).Add(Edge.V0);
                RemainingEdges.Add(SortedEdgeKey(Edge.V0, Edge.V1));
            }

            int32 BranchVertex = INDEX_NONE;
            int32 BranchDegree = 0;
            if (PlanetaryCreation::OverlayDiagnostics::DetectNonManifoldAdjacency(Adjacency, BranchVertex, BranchDegree))
            {
                if (GHighResBoundarySimplifierBranchWarnings < GHighResBoundarySimplifierMaxWarnings)
                {
                    ++GHighResBoundarySimplifierBranchWarnings;
                    UE_LOG(LogPlanetaryCreation, Warning,
                        TEXT("[HighResBoundary] Simplified overlay fallback for boundary %d/%d due to branching vertex %d (degree %d). Using detailed ribbons."),
                        Entry.Key.Key,
                        Entry.Key.Value,
                        BranchVertex,
                        BranchDegree);
                }

                for (const FBoundaryEdge& Edge : EdgesForBoundary)
                {
                    AddRibbonSegment(Edge.V0, Edge.V1, Edge);
                }
                continue;
            }

            // Trace polylines from remaining edges
            // Each iteration builds one polyline chain or loop
            while (RemainingEdges.Num() > 0)
            {
                // Pick any remaining edge as starting point
                const FIntPoint FirstEdge = *RemainingEdges.CreateConstIterator();
                RemainingEdges.Remove(FirstEdge);

                FBoundaryPolyline Polyline;
                Polyline.Vertices.Add(FirstEdge.X);
                Polyline.Vertices.Add(FirstEdge.Y);

                if (const FBoundaryEdge* FirstEdgeData = EdgeLookup.Find(FirstEdge))
                {
                    Polyline.Representative = *FirstEdgeData;
                    Polyline.Segments.Add(*FirstEdgeData);
                }
                else
                {
                    Polyline.Representative = EdgesForBoundary[0];
                    Polyline.Segments.Add(Polyline.Representative);
                }

                // Try to extend the polyline in both directions
                for (int32 Direction = 0; Direction < 2; ++Direction)
                {
                    bool bExtended = true;
                    while (bExtended)
                    {
                        bExtended = false;

                        // Get the endpoint we're trying to extend from
                        const int32 EndpointIdx = (Direction == 0) ? 0 : Polyline.Vertices.Num() - 1;
                        const int32 Endpoint = Polyline.Vertices[EndpointIdx];

                        // Look for an edge connected to this endpoint
                        const TArray<int32>* Neighbors = Adjacency.Find(Endpoint);
                        if (!Neighbors)
                        {
                            continue;
                        }

                        for (int32 Neighbor : *Neighbors)
                        {
                            const FIntPoint EdgeKey = SortedEdgeKey(Endpoint, Neighbor);
                            if (RemainingEdges.Contains(EdgeKey))
                            {
                                // Found an edge to extend with
                                RemainingEdges.Remove(EdgeKey);

                                if (Direction == 0)
                                {
                                    // Extending from the front
                                    Polyline.Vertices.Insert(Neighbor, 0);
                                    if (const FBoundaryEdge* EdgeData = EdgeLookup.Find(EdgeKey))
                                    {
                                        Polyline.Segments.Insert(*EdgeData, 0);
                                    }
                                    else
                                    {
                                        Polyline.Segments.Insert(Polyline.Representative, 0);
                                    }
                                }
                                else
                                {
                                    // Extending from the back
                                    Polyline.Vertices.Add(Neighbor);
                                    if (const FBoundaryEdge* EdgeData = EdgeLookup.Find(EdgeKey))
                                    {
                                        Polyline.Segments.Add(*EdgeData);
                                    }
                                    else
                                    {
                                        Polyline.Segments.Add(Polyline.Representative);
                                    }
                                }

                                bExtended = true;
                                break; // Found one edge, continue extending
                            }
                        }
                    }
                }

                if (Polyline.Vertices.Num() >= 2)
                {
                    Polylines.Add(MoveTemp(Polyline));
                }
            }
        }

            // Render each polyline as a continuous ribbon strip for smooth connected appearance
            for (const FBoundaryPolyline& Polyline : Polylines)
            {
                AddRibbonStrip(Polyline);
            }
        }
    else
    {
        for (const FBoundaryEdge& Edge : BoundaryEdges)
        {
            AddRibbonSegment(Edge.V0, Edge.V1, Edge);
        }
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

    if (OverlayMode == 1)
    {
        UE_LOG(LogPlanetaryCreation, Verbose, TEXT("[HighResBoundary] Drew %d simplified segments (%d verts, %d tris)"), SegmentCount, VertexCount, TriangleCount);
    }
    else
    {
        UE_LOG(LogPlanetaryCreation, Verbose, TEXT("[HighResBoundary] Drew %d ribbons (%d verts, %d tris)"), BoundaryEdges.Num(), VertexCount, TriangleCount);
    }
#endif
}
