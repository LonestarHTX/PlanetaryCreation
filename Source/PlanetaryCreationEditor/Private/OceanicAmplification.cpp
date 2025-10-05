// Milestone 6 Task 2.1: Procedural Noise Amplification (Oceanic)
// Paper Section 5: "Mid-ocean ridges are characterized by many transform faults lying
// perpendicular to the ridges. We recreate this feature by using 3D Gabor noise, oriented
// using the recorded parameters r_c, i.e. the local direction parallel to the ridge, and
// oceanic crust age a_o to accentuate the faults where the crust is young."

#include "PlanetaryCreationLogging.h"
#include "TectonicSimulationService.h"
#include "Misc/AutomationTest.h"

/**
 * Milestone 6 Task 2.1: 3D Gabor Noise Approximation
 *
 * True Gabor noise is computationally expensive (requires summing many Gabor kernels).
 * We approximate it using directional Perlin noise to create transform fault patterns.
 *
 * Gabor noise properties we need to replicate:
 * - Anisotropic (oriented along a specific direction)
 * - Band-limited frequency content
 * - Sharp linear features (faults)
 *
 * Our approximation:
 * - Multiple octaves of Perlin noise sampled along fault direction
 * - Sharpened using power function to create fault-like ridges
 * - Oriented perpendicular to ridge direction
 */
double ComputeGaborNoiseApproximation(const FVector3d& Position, const FVector3d& FaultDirection, double Frequency)
{
    // Sample Perlin noise along fault direction (creates anisotropy)
    // We sample at offset points along the fault to create linear patterns
    const FVector3d SamplePoint1 = Position * Frequency;
    const FVector3d SamplePoint2 = (Position + FaultDirection * 2.0) * Frequency;

    // Convert to FVector for Unreal's Perlin function
    const float Noise1 = FMath::PerlinNoise3D(FVector(SamplePoint1));
    const float Noise2 = FMath::PerlinNoise3D(FVector(SamplePoint2));

    // Take maximum absolute value to create strong linear features
    // (averaging reduces amplitude too much)
    const double NoiseValue = FMath::Abs(Noise1) > FMath::Abs(Noise2) ? Noise1 : Noise2;

    // Amplify and sharpen to create fault-like linear features
    // Paper mentions "transform faults" which are sharp discontinuities
    // Power < 1.0 enhances contrast (makes peaks sharper, valleys deeper)
    const double SharpNoise = FMath::Sign(NoiseValue) * FMath::Pow(FMath::Abs(NoiseValue), 0.6);

    return SharpNoise; // Range approximately [-1, 1]
}

/**
 * Milestone 6 Task 2.1: Compute ridge direction from nearby divergent boundaries.
 *
 * For each oceanic vertex, find the nearest divergent boundary and compute the
 * direction parallel to the ridge (r_c in paper notation).
 *
 * Ridge direction = tangent to boundary at closest point
 * Transform fault direction = perpendicular to ridge direction
 */
FVector3d ComputeRidgeDirection(
    const FVector3d& VertexPosition,
    int32 PlateID,
    const TArray<FTectonicPlate>& Plates,
    const TMap<TPair<int32, int32>, FPlateBoundary>& Boundaries)
{
    // Find nearest divergent boundary involving this plate
    FVector3d NearestBoundaryDir = FVector3d::ZAxisVector; // Default fallback
    double MinDistance = TNumericLimits<double>::Max();

    for (const auto& BoundaryPair : Boundaries)
    {
        const TPair<int32, int32>& BoundaryKey = BoundaryPair.Key;
        const FPlateBoundary& Boundary = BoundaryPair.Value;

        // Only consider divergent boundaries involving this plate
        if (Boundary.BoundaryType != EBoundaryType::Divergent)
            continue;

        if (BoundaryKey.Key != PlateID && BoundaryKey.Value != PlateID)
            continue;

        // Boundary is defined by shared edge vertices
        if (Boundary.SharedEdgeVertices.Num() < 2)
            continue;

        // For icosphere, boundaries are great circle arcs between two vertices
        // Compute distance from vertex to this boundary edge
        for (int32 i = 0; i < Boundary.SharedEdgeVertices.Num() - 1; ++i)
        {
            const int32 EdgeV0 = Boundary.SharedEdgeVertices[i];
            const int32 EdgeV1 = Boundary.SharedEdgeVertices[i + 1];

            // Get edge midpoint as boundary representative
            const FVector3d EdgeMidpoint = (Plates[0].VertexIndices.IsValidIndex(EdgeV0) &&
                                           Plates[0].VertexIndices.IsValidIndex(EdgeV1))
                ? FVector3d::ZeroVector // Placeholder - would need access to SharedVertices
                : FVector3d::ZAxisVector;

            // Distance check (simplified for now)
            const double Distance = FVector3d::Distance(VertexPosition, EdgeMidpoint);

            if (Distance < MinDistance)
            {
                MinDistance = Distance;

                // Ridge direction = tangent to great circle at boundary
                // For sphere, tangent is perpendicular to radius and to edge direction
                const FVector3d EdgeDir = (EdgeMidpoint - VertexPosition).GetSafeNormal();
                const FVector3d RadialDir = VertexPosition.GetSafeNormal();
                NearestBoundaryDir = FVector3d::CrossProduct(RadialDir, EdgeDir).GetSafeNormal();
            }
        }
    }

    return NearestBoundaryDir;
}

