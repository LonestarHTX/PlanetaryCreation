// Milestone 4 Task 1.2: Plate Split & Merge Implementation
// Implements rift-driven splitting and subduction-driven merging per paper Sections 4.2-4.3

#include "PlanetaryCreationLogging.h"
#include "TectonicSimulationService.h"
#include "HAL/PlatformTime.h"

// Milestone 4 Task 1.2: Detect and execute plate splits (rift-driven)
void UTectonicSimulationService::DetectAndExecutePlateSplits()
{
    if (!Parameters.bEnablePlateTopologyChanges)
        return;

    // Iterate boundaries to find sustained divergent boundaries
    TArray<TPair<int32, int32>> CandidateSplits;

    for (auto& BoundaryPair : Boundaries)
    {
        const TPair<int32, int32>& PlateIDs = BoundaryPair.Key;
        FPlateBoundary& Boundary = BoundaryPair.Value;

        // Check if divergent boundary meets split criteria
        // Two paths: (1) rift-based (if rift propagation enabled), (2) duration-based (legacy)
        bool bMeetsSplitCriteria = false;

        if (Parameters.bEnableRiftPropagation && Boundary.BoundaryState == EBoundaryState::Rifting)
        {
            // Rift-based split: check if rift width exceeds threshold
            bMeetsSplitCriteria = (Boundary.RiftWidthMeters > Parameters.RiftSplitThresholdMeters);
        }
        else
        {
            // Legacy duration-based split: sustained divergence
            bMeetsSplitCriteria = (Boundary.BoundaryType == EBoundaryType::Divergent &&
                                   Boundary.RelativeVelocity > Parameters.SplitVelocityThreshold &&
                                   Boundary.DivergentDurationMy > Parameters.SplitDurationThreshold);
        }

        if (bMeetsSplitCriteria)
        {
            // Candidate for split - pick one of the two plates (deterministic: always choose lower PlateID)
            const int32 PlateToSplit = FMath::Min(PlateIDs.Key, PlateIDs.Value);

            // Check if we already have a split candidate for this plate (avoid double-splitting)
            bool bAlreadyQueued = false;
            for (const TPair<int32, int32>& Existing : CandidateSplits)
            {
                if (Existing.Key == PlateToSplit)
                {
                    bAlreadyQueued = true;
                    break;
                }
            }

            if (!bAlreadyQueued)
            {
                CandidateSplits.Add(TPair<int32, int32>(PlateToSplit, PlateIDs.Key == PlateToSplit ? PlateIDs.Value : PlateIDs.Key));

                if (Parameters.bEnableRiftPropagation && Boundary.BoundaryState == EBoundaryState::Rifting)
                {
                    UE_LOG(LogPlanetaryCreation, Warning, TEXT("[Split Detection] Plate %d candidate for rift-based split along boundary with Plate %d (rift width=%.0f m > %.0f m, velocity=%.4f rad/My)"),
                        PlateToSplit, PlateIDs.Key == PlateToSplit ? PlateIDs.Value : PlateIDs.Key,
                        Boundary.RiftWidthMeters, Parameters.RiftSplitThresholdMeters, Boundary.RelativeVelocity);
                }
                else
                {
                    UE_LOG(LogPlanetaryCreation, Warning, TEXT("[Split Detection] Plate %d candidate for duration-based split along boundary with Plate %d (velocity=%.4f rad/My, duration=%.1f My)"),
                        PlateToSplit, PlateIDs.Key == PlateToSplit ? PlateIDs.Value : PlateIDs.Key,
                        Boundary.RelativeVelocity, Boundary.DivergentDurationMy);
                }
            }
        }
    }

    // Execute splits (limit to 1 per step to avoid cascading instability)
    if (CandidateSplits.Num() > 0)
    {
        const TPair<int32, int32>& SplitPair = CandidateSplits[0];
        const int32 PlateToSplit = SplitPair.Key;
        const int32 NeighborPlate = SplitPair.Value;

        // Find the boundary key
        TPair<int32, int32> BoundaryKey = PlateToSplit < NeighborPlate
            ? TPair<int32, int32>(PlateToSplit, NeighborPlate)
            : TPair<int32, int32>(NeighborPlate, PlateToSplit);

        if (FPlateBoundary* Boundary = Boundaries.Find(BoundaryKey))
        {
            const bool Success = SplitPlate(PlateToSplit, BoundaryKey, *Boundary);
            if (Success)
            {
                UE_LOG(LogPlanetaryCreation, Log, TEXT("[Split] Successfully split Plate %d → new plate count: %d"), PlateToSplit, Plates.Num());
            }
            else
            {
                UE_LOG(LogPlanetaryCreation, Warning, TEXT("[Split] Failed to split Plate %d (validation failed)"), PlateToSplit);
            }
        }
    }
}

