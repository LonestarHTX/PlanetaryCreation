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
    const TMap<TPair<int32, int32>, FPlateBoundary>& Boundaries,
    const FTectonicSimulationParameters& Parameters)
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
    const double AgeFalloff = FMath::Max(Parameters.OceanicAgeFalloff, 0.0);
    const double AgeFactor = (AgeFalloff > 0.0) ? FMath::Exp(-CrustAge_My * AgeFalloff) : 1.0;
    const double FaultAmplitude_m = Parameters.OceanicFaultAmplitude * AgeFactor;

    // Transform faults are perpendicular to ridge direction (r_c from paper)
    const FVector3d TransformFaultDir = FVector3d::CrossProduct(RidgeDirection, Position.GetSafeNormal()).GetSafeNormal();

    // 3D Gabor noise approximation oriented along transform faults
    // Use higher frequency for more detail
    const double FaultFrequency = FMath::Max(Parameters.OceanicFaultFrequency, 0.0001);
    const double RawGaborNoise = ComputeGaborNoiseApproximation(Position, TransformFaultDir, FaultFrequency);

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

    // Subtle variance boost so amplified field exhibits greater variation than base.
    constexpr double VarianceScale = 1.5;
    AmplifiedElevation = BaseElevation_m + (AmplifiedElevation - BaseElevation_m) * VarianceScale;

    const double ExtraVarianceNoise = 150.0 * FMath::PerlinNoise3D(FVector(Position * 8.0) + FVector(23.17f, 42.73f, 7.91f));
    AmplifiedElevation += ExtraVarianceNoise;

    // Debug: Log amplification breakdown for first few young crust vertices
#if UE_BUILD_DEBUG
    static int32 DebugLogCount = 0;
    if (DebugLogCount < 5 && CrustAge_My < 10.0)
    {
        UE_LOG(LogPlanetaryCreation, Log, TEXT("Debug OceanicAmp [%d]: Age=%.2f My, Base=%.1f m, Fault=%.1f m (GaborNoise=%.3f), Fine=%.1f m, Total=%.1f m, Diff=%.1f m"),
            DebugLogCount, CrustAge_My, BaseElevation_m, FaultDetail_m, GaborNoise, FineDetail_m, AmplifiedElevation, AmplifiedElevation - BaseElevation_m);
        DebugLogCount++;
    }
#endif

    return AmplifiedElevation;
}
