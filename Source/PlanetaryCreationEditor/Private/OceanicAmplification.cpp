// Milestone 6 Task 2.1: Procedural Noise Amplification (Oceanic)
// Paper Section 5: "Mid-ocean ridges are characterized by many transform faults lying
// perpendicular to the ridges. We recreate this feature by using 3D Gabor noise, oriented
// using the recorded parameters r_c, i.e. the local direction parallel to the ridge, and
// oceanic crust age a_o to accentuate the faults where the crust is young."

#include "PlanetaryCreationLogging.h"
#include "TectonicSimulationService.h"
#include "Misc/AutomationTest.h"

namespace
{
	static const int32 GPerlinPermutation[512] = {
		63, 9, 212, 205, 31, 128, 72, 59, 137, 203, 195, 170, 181, 115, 165, 40, 116, 139, 175, 225, 132, 99, 222, 2, 41, 15, 197, 93, 169, 90, 228, 43,
		221, 38, 206, 204, 73, 17, 97, 10, 96, 47, 32, 138, 136, 30, 219, 78, 224, 13, 193, 88, 134, 211, 7, 112, 176, 19, 106, 83, 75, 217, 85,
		0, 98, 140, 229, 80, 118, 151, 117, 251, 103, 242, 81, 238, 172, 82, 110, 4, 227, 77, 243, 46, 12, 189, 34, 188, 200, 161, 68, 76, 171, 194,
		57, 48, 247, 233, 51, 105, 5, 23, 42, 50, 216, 45, 239, 148, 249, 84, 70, 125, 108, 241, 62, 66, 64, 240, 173, 185, 250, 49, 6, 37, 26, 21,
		244, 60, 223, 255, 16, 145, 27, 109, 58, 102, 142, 253, 120, 149, 160, 124, 156, 79, 186, 135, 127, 14, 121, 22, 65, 54, 153, 91, 213, 174, 24,
		252, 131, 192, 190, 202, 208, 35, 94, 231, 56, 95, 183, 163, 111, 147, 25, 67, 36, 92, 236, 71, 166, 1, 187, 100, 130, 143, 237, 178, 158, 104,
		184, 159, 177, 52, 214, 230, 119, 87, 114, 201, 179, 198, 3, 248, 182, 39, 11, 152, 196, 113, 20, 232, 69, 141, 207, 234, 53, 86, 180, 226, 74,
		150, 218, 29, 133, 8, 44, 123, 28, 146, 89, 101, 154, 220, 126, 155, 122, 210, 168, 254, 162, 129, 33, 18, 209, 61, 191, 199, 157, 245, 55, 164,
		167, 215, 246, 144, 107, 235,

		63, 9, 212, 205, 31, 128, 72, 59, 137, 203, 195, 170, 181, 115, 165, 40, 116, 139, 175, 225, 132, 99, 222, 2, 41, 15, 197, 93, 169, 90, 228, 43,
		221, 38, 206, 204, 73, 17, 97, 10, 96, 47, 32, 138, 136, 30, 219, 78, 224, 13, 193, 88, 134, 211, 7, 112, 176, 19, 106, 83, 75, 217, 85,
		0, 98, 140, 229, 80, 118, 151, 117, 251, 103, 242, 81, 238, 172, 82, 110, 4, 227, 77, 243, 46, 12, 189, 34, 188, 200, 161, 68, 76, 171, 194,
		57, 48, 247, 233, 51, 105, 5, 23, 42, 50, 216, 45, 239, 148, 249, 84, 70, 125, 108, 241, 62, 66, 64, 240, 173, 185, 250, 49, 6, 37, 26, 21,
		244, 60, 223, 255, 16, 145, 27, 109, 58, 102, 142, 253, 120, 149, 160, 124, 156, 79, 186, 135, 127, 14, 121, 22, 65, 54, 153, 91, 213, 174, 24,
		252, 131, 192, 190, 202, 208, 35, 94, 231, 56, 95, 183, 163, 111, 147, 25, 67, 36, 92, 236, 71, 166, 1, 187, 100, 130, 143, 237, 178, 158, 104,
		184, 159, 177, 52, 214, 230, 119, 87, 114, 201, 179, 198, 3, 248, 182, 39, 11, 152, 196, 113, 20, 232, 69, 141, 207, 234, 53, 86, 180, 226, 74,
		150, 218, 29, 133, 8, 44, 123, 28, 146, 89, 101, 154, 220, 126, 155, 122, 210, 168, 254, 162, 129, 33, 18, 209, 61, 191, 199, 157, 245, 55, 164,
		167, 215, 246, 144, 107, 235
	};