// Milestone 4 Task 1.2: Detect and execute plate merges (subduction-driven)
void UTectonicSimulationService::DetectAndExecutePlateMerges()
{
    if (!Parameters.bEnablePlateTopologyChanges)
        return;

    // Iterate boundaries to find sustained convergent boundaries with small plates
    TArray<TPair<TPair<int32, int32>, TPair<int32, int32>>> CandidateMerges; // (ConsumedID, SurvivorID), (PlateA, PlateB) boundary key

    for (const auto& BoundaryPair : Boundaries)
    {
        const TPair<int32, int32>& PlateIDs = BoundaryPair.Key;
        const FPlateBoundary& Boundary = BoundaryPair.Value;

        // Check if convergent boundary meets merge criteria
        if (Boundary.BoundaryType == EBoundaryType::Convergent &&
            Boundary.AccumulatedStress > Parameters.MergeStressThreshold)
        {
            // Find the two plates
            const FTectonicPlate* PlateA = Plates.FindByPredicate([ID = PlateIDs.Key](const FTectonicPlate& P) { return P.PlateID == ID; });
            const FTectonicPlate* PlateB = Plates.FindByPredicate([ID = PlateIDs.Value](const FTectonicPlate& P) { return P.PlateID == ID; });

            if (!PlateA || !PlateB)
                continue;

            // Calculate plate areas
            const double AreaA = ComputePlateArea(*PlateA);
            const double AreaB = ComputePlateArea(*PlateB);

            // Check if area ratio meets threshold (smaller plate < 25% of larger)
            const double AreaRatio = FMath::Min(AreaA, AreaB) / FMath::Max(AreaA, AreaB);

            if (AreaRatio < Parameters.MergeAreaRatioThreshold)
            {
                // Determine consumed vs survivor (smaller gets consumed)
                const int32 ConsumedID = (AreaA < AreaB) ? PlateIDs.Key : PlateIDs.Value;
                const int32 SurvivorID = (AreaA < AreaB) ? PlateIDs.Value : PlateIDs.Key;

                CandidateMerges.Add(TPair<TPair<int32, int32>, TPair<int32, int32>>(
                    TPair<int32, int32>(ConsumedID, SurvivorID), PlateIDs));

                UE_LOG(LogPlanetaryCreation, Warning, TEXT("[Merge Detection] Plate %d candidate for merge into Plate %d (stress=%.1f MPa, area ratio=%.2f%%)"),
                    ConsumedID, SurvivorID, Boundary.AccumulatedStress, AreaRatio * 100.0);
            }
        }
    }

    // Execute merges (limit to 1 per step to avoid cascading instability)
    if (CandidateMerges.Num() > 0)
    {
        const auto& MergePair = CandidateMerges[0];
        const int32 ConsumedID = MergePair.Key.Key;
        const int32 SurvivorID = MergePair.Key.Value;
        const TPair<int32, int32>& BoundaryKey = MergePair.Value;

        if (const FPlateBoundary* Boundary = Boundaries.Find(BoundaryKey))
        {
            const bool Success = MergePlates(ConsumedID, SurvivorID, BoundaryKey, *Boundary);
            if (Success)
            {
                UE_LOG(LogPlanetaryCreation, Log, TEXT("[Merge] Successfully merged Plate %d into Plate %d → new plate count: %d"),
                    ConsumedID, SurvivorID, Plates.Num());
            }
            else
            {
                UE_LOG(LogPlanetaryCreation, Warning, TEXT("[Merge] Failed to merge Plate %d into Plate %d (validation failed)"),
                    ConsumedID, SurvivorID);
            }
        }
    }
}

