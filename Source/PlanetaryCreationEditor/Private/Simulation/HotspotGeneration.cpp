// Milestone 4 Task 2.1: Hotspot Generation & Drift (Paper Section 4.4)

#include "Utilities/PlanetaryCreationLogging.h"
#include "Simulation/TectonicSimulationService.h"
#include "Math/RandomStream.h"

void UTectonicSimulationService::GenerateHotspots()
{
    if (!Parameters.bEnableHotspots)
    {
        Hotspots.Empty();
        return;
    }

    Hotspots.Empty();

    FRandomStream RNG(Parameters.Seed + 1000); // Offset seed to avoid collision with plate generation

    UE_LOG(LogPlanetaryCreation, Log, TEXT("[Hotspots] Generating %d major + %d minor hotspots (seed=%d)"),
        Parameters.MajorHotspotCount, Parameters.MinorHotspotCount, Parameters.Seed);

    // Generate major hotspots (large, long-lived plumes)
    for (int32 i = 0; i < Parameters.MajorHotspotCount; ++i)
    {
        FMantleHotspot Hotspot;
        Hotspot.HotspotID = Hotspots.Num();
        Hotspot.Type = EHotspotType::Major;

        // Generate random position on unit sphere using uniform spherical distribution
        const double Theta = RNG.FRand() * 2.0 * PI;
        const double Phi = FMath::Acos(2.0 * RNG.FRand() - 1.0);

        Hotspot.Position = FVector3d(
            FMath::Sin(Phi) * FMath::Cos(Theta),
            FMath::Sin(Phi) * FMath::Sin(Theta),
            FMath::Cos(Phi)
        ).GetSafeNormal();

        Hotspot.ThermalOutput = Parameters.MajorHotspotThermalOutput;
        Hotspot.InfluenceRadius = 0.15; // ~8.6° for major hotspots (wider influence)

        // Generate random drift direction (independent of plate motion)
        const double DriftTheta = RNG.FRand() * 2.0 * PI;
        const double DriftPhi = FMath::Acos(2.0 * RNG.FRand() - 1.0);

        Hotspot.DriftVelocity = FVector3d(
            FMath::Sin(DriftPhi) * FMath::Cos(DriftTheta),
            FMath::Sin(DriftPhi) * FMath::Sin(DriftTheta),
            FMath::Cos(DriftPhi)
        ).GetSafeNormal() * Parameters.HotspotDriftSpeed;

        Hotspots.Add(Hotspot);

        UE_LOG(LogPlanetaryCreation, Verbose, TEXT("  Major hotspot %d: pos=(%.3f, %.3f, %.3f), output=%.2f, radius=%.3f rad"),
            Hotspot.HotspotID, Hotspot.Position.X, Hotspot.Position.Y, Hotspot.Position.Z,
            Hotspot.ThermalOutput, Hotspot.InfluenceRadius);
    }

    // Generate minor hotspots (smaller, shorter-lived plumes)
    for (int32 i = 0; i < Parameters.MinorHotspotCount; ++i)
    {
        FMantleHotspot Hotspot;
        Hotspot.HotspotID = Hotspots.Num();
        Hotspot.Type = EHotspotType::Minor;

        // Generate random position on unit sphere
        const double Theta = RNG.FRand() * 2.0 * PI;
        const double Phi = FMath::Acos(2.0 * RNG.FRand() - 1.0);

        Hotspot.Position = FVector3d(
            FMath::Sin(Phi) * FMath::Cos(Theta),
            FMath::Sin(Phi) * FMath::Sin(Theta),
            FMath::Cos(Phi)
        ).GetSafeNormal();

        Hotspot.ThermalOutput = Parameters.MinorHotspotThermalOutput;
        Hotspot.InfluenceRadius = 0.1; // ~5.7° for minor hotspots

        // Generate random drift direction
        const double DriftTheta = RNG.FRand() * 2.0 * PI;
        const double DriftPhi = FMath::Acos(2.0 * RNG.FRand() - 1.0);

        Hotspot.DriftVelocity = FVector3d(
            FMath::Sin(DriftPhi) * FMath::Cos(DriftTheta),
            FMath::Sin(DriftPhi) * FMath::Sin(DriftTheta),
            FMath::Cos(DriftPhi)
        ).GetSafeNormal() * Parameters.HotspotDriftSpeed;

        Hotspots.Add(Hotspot);

        UE_LOG(LogPlanetaryCreation, Verbose, TEXT("  Minor hotspot %d: pos=(%.3f, %.3f, %.3f), output=%.2f, radius=%.3f rad"),
            Hotspot.HotspotID, Hotspot.Position.X, Hotspot.Position.Y, Hotspot.Position.Z,
            Hotspot.ThermalOutput, Hotspot.InfluenceRadius);
    }

    UE_LOG(LogPlanetaryCreation, Log, TEXT("[Hotspots] Generated %d total hotspots"), Hotspots.Num());
}

void UTectonicSimulationService::UpdateHotspotDrift(double DeltaTimeMy)
{
    if (!Parameters.bEnableHotspots || Parameters.HotspotDriftSpeed <= 0.0)
        return;

    // Update hotspot positions in mantle reference frame
    for (FMantleHotspot& Hotspot : Hotspots)
    {
        // Drift along velocity vector (spherical surface tangent motion)
        // Compute rotation axis perpendicular to current position and drift direction
        const FVector3d RotationAxis = FVector3d::CrossProduct(Hotspot.Position, Hotspot.DriftVelocity).GetSafeNormal();
        const double RotationAngle = Hotspot.DriftVelocity.Length() * DeltaTimeMy;

        if (RotationAxis.IsNearlyZero() || FMath::IsNearlyZero(RotationAngle))
            continue; // No drift or degenerate case

        // Rodrigues' rotation formula: v' = v*cos(θ) + (k×v)*sin(θ) + k*(k·v)*(1-cos(θ))
        const double CosTheta = FMath::Cos(RotationAngle);
        const double SinTheta = FMath::Sin(RotationAngle);
        const double DotProduct = FVector3d::DotProduct(RotationAxis, Hotspot.Position);

        Hotspot.Position = (Hotspot.Position * CosTheta) +
                           (FVector3d::CrossProduct(RotationAxis, Hotspot.Position) * SinTheta) +
                           (RotationAxis * DotProduct * (1.0 - CosTheta));

        Hotspot.Position.Normalize(); // Ensure unit sphere constraint
    }
}

void UTectonicSimulationService::ApplyHotspotThermalContribution()
{
    if (!Parameters.bEnableHotspots || Hotspots.Num() == 0)
        return;

    // Milestone 4 Phase 5: Thermal-only hotspot model (paper-aligned)
    //
    // Per paper physics: Hotspots are thermal anomalies that elevate temperature and drive volcanism,
    // but do NOT directly add mechanical stress. Stress comes from plate interactions (subduction,
    // divergence). Hotspots affect stress indirectly through temperature-driven viscosity changes.
    //
    // Thermal field computation (ComputeThermalField()) already handles hotspot temperature contribution.
    // This function is retained for potential future thermal softening effects (stress modulation by temp),
    // but currently does not add stress directly.
    //
    // NOTE: Original implementation added hotspot heat directly to stress field, creating artificial
    // coupling. Removed to align with paper's separation of thermal vs. mechanical effects.

    // Future enhancement: Apply thermal softening multiplier to existing stress
    // E.g., VertexStressValues[i] *= (1.0 - thermalSofteningFactor)
    // where thermalSofteningFactor depends on local temperature from ComputeThermalField()
}