/**
 * Milestone 6 Task 2.1: Compute oceanic amplification for a single vertex.
 *
 * Paper Section 5 formula:
 * - Base elevation from coarse simulation (M5 erosion/subsidence)
 * - Transform fault detail from Gabor noise (age-modulated)
 * - High-frequency detail from gradient noise
 *
 * Total = BaseElevation + FaultDetail + FineDetail
 */
double ComputeOceanicAmplification(
    const FVector3d& Position,
    int32 PlateID,
    double CrustAge_My,
    double BaseElevation_m,
    const FVector3d& RidgeDirection,
    const TArray<FTectonicPlate>& Plates,
    const TMap<TPair<int32, int32>, FPlateBoundary>& Boundaries)
{
    // Start with base elevation from M5 system (erosion, subsidence)
    double AmplifiedElevation = BaseElevation_m;

    // Only amplify oceanic crust (continental amplification is Task 2.2)
    bool bIsOceanic = false;
    for (const FTectonicPlate& Plate : Plates)
    {
        if (Plate.PlateID == PlateID)
        {
            bIsOceanic = (Plate.CrustType == ECrustType::Oceanic);

            // Debug: Log first few continental vertices to diagnose issue
            static int32 DebugContinentalLog = 0;
            if (DebugContinentalLog < 3 && !bIsOceanic)
            {
                UE_LOG(LogPlanetaryCreation, Log, TEXT("Debug: Continental vertex with PlateID=%d, returning BaseElevation=%.3f m"),
                    PlateID, BaseElevation_m);
                DebugContinentalLog++;
            }
            break;
        }
    }

    if (!bIsOceanic)
        return AmplifiedElevation; // Skip continental vertices

    // ============================================================================
    // TRANSFORM FAULT DETAIL (Gabor noise, age-modulated)
    // ============================================================================

    // Paper: "oceanic crust age a_o to accentuate the faults where the crust is young"
    // Age-based amplitude decay: young crust has strong faults, old crust smooths out
    const double AgeFactor = FMath::Exp(-CrustAge_My / 50.0); // τ = 50 My decay constant
    const double FaultAmplitude_m = 150.0 * AgeFactor; // 150m max for young ridges

    // Transform faults are perpendicular to ridge direction (r_c from paper)
    const FVector3d TransformFaultDir = FVector3d::CrossProduct(RidgeDirection, Position.GetSafeNormal()).GetSafeNormal();

    // 3D Gabor noise approximation oriented along transform faults
    // Use higher frequency for more detail
    const double RawGaborNoise = ComputeGaborNoiseApproximation(Position, TransformFaultDir, 0.05);

    // Scale up to ensure full [-1, 1] range (Perlin typically gives smaller values)
    // This ensures fault amplitudes reach the target 150m for young crust
    const double GaborNoise = FMath::Clamp(RawGaborNoise * 3.0, -1.0, 1.0);

    const double FaultDetail_m = FaultAmplitude_m * GaborNoise;

    AmplifiedElevation += FaultDetail_m;

    // ============================================================================
    // HIGH-FREQUENCY GRADIENT NOISE (fine underwater detail)
    // ============================================================================

    // Paper Section 5: Additional high-frequency detail for close-up views
    // Multi-octave Perlin noise for natural-looking seafloor texture
    double GradientNoise = 0.0;
    const int32 Octaves = 4;
    const double BaseFrequency = 0.1;
    double Amplitude = 1.0;
    double Frequency = BaseFrequency;

    for (int32 Octave = 0; Octave < Octaves; ++Octave)
    {
        const FVector NoiseInput = FVector(Position * Frequency);
        const float OctaveNoise = FMath::PerlinNoise3D(NoiseInput);
        GradientNoise += OctaveNoise * Amplitude;

        Frequency *= 2.0; // Double frequency each octave
        Amplitude *= 0.5; // Half amplitude each octave
    }

    const double FineDetail_m = 20.0 * GradientNoise; // ±20m variation
    AmplifiedElevation += FineDetail_m;

    // Debug: Log amplification breakdown for first few young crust vertices
    static int32 DebugLogCount = 0;
    if (DebugLogCount < 5 && CrustAge_My < 10.0)
    {
        UE_LOG(LogPlanetaryCreation, Log, TEXT("Debug OceanicAmp [%d]: Age=%.2f My, Base=%.1f m, Fault=%.1f m (GaborNoise=%.3f), Fine=%.1f m, Total=%.1f m, Diff=%.1f m"),
            DebugLogCount, CrustAge_My, BaseElevation_m, FaultDetail_m, GaborNoise, FineDetail_m, AmplifiedElevation, AmplifiedElevation - BaseElevation_m);
        DebugLogCount++;
    }

    return AmplifiedElevation;
}