// Milestone 4 Task 1.2: Execute plate split along divergent boundary
bool UTectonicSimulationService::SplitPlate(int32 PlateID, const TPair<int32, int32>& BoundaryKey, const FPlateBoundary& Boundary)
{
    // Find the plate to split
    FTectonicPlate* OriginalPlate = Plates.FindByPredicate([PlateID](const FTectonicPlate& P) { return P.PlateID == PlateID; });
    if (!OriginalPlate)
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("[Split] Plate %d not found"), PlateID);
        return false;
    }

    // Create snapshot for rollback
    const int32 OriginalPlateCount = Plates.Num();

    // Deterministic split algorithm:
    // 1. Create new plate by duplicating original
    // 2. Derive child Euler poles from parent pole + rift direction (NO random offsets)
    // 3. Offset centroids perpendicular to rift line
    // 4. Trigger full re-tessellation to rebuild mesh

    FTectonicPlate NewPlate = *OriginalPlate;
    NewPlate.PlateID = Plates.Num(); // New ID = next available

    // ===== DETERMINISTIC EULER POLE DERIVATION =====
    //
    // Physical model: Rifting occurs when a plate experiences extensional stress perpendicular
    // to the rift axis. The two child plates inherit the parent's motion but diverge along the rift.
    //
    // Math:
    //   1. Rift direction R = normalized vector along boundary midline (great circle tangent)
    //   2. Parent angular velocity ω_parent = EulerPoleAxis × AngularVelocity
    //   3. Child A: ω_A = ω_parent + (divergence component along R)
    //   4. Child B: ω_B = ω_parent - (divergence component along R)
    //
    // The divergence component is derived from the rift velocity (RelativeVelocity):
    //   Divergence magnitude = RelativeVelocity / 2 (split equally between children)
    //
    // This ensures:
    //   - Conservation of angular momentum (ω_A + ω_B ≈ 2 × ω_parent)
    //   - Determinism (no random offsets, same seed → same result)
    //   - Physical plausibility (motion guided by rift geometry)

    // Step 1: Compute rift direction (great circle tangent at boundary midpoint)
    FVector3d BoundaryV0 = SharedVertices[Boundary.SharedEdgeVertices[0]];
    FVector3d BoundaryV1 = SharedVertices[Boundary.SharedEdgeVertices[1]];
    const FVector3d BoundaryMidpoint = ((BoundaryV0 + BoundaryV1) * 0.5).GetSafeNormal();

    // Rift direction = tangent to great circle connecting boundary vertices
    FVector3d RiftDirection = (BoundaryV1 - BoundaryV0).GetSafeNormal();

    // Ensure rift direction is tangent to sphere at midpoint (project onto tangent plane)
    RiftDirection = (RiftDirection - BoundaryMidpoint * FVector3d::DotProduct(RiftDirection, BoundaryMidpoint)).GetSafeNormal();

    // Step 2: Parent angular velocity vector
    const FVector3d ParentOmega = OriginalPlate->EulerPoleAxis * OriginalPlate->AngularVelocity;

    // Step 3: Divergence component (half the relative velocity for each child)
    const double DivergenceMagnitude = Boundary.RelativeVelocity * 0.5; // Split equally
    const FVector3d DivergenceVector = RiftDirection * DivergenceMagnitude;

    // Step 4: Child angular velocity vectors (deterministic split)
    const FVector3d ChildA_Omega = ParentOmega + DivergenceVector; // Original plate (ChildA) diverges in +R direction
    const FVector3d ChildB_Omega = ParentOmega - DivergenceVector; // New plate (ChildB) diverges in -R direction

    // Step 5: Convert back to Euler pole axis + angular velocity
    OriginalPlate->AngularVelocity = ChildA_Omega.Length();
    OriginalPlate->EulerPoleAxis = (OriginalPlate->AngularVelocity > 1e-6)
        ? ChildA_Omega.GetSafeNormal()
        : OriginalPlate->EulerPoleAxis; // Preserve axis if velocity is near-zero

    NewPlate.AngularVelocity = ChildB_Omega.Length();
    NewPlate.EulerPoleAxis = (NewPlate.AngularVelocity > 1e-6)
        ? ChildB_Omega.GetSafeNormal()
        : OriginalPlate->EulerPoleAxis; // Fallback to parent axis

    // Log the derivation for validation
    UE_LOG(LogPlanetaryCreation, Log, TEXT("[Split Derivation] Parent: ω=%.4f rad/My, axis=(%.3f,%.3f,%.3f)"),
        ParentOmega.Length(),
        OriginalPlate->EulerPoleAxis.X, OriginalPlate->EulerPoleAxis.Y, OriginalPlate->EulerPoleAxis.Z);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("[Split Derivation] Rift direction: R=(%.3f,%.3f,%.3f), divergence=%.4f rad/My"),
        RiftDirection.X, RiftDirection.Y, RiftDirection.Z, DivergenceMagnitude);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("[Split Derivation] Child A: ω=%.4f rad/My, axis=(%.3f,%.3f,%.3f)"),
        OriginalPlate->AngularVelocity,
        OriginalPlate->EulerPoleAxis.X, OriginalPlate->EulerPoleAxis.Y, OriginalPlate->EulerPoleAxis.Z);
    UE_LOG(LogPlanetaryCreation, Log, TEXT("[Split Derivation] Child B: ω=%.4f rad/My, axis=(%.3f,%.3f,%.3f)"),
        NewPlate.AngularVelocity,
        NewPlate.EulerPoleAxis.X, NewPlate.EulerPoleAxis.Y, NewPlate.EulerPoleAxis.Z);

    // ===== DETERMINISTIC CENTROID OFFSET =====
    //
    // Offset centroids perpendicular to rift line to prevent immediate re-merge
    // Offset direction = cross product of rift direction and boundary midpoint normal
    // This creates separation tangent to the sphere surface

    const FVector3d OffsetDirection = FVector3d::CrossProduct(RiftDirection, BoundaryMidpoint).GetSafeNormal();
    const double OffsetMagnitude = 0.08; // ~4.6° offset (conservative to avoid excessive drift)

    OriginalPlate->Centroid = (OriginalPlate->Centroid + OffsetDirection * OffsetMagnitude).GetSafeNormal();
    NewPlate.Centroid = (NewPlate.Centroid - OffsetDirection * OffsetMagnitude).GetSafeNormal();

    // Add new plate to simulation
    Plates.Add(NewPlate);

    // Log topology event
    FPlateTopologyEvent Event;
    Event.EventType = EPlateTopologyEventType::Split;
    Event.PlateIDs.Add(PlateID);
    Event.PlateIDs.Add(NewPlate.PlateID);
    Event.TimestampMy = CurrentTimeMy;
    Event.StressAtEvent = Boundary.AccumulatedStress;
    Event.VelocityAtEvent = Boundary.RelativeVelocity;
    TopologyEvents.Add(Event);

    UE_LOG(LogPlanetaryCreation, Log, TEXT("[Split] Plate %d split into Plate %d at %.2f My (stress=%.1f MPa, velocity=%.4f rad/My)"),
        PlateID, NewPlate.PlateID, CurrentTimeMy, Boundary.AccumulatedStress, Boundary.RelativeVelocity);

    // Trigger full re-tessellation to rebuild mesh with new plate configuration
    // NOTE: We DON'T call GenerateIcospherePlates() here because that would reset to initial topology
    // Instead, we rebuild mesh/boundaries from the modified plate list (which now includes the new plate)

    // For now, just use Voronoi-based tessellation from plate centroids
    // This is a simplified approach - proper incremental split would preserve icosphere connectivity
    BuildBoundaryAdjacencyMap(); // Rebuild boundaries for new plate count
    GenerateRenderMesh();         // Regenerate render mesh
    BuildVoronoiMapping();        // Reassign vertices to plates (including new plate)
    ComputeVelocityField();
    InterpolateStressToVertices();

    // Validate plate count increased
    if (Plates.Num() != OriginalPlateCount + 1)
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("[Split] Plate count mismatch after split (expected %d, got %d)"),
            OriginalPlateCount + 1, Plates.Num());
        return false;
    }

    // Update InitialPlateCentroids array to match new plate count
    InitialPlateCentroids.SetNum(Plates.Num());
    for (int32 i = 0; i < Plates.Num(); ++i)
    {
        InitialPlateCentroids[i] = Plates[i].Centroid;
    }

    // Milestone 4 Phase 4.2: Increment topology version (split changed geometry)
    TopologyVersion++;
    UE_LOG(LogPlanetaryCreation, Verbose, TEXT("[LOD Cache] Topology version incremented after split: %d"), TopologyVersion);

    return true;
}

