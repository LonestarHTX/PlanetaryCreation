// Milestone 4 Task 2.2: Rift Propagation Model (Paper Section 4.2)

#include "TectonicSimulationService.h"

void UTectonicSimulationService::UpdateRiftProgression(double DeltaTimeMy)
{
    if (!Parameters.bEnableRiftPropagation)
        return;

    for (auto& BoundaryPair : Boundaries)
    {
        FPlateBoundary& Boundary = BoundaryPair.Value;

        // Only process divergent boundaries
        if (Boundary.BoundaryType != EBoundaryType::Divergent)
        {
            // Reset rift parameters for non-divergent boundaries
            if (Boundary.BoundaryState == EBoundaryState::Rifting)
            {
                Boundary.BoundaryState = EBoundaryState::Nascent;
                Boundary.RiftWidthMeters = 0.0;
                Boundary.RiftFormationTimeMy = 0.0;
            }
            continue;
        }

        // Check if boundary should enter rifting state
        // Criteria: Divergent + sustained high velocity (from split threshold)
        if (Boundary.BoundaryState != EBoundaryState::Rifting)
        {
            // Transition to rifting if divergent velocity sustained above threshold
            if (Boundary.RelativeVelocity > Parameters.SplitVelocityThreshold &&
                Boundary.DivergentDurationMy > Parameters.SplitDurationThreshold * 0.5) // Trigger rift earlier than split
            {
                Boundary.BoundaryState = EBoundaryState::Rifting;
                Boundary.RiftFormationTimeMy = CurrentTimeMy;
                Boundary.StateTransitionTimeMy = CurrentTimeMy;

                UE_LOG(LogTemp, Log, TEXT("[Rift] Boundary [%d-%d] entered rifting state at %.2f My (velocity=%.4f rad/My)"),
                    BoundaryPair.Key.Key, BoundaryPair.Key.Value, CurrentTimeMy, Boundary.RelativeVelocity);
            }
        }

        // Update rift width for active rifts
        if (Boundary.BoundaryState == EBoundaryState::Rifting)
        {
            // Rift widening: width increases based on divergent velocity
            // Formula: Δwidth = RiftProgressionRate * RelativeVelocity * ΔTime
            // Units: meters = (m/My)/(rad/My) * (rad/My) * My = m
            const double WidthIncrement = Parameters.RiftProgressionRate * Boundary.RelativeVelocity * DeltaTimeMy;
            Boundary.RiftWidthMeters += WidthIncrement;

            const double RiftAgeMy = CurrentTimeMy - Boundary.RiftFormationTimeMy;

            UE_LOG(LogTemp, Verbose, TEXT("[Rift] Boundary [%d-%d]: width=%.0f m, age=%.2f My, velocity=%.4f rad/My"),
                BoundaryPair.Key.Key, BoundaryPair.Key.Value, Boundary.RiftWidthMeters, RiftAgeMy, Boundary.RelativeVelocity);

            // Check if rift has reached split threshold
            if (Boundary.RiftWidthMeters > Parameters.RiftSplitThresholdMeters)
            {
                UE_LOG(LogTemp, Log, TEXT("[Rift] Boundary [%d-%d] exceeded split threshold (width=%.0f m > %.0f m) at %.2f My"),
                    BoundaryPair.Key.Key, BoundaryPair.Key.Value, Boundary.RiftWidthMeters, Parameters.RiftSplitThresholdMeters, CurrentTimeMy);

                // Note: Actual split will be triggered by DetectAndExecutePlateSplits()
                // This just tracks the rift maturity for visualization/analytics
            }

            // Transition back to Active if velocity drops below threshold (rift dormancy)
            if (Boundary.RelativeVelocity < Parameters.SplitVelocityThreshold * 0.5) // Hysteresis
            {
                UE_LOG(LogTemp, Log, TEXT("[Rift] Boundary [%d-%d] became dormant (velocity dropped to %.4f rad/My)"),
                    BoundaryPair.Key.Key, BoundaryPair.Key.Value, Boundary.RelativeVelocity);

                Boundary.BoundaryState = EBoundaryState::Active;
                // Preserve rift width for potential resumption
            }
        }
    }
}