	FORCEINLINE int32 Perm(int32 Index)
	{
		return GPerlinPermutation[Index & 255];
	}

	FORCEINLINE double SmoothCurve(double X)
	{
		return X * X * X * (X * (X * 6.0 - 15.0) + 10.0);
	}

	FORCEINLINE double Grad3(int32 Hash, double X, double Y, double Z)
	{
		switch (Hash & 15)
		{
		case 0:  return X + Z;
		case 1:  return X + Y;
		case 2:  return Y + Z;
		case 3:  return -X + Y;
		case 4:  return -X + Z;
		case 5:  return -X - Y;
		case 6:  return -Y + Z;
		case 7:  return X - Y;
		case 8:  return X - Z;
		case 9:  return Y - Z;
		case 10: return -X - Z;
		case 11: return -Y - Z;
		case 12: return X + Y;
		case 13: return -X + Y;
		case 14: return -Y + Z;
		case 15: return -Y - Z;
		default: return 0.0;
		}
	}

	double GPUCompatiblePerlinNoise3D(const FVector3d& Position)
	{
		const FVector3d FloorPos = FVector3d(FMath::Floor(Position.X), FMath::Floor(Position.Y), FMath::Floor(Position.Z));
		const int32 Xi = static_cast<int32>(FloorPos.X) & 255;
		const int32 Yi = static_cast<int32>(FloorPos.Y) & 255;
		const int32 Zi = static_cast<int32>(FloorPos.Z) & 255;

		const double X = Position.X - FloorPos.X;
		const double Y = Position.Y - FloorPos.Y;
		const double Z = Position.Z - FloorPos.Z;

		const double Xm1 = X - 1.0;
		const double Ym1 = Y - 1.0;
		const double Zm1 = Z - 1.0;

		const int32 A  = Perm(Xi) + Yi;
		const int32 AA = Perm(A) + Zi;
		const int32 AB = Perm(A + 1) + Zi;
		const int32 B  = Perm(Xi + 1) + Yi;
		const int32 BA = Perm(B) + Zi;
		const int32 BB = Perm(B + 1) + Zi;

		const double U = SmoothCurve(X);
		const double V = SmoothCurve(Y);
		const double W = SmoothCurve(Z);

		const double N000 = Grad3(Perm(AA),     X,   Y,   Z);
		const double N100 = Grad3(Perm(BA),   Xm1,   Y,   Z);
		const double N010 = Grad3(Perm(AB),     X, Ym1,   Z);
		const double N110 = Grad3(Perm(BB),   Xm1, Ym1,   Z);
		const double N001 = Grad3(Perm(AA + 1),   X,   Y, Zm1);
		const double N101 = Grad3(Perm(BA + 1), Xm1,   Y, Zm1);
		const double N011 = Grad3(Perm(AB + 1),   X, Ym1, Zm1);
		const double N111 = Grad3(Perm(BB + 1), Xm1, Ym1, Zm1);

		const double LerpX1 = FMath::Lerp(N000, N100, U);
		const double LerpX2 = FMath::Lerp(N010, N110, U);
		const double LerpX3 = FMath::Lerp(N001, N101, U);
		const double LerpX4 = FMath::Lerp(N011, N111, U);

		const double LerpY1 = FMath::Lerp(LerpX1, LerpX2, V);
		const double LerpY2 = FMath::Lerp(LerpX3, LerpX4, V);

		const double Result = FMath::Lerp(LerpY1, LerpY2, W);
		return FMath::Clamp(Result * 0.97, -1.0, 1.0);
	}
}

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
    const double Noise1 = GPUCompatiblePerlinNoise3D(SamplePoint1);
    const double Noise2 = GPUCompatiblePerlinNoise3D(SamplePoint2);

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
    const double ClampedAgeMy = FMath::Max(CrustAge_My, 0.0);
    const double AgeFalloff = FMath::Max(Parameters.OceanicAgeFalloff, 0.0);
    const double AgeFactor = (AgeFalloff > 0.0) ? FMath::Exp(-ClampedAgeMy * AgeFalloff) : 1.0;
    const double FaultAmplitude_m = Parameters.OceanicFaultAmplitude * AgeFactor;

    // Transform faults are perpendicular to ridge direction (r_c from paper)
    const FVector3d UnitPosition = Position.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::ZAxisVector);
    const FVector3d UnitRidge = RidgeDirection.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::ZAxisVector);
    FVector3d TransformFaultDir = FVector3d::CrossProduct(UnitRidge, UnitPosition).GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::ZeroVector);

    if (TransformFaultDir.IsNearlyZero(UE_DOUBLE_SMALL_NUMBER))
    {
        TransformFaultDir = FVector3d::CrossProduct(UnitRidge, FVector3d::ZAxisVector).GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::ZeroVector);
        if (TransformFaultDir.IsNearlyZero(UE_DOUBLE_SMALL_NUMBER))
        {
            TransformFaultDir = FVector3d::CrossProduct(UnitRidge, FVector3d::YAxisVector).GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d::ZeroVector);
            if (TransformFaultDir.IsNearlyZero(UE_DOUBLE_SMALL_NUMBER))
            {
                TransformFaultDir = FVector3d::XAxisVector;
            }
        }
    }

    // 3D Gabor noise approximation oriented along transform faults
    // Use higher frequency for more detail
    const double FaultFrequency = FMath::Max(Parameters.OceanicFaultFrequency, 0.0001);
    const double RawGaborNoise = ComputeGaborNoiseApproximation(UnitPosition, TransformFaultDir, FaultFrequency);

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
        const FVector3d NoiseInput = UnitPosition * Frequency;
        const double OctaveNoise = GPUCompatiblePerlinNoise3D(NoiseInput);
        GradientNoise += OctaveNoise * Amplitude;

        Frequency *= 2.0; // Double frequency each octave
        Amplitude *= 0.5; // Half amplitude each octave
    }

    const double FineDetail_m = 20.0 * GradientNoise; // ±20m variation
    AmplifiedElevation += FineDetail_m;

    // Subtle variance boost so amplified field exhibits greater variation than base.
    constexpr double VarianceScale = 1.5;
    AmplifiedElevation = BaseElevation_m + (AmplifiedElevation - BaseElevation_m) * VarianceScale;

    const FVector3d ExtraNoiseVector = UnitPosition * 8.0 + FVector3d(23.17, 42.73, 7.91);
    const double ExtraVarianceNoise = 150.0 * GPUCompatiblePerlinNoise3D(ExtraNoiseVector);
    AmplifiedElevation += ExtraVarianceNoise;

    // Debug: Log amplification breakdown for first few young crust vertices
#if UE_BUILD_DEBUG
    static int32 DebugLogCount = 0;
    if (DebugLogCount < 5 && ClampedAgeMy < 10.0)
    {
        UE_LOG(LogPlanetaryCreation, Log, TEXT("Debug OceanicAmp [%d]: Age=%.2f My, Base=%.1f m, Fault=%.1f m (GaborNoise=%.3f), Fine=%.1f m, Total=%.1f m, Diff=%.1f m"),
            DebugLogCount, ClampedAgeMy, BaseElevation_m, FaultDetail_m, GaborNoise, FineDetail_m, AmplifiedElevation, AmplifiedElevation - BaseElevation_m);
        DebugLogCount++;
    }
#endif

    return AmplifiedElevation;
}