// Milestone 4 Task 1.2: Execute plate merge (consume smaller plate into larger)
bool UTectonicSimulationService::MergePlates(int32 ConsumedPlateID, int32 SurvivorPlateID, const TPair<int32, int32>& BoundaryKey, const FPlateBoundary& Boundary)
{
    // Find plates
    const int32 ConsumedIndex = Plates.IndexOfByPredicate([ConsumedPlateID](const FTectonicPlate& P) { return P.PlateID == ConsumedPlateID; });
    const int32 SurvivorIndex = Plates.IndexOfByPredicate([SurvivorPlateID](const FTectonicPlate& P) { return P.PlateID == SurvivorPlateID; });

    if (ConsumedIndex == INDEX_NONE || SurvivorIndex == INDEX_NONE)
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("[Merge] Plate %d or %d not found"), ConsumedPlateID, SurvivorPlateID);
        return false;
    }

    FTectonicPlate& SurvivorPlate = Plates[SurvivorIndex];
    const FTectonicPlate& ConsumedPlate = Plates[ConsumedIndex];

    // Create snapshot for rollback
    const int32 OriginalPlateCount = Plates.Num();

    // ===== DETERMINISTIC MERGE: AREA-WEIGHTED EULER POLE BLENDING =====
    //
    // Physical model: When a smaller plate is consumed (subducted), the survivor plate
    // inherits a blended motion proportional to the mass (area) of each plate.
    //
    // Math:
    //   1. Compute area proxies: A_survivor, A_consumed (using vertex counts)
    //   2. Extract angular velocity vectors: ω_survivor, ω_consumed
    //   3. Blend: ω_merged = (A_survivor × ω_survivor + A_consumed × ω_consumed) / (A_survivor + A_consumed)
    //   4. Convert back to Euler pole axis + angular velocity
    //   5. Blend centroids using same weights

    // Step 1: Area proxies (vertex count is proportional to spherical area for Voronoi cells)
    const double AreaSurvivor = static_cast<double>(SurvivorPlate.VertexIndices.Num());
    const double AreaConsumed = static_cast<double>(ConsumedPlate.VertexIndices.Num());
    const double TotalArea = AreaSurvivor + AreaConsumed;

    if (TotalArea < 1.0)
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("[Merge] Invalid plate areas (survivor=%d, consumed=%d vertices)"),
            SurvivorPlate.VertexIndices.Num(), ConsumedPlate.VertexIndices.Num());
        return false;
    }

    // Step 2: Angular velocity vectors
    const FVector3d OmegaSurvivor = SurvivorPlate.EulerPoleAxis * SurvivorPlate.AngularVelocity;
    const FVector3d OmegaConsumed = ConsumedPlate.EulerPoleAxis * ConsumedPlate.AngularVelocity;

    // Step 3: Area-weighted blend
    const FVector3d OmegaMerged = (OmegaSurvivor * AreaSurvivor + OmegaConsumed * AreaConsumed) / TotalArea;

    // Step 4: Convert back to axis + magnitude
    const double MergedAngularVelocity = OmegaMerged.Length();
    const FVector3d MergedEulerPoleAxis = (MergedAngularVelocity > 1e-6)
        ? OmegaMerged.GetSafeNormal()
        : SurvivorPlate.EulerPoleAxis; // Fallback if merged velocity is zero

    // Step 5: Blend centroids
    const FVector3d MergedCentroid = ((SurvivorPlate.Centroid * AreaSurvivor + ConsumedPlate.Centroid * AreaConsumed) / TotalArea).GetSafeNormal();

    // Log the derivation for validation
    UE_LOG(LogPlanetaryCreation, Log, TEXT("[Merge Derivation] Survivor: ω=%.4f rad/My, axis=(%.3f,%.3f,%.3f), area=%d vertices"),
        SurvivorPlate.AngularVelocity,
        SurvivorPlate.EulerPoleAxis.X, SurvivorPlate.EulerPoleAxis.Y, SurvivorPlate.EulerPoleAxis.Z,
        SurvivorPlate.VertexIndices.Num());
    UE_LOG(LogPlanetaryCreation, Log, TEXT("[Merge Derivation] Consumed: ω=%.4f rad/My, axis=(%.3f,%.3f,%.3f), area=%d vertices"),
        ConsumedPlate.AngularVelocity,
        ConsumedPlate.EulerPoleAxis.X, ConsumedPlate.EulerPoleAxis.Y, ConsumedPlate.EulerPoleAxis.Z,
        ConsumedPlate.VertexIndices.Num());
    UE_LOG(LogPlanetaryCreation, Log, TEXT("[Merge Derivation] Merged: ω=%.4f rad/My, axis=(%.3f,%.3f,%.3f)"),
        MergedAngularVelocity,
        MergedEulerPoleAxis.X, MergedEulerPoleAxis.Y, MergedEulerPoleAxis.Z);

    // Apply merged values to survivor
    SurvivorPlate.AngularVelocity = MergedAngularVelocity;
    SurvivorPlate.EulerPoleAxis = MergedEulerPoleAxis;
    SurvivorPlate.Centroid = MergedCentroid;

    // ===== STRESS HISTORY CARRY-OVER =====
    // Survivor inherits the higher accumulated stress (conservative: retain worst-case scenario)
    // Note: Stress is already stored per-boundary, not per-plate, so this is informational only
    // Future enhancement: track per-plate stress history if needed

    // Log topology event before removal
    FPlateTopologyEvent Event;
    Event.EventType = EPlateTopologyEventType::Merge;
    Event.PlateIDs.Add(ConsumedPlateID);
    Event.PlateIDs.Add(SurvivorPlateID);
    Event.TimestampMy = CurrentTimeMy;
    Event.StressAtEvent = Boundary.AccumulatedStress;
    Event.VelocityAtEvent = Boundary.RelativeVelocity;
    TopologyEvents.Add(Event);

    UE_LOG(LogPlanetaryCreation, Log, TEXT("[Merge] Plate %d consumed by Plate %d at %.2f My (stress=%.1f MPa)"),
        ConsumedPlateID, SurvivorPlateID, CurrentTimeMy, Boundary.AccumulatedStress);

    // Remove consumed plate
    Plates.RemoveAt(ConsumedIndex);

    // Trigger full re-tessellation
    // NOTE: We DON'T call GenerateIcospherePlates() here because that would reset to initial topology
    BuildBoundaryAdjacencyMap();
    GenerateRenderMesh();
    BuildVoronoiMapping();
    ComputeVelocityField();
    InterpolateStressToVertices();

    // Validate plate count decreased
    if (Plates.Num() != OriginalPlateCount - 1)
    {
        UE_LOG(LogPlanetaryCreation, Error, TEXT("[Merge] Plate count mismatch after merge (expected %d, got %d)"),
            OriginalPlateCount - 1, Plates.Num());
        return false;
    }

    // Update InitialPlateCentroids array to match new plate count
    InitialPlateCentroids.SetNum(Plates.Num());
    for (int32 i = 0; i < Plates.Num(); ++i)
    {
        InitialPlateCentroids[i] = Plates[i].Centroid;
    }

    // Milestone 4 Phase 4.2: Increment topology version (merge changed geometry)
    TopologyVersion++;
    UE_LOG(LogPlanetaryCreation, Verbose, TEXT("[LOD Cache] Topology version incremented after merge: %d"), TopologyVersion);

    return true;
}

