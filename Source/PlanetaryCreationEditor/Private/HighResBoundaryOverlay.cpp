// Milestone 4 Task 3.1: High-Resolution Boundary Overlay

#include "PlanetaryCreationLogging.h"
#include "TectonicSimulationController.h"
#include "TectonicSimulationService.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Components/LineBatchComponent.h"

void FTectonicSimulationController::DrawHighResolutionBoundaryOverlay()
{
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

    ULineBatchComponent* LineBatcher = World->PersistentLineBatcher;
    if (!LineBatcher)
    {
        LineBatcher = World->LineBatcher;
    }
    if (!LineBatcher)
    {
        return;
    }

    // Clear existing boundary lines BEFORE early return
    constexpr uint32 HighResBoundaryBatchId = 0x48524253; // 'HRBS' (High-Res Boundary Seam)
    LineBatcher->ClearBatch(HighResBoundaryBatchId);

    if (!bShowBoundaries)
    {
        return; // Boundaries hidden - lines cleared, nothing more to draw
    }

    UTectonicSimulationService* Service = GetService();
    if (!Service)
    {
        return;
    }

    const TArray<FVector3d>& RenderVertices = Service->GetRenderVertices();
    const TArray<int32>& RenderTriangles = Service->GetRenderTriangles();
    const TArray<int32>& VertexPlateAssignments = Service->GetVertexPlateAssignments();
    const TMap<TPair<int32, int32>, FPlateBoundary>& Boundaries = Service->GetBoundaries();

    if (RenderVertices.Num() == 0 || RenderTriangles.Num() == 0)
    {
        return;
    }

    // Milestone 4 Task 3.1: Trace render triangle edges where plate IDs transition
    // Build set of boundary edges (edges connecting vertices with different plate IDs)

    struct FBoundaryEdge
    {
        int32 V0;
        int32 V1;
        int32 PlateA;
        int32 PlateB;
        EBoundaryType BoundaryType;
        EBoundaryState BoundaryState;
        double Stress;
        double RiftWidth;
    };

    TArray<FBoundaryEdge> BoundaryEdges;
    TSet<TPair<int32, int32>> ProcessedEdges; // Avoid duplicate edges

    // Scan all triangles and find edges with plate transitions
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

        auto AddBoundaryEdge = [&](int32 VA, int32 VB, int32 PA, int32 PB)
        {
            if (PA == PB || PA == INDEX_NONE || PB == INDEX_NONE)
                return; // Same plate or unassigned

            // Canonical edge key (sorted)
            const TPair<int32, int32> EdgeKey(FMath::Min(VA, VB), FMath::Max(VA, VB));

            if (ProcessedEdges.Contains(EdgeKey))
                return; // Already processed

            ProcessedEdges.Add(EdgeKey);

            // Look up boundary data
            const TPair<int32, int32> BoundaryKey(FMath::Min(PA, PB), FMath::Max(PA, PB));
            const FPlateBoundary* Boundary = Boundaries.Find(BoundaryKey);

            FBoundaryEdge Edge;
            Edge.V0 = VA;
            Edge.V1 = VB;
            Edge.PlateA = PA;
            Edge.PlateB = PB;
            Edge.BoundaryType = Boundary ? Boundary->BoundaryType : EBoundaryType::Transform;
            Edge.BoundaryState = Boundary ? Boundary->BoundaryState : EBoundaryState::Nascent;
            Edge.Stress = Boundary ? Boundary->AccumulatedStress : 0.0;
            Edge.RiftWidth = Boundary ? Boundary->RiftWidthMeters : 0.0;

            BoundaryEdges.Add(Edge);
        };

        // Check all three edges of the triangle
        AddBoundaryEdge(V0, V1, Plate0, Plate1);
        AddBoundaryEdge(V1, V2, Plate1, Plate2);
        AddBoundaryEdge(V2, V0, Plate2, Plate0);
    }

    UE_LOG(LogPlanetaryCreation, Verbose, TEXT("[HighResBoundary] Found %d boundary edges from %d triangles"),
        BoundaryEdges.Num(), RenderTriangles.Num() / 3);

    // Draw boundary edges with color/width modulation
    constexpr float RadiusUnits = 6370.0f; // Match mesh scale (1 unit = 1 km)
    constexpr float LineDuration = 0.0f; // Persistent

    // (HighResBoundaryBatchId and ClearBatch moved to top of function before early returns)

    auto GetBoundaryColor = [](EBoundaryType Type, EBoundaryState State, double Stress) -> FColor
    {
        // Base color by type
        FColor BaseColor;
        switch (Type)
        {
            case EBoundaryType::Convergent: BaseColor = FColor::Red; break;
            case EBoundaryType::Divergent:  BaseColor = FColor::Green; break;
            case EBoundaryType::Transform:  BaseColor = FColor::Yellow; break;
            default:                         BaseColor = FColor::White; break;
        }

        // Modulate intensity by state
        float Intensity = 1.0f;
        switch (State)
        {
            case EBoundaryState::Nascent: Intensity = 0.5f; break;
            case EBoundaryState::Active:  Intensity = 1.0f; break;
            case EBoundaryState::Dormant: Intensity = 0.3f; break;
            case EBoundaryState::Rifting: return FColor::Cyan; // Special color for rifting
        }

        return FColor(
            static_cast<uint8>(BaseColor.R * Intensity),
            static_cast<uint8>(BaseColor.G * Intensity),
            static_cast<uint8>(BaseColor.B * Intensity),
            255
        );
    };

    auto GetLineThickness = [](double Stress, double RiftWidth) -> float
    {
        // Base thickness
        float Thickness = 15.0f;

        // Increase thickness for high-stress boundaries
        if (Stress > 75.0)
        {
            Thickness = 30.0f; // Extra thick for imminent events
        }
        else if (Stress > 50.0)
        {
            Thickness = 22.0f;
        }

        // Increase thickness for active rifts
        if (RiftWidth > 100000.0) // > 100 km
        {
            Thickness += 10.0f;
        }

        return Thickness;
    };

    for (const FBoundaryEdge& Edge : BoundaryEdges)
    {
        if (!RenderVertices.IsValidIndex(Edge.V0) || !RenderVertices.IsValidIndex(Edge.V1))
            continue;

        const FVector3d& Pos0 = RenderVertices[Edge.V0];
        const FVector3d& Pos1 = RenderVertices[Edge.V1];

        // Scale to world space
        const FVector WorldPos0 = FVector(Pos0 * RadiusUnits);
        const FVector WorldPos1 = FVector(Pos1 * RadiusUnits);

        const FColor LineColor = GetBoundaryColor(Edge.BoundaryType, Edge.BoundaryState, Edge.Stress);
        const float LineThickness = GetLineThickness(Edge.Stress, Edge.RiftWidth);

        LineBatcher->DrawLine(
            WorldPos0,
            WorldPos1,
            LineColor,
            SDPG_World,
            LineThickness,
            LineDuration,
            HighResBoundaryBatchId
        );
    }

    UE_LOG(LogPlanetaryCreation, Verbose, TEXT("[HighResBoundary] Drew %d boundary edges"), BoundaryEdges.Num());
#endif
}