// Milestone 4 Task 1.2: Calculate plate area (spherical triangles using Girard's theorem)
double UTectonicSimulationService::ComputePlateArea(const FTectonicPlate& Plate) const
{
    if (Plate.VertexIndices.Num() != 3)
    {
        // Not a triangular plate (shouldn't happen with icosphere)
        return 0.0;
    }

    const FVector3d& V0 = SharedVertices[Plate.VertexIndices[0]];
    const FVector3d& V1 = SharedVertices[Plate.VertexIndices[1]];
    const FVector3d& V2 = SharedVertices[Plate.VertexIndices[2]];

    // Spherical triangle area using Girard's theorem (reuse from validation code)
    const double CosA = FMath::Clamp(FVector3d::DotProduct(V1, V2), -1.0, 1.0);
    const double CosB = FMath::Clamp(FVector3d::DotProduct(V2, V0), -1.0, 1.0);
    const double CosC = FMath::Clamp(FVector3d::DotProduct(V0, V1), -1.0, 1.0);

    const double a = FMath::Acos(CosA);
    const double b = FMath::Acos(CosB);
    const double c = FMath::Acos(CosC);

    // Skip degenerate triangles
    if (a < SMALL_NUMBER || b < SMALL_NUMBER || c < SMALL_NUMBER)
        return 0.0;

    // Spherical law of cosines to compute angles
    const double CosAlpha = (FMath::Cos(a) - FMath::Cos(b) * FMath::Cos(c)) / (FMath::Sin(b) * FMath::Sin(c));
    const double CosBeta = (FMath::Cos(b) - FMath::Cos(c) * FMath::Cos(a)) / (FMath::Sin(c) * FMath::Sin(a));
    const double CosGamma = (FMath::Cos(c) - FMath::Cos(a) * FMath::Cos(b)) / (FMath::Sin(a) * FMath::Sin(b));

    const double Alpha = FMath::Acos(FMath::Clamp(CosAlpha, -1.0, 1.0));
    const double Beta = FMath::Acos(FMath::Clamp(CosBeta, -1.0, 1.0));
    const double Gamma = FMath::Acos(FMath::Clamp(CosGamma, -1.0, 1.0));

    // Spherical excess (Girard's theorem): Area = α + β + γ - π
    const double SphericalExcess = Alpha + Beta + Gamma - PI;

    return SphericalExcess; // Area in steradians
}

// Milestone 4 Task 1.3: Update boundary lifecycle states
void UTectonicSimulationService::UpdateBoundaryStates(double DeltaTimeMy)
{
    // State transition rules (paper Section 4.1):
    // - Nascent → Active: velocity > threshold for sustained duration
    // - Active → Dormant: velocity drops below threshold
    // - Dormant → Active: velocity rises above threshold again

    constexpr double ActiveVelocityThreshold = 0.02; // rad/My (~1-2 cm/yr)
    constexpr double ActiveDurationThreshold = 10.0; // My (sustained activity required)

    for (auto& BoundaryPair : Boundaries)
    {
        FPlateBoundary& Boundary = BoundaryPair.Value;

        // Track divergent/convergent duration
        if (Boundary.BoundaryType == EBoundaryType::Divergent)
        {
            Boundary.DivergentDurationMy += DeltaTimeMy;
            Boundary.ConvergentDurationMy = 0.0; // Reset other counter
        }
        else if (Boundary.BoundaryType == EBoundaryType::Convergent)
        {
            Boundary.ConvergentDurationMy += DeltaTimeMy;
            Boundary.DivergentDurationMy = 0.0; // Reset other counter
        }
        else // Transform
        {
            Boundary.DivergentDurationMy = 0.0;
            Boundary.ConvergentDurationMy = 0.0;
        }

        // State transitions
        EBoundaryState OldState = Boundary.BoundaryState;

        switch (Boundary.BoundaryState)
        {
        case EBoundaryState::Nascent:
            // Nascent → Active: sustained high velocity
            if (Boundary.RelativeVelocity > ActiveVelocityThreshold &&
                (Boundary.DivergentDurationMy > ActiveDurationThreshold || Boundary.ConvergentDurationMy > ActiveDurationThreshold))
            {
                Boundary.BoundaryState = EBoundaryState::Active;
                Boundary.StateTransitionTimeMy = CurrentTimeMy;
            }
            break;

        case EBoundaryState::Active:
            // Active → Dormant: velocity drops
            if (Boundary.RelativeVelocity < ActiveVelocityThreshold)
            {
                Boundary.BoundaryState = EBoundaryState::Dormant;
                Boundary.StateTransitionTimeMy = CurrentTimeMy;
            }
            break;

        case EBoundaryState::Dormant:
            // Dormant → Active: velocity rises again
            if (Boundary.RelativeVelocity > ActiveVelocityThreshold)
            {
                Boundary.BoundaryState = EBoundaryState::Active;
                Boundary.StateTransitionTimeMy = CurrentTimeMy;
            }
            break;
        }

        // Log state transitions
        if (Boundary.BoundaryState != OldState)
        {
            UE_LOG(LogPlanetaryCreation, Verbose, TEXT("[Boundary State] Plate %d <-> %d: %s → %s at %.2f My (velocity=%.4f rad/My)"),
                BoundaryPair.Key.Key, BoundaryPair.Key.Value,
                OldState == EBoundaryState::Nascent ? TEXT("Nascent") : (OldState == EBoundaryState::Active ? TEXT("Active") : TEXT("Dormant")),
                Boundary.BoundaryState == EBoundaryState::Nascent ? TEXT("Nascent") : (Boundary.BoundaryState == EBoundaryState::Active ? TEXT("Active") : TEXT("Dormant")),
                CurrentTimeMy, Boundary.RelativeVelocity);
        }
    }
}
