#pragma once

#include "CoreMinimal.h"

/** STG-06: Orogeny classification for continental vertices near convergent boundaries. */
UENUM()
enum class EOrogenyClass : uint8
{
    None = 0,      // No convergent boundary influence
    Nascent = 1,   // Within nascent proximity threshold
    Active = 2,    // Within active proximity threshold (closest)
    Dormant = 3    // Beyond nascent threshold but has valid fold direction
};

UENUM(BlueprintType)
enum class EStageBAmplificationReadyReason : uint8
{
    None UMETA(DisplayName = "Ready"),
    NoRenderMesh UMETA(DisplayName = "Render Mesh Unavailable"),
    PendingCPUAmplification UMETA(DisplayName = "CPU Amplification In Progress"),
    PendingGPUReadback UMETA(DisplayName = "GPU Readback Pending"),
    ParametersDirty UMETA(DisplayName = "Parameters Changed"),
    LODChange UMETA(DisplayName = "LOD Update In Progress"),
    ExternalReset UMETA(DisplayName = "Manual Reset"),
    AutomationHold UMETA(DisplayName = "Automation Hold"),
    GPUFailure UMETA(DisplayName = "GPU Failure")
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnStageBAmplificationReadyChanged, bool /*bReady*/, EStageBAmplificationReadyReason /*Reason*/);

namespace PlanetaryCreation
{
namespace StageB
{
    inline const TCHAR* GetReadyReasonLabel(EStageBAmplificationReadyReason Reason)
    {
        switch (Reason)
        {
        case EStageBAmplificationReadyReason::None:               return TEXT("None");
        case EStageBAmplificationReadyReason::NoRenderMesh:       return TEXT("NoRenderMesh");
        case EStageBAmplificationReadyReason::PendingCPUAmplification: return TEXT("PendingCPUAmplification");
        case EStageBAmplificationReadyReason::PendingGPUReadback: return TEXT("PendingGPUReadback");
        case EStageBAmplificationReadyReason::ParametersDirty:    return TEXT("ParametersDirty");
        case EStageBAmplificationReadyReason::LODChange:          return TEXT("LODChange");
        case EStageBAmplificationReadyReason::ExternalReset:      return TEXT("ExternalReset");
        case EStageBAmplificationReadyReason::AutomationHold:     return TEXT("AutomationHold");
        case EStageBAmplificationReadyReason::GPUFailure:         return TEXT("GPUFailure");
        default:                                                  return TEXT("Unknown");
        }
    }

    inline const TCHAR* GetReadyReasonDescription(EStageBAmplificationReadyReason Reason)
    {
        switch (Reason)
        {
        case EStageBAmplificationReadyReason::None:                    return TEXT("Stage B amplification ready.");
        case EStageBAmplificationReadyReason::NoRenderMesh:            return TEXT("Waiting for render mesh to initialize.");
        case EStageBAmplificationReadyReason::PendingCPUAmplification: return TEXT("Stage B CPU amplification still running.");
        case EStageBAmplificationReadyReason::PendingGPUReadback:      return TEXT("Stage B GPU readback pending; amplified data not yet available.");
        case EStageBAmplificationReadyReason::ParametersDirty:         return TEXT("Amplification parameters changed; awaiting rebuild.");
        case EStageBAmplificationReadyReason::LODChange:               return TEXT("Render LOD changed; Stage B rebuild in progress.");
        case EStageBAmplificationReadyReason::ExternalReset:           return TEXT("Stage B reset requested; rerun amplification to refresh detail.");
        case EStageBAmplificationReadyReason::AutomationHold:          return TEXT("Stage B temporarily disabled for automation.");
        case EStageBAmplificationReadyReason::GPUFailure:              return TEXT("Stage B GPU path failed; awaiting recovery.");
        default:                                                       return TEXT("Stage B readiness unknown.");
        }
    }

    /** Convert equirectangular UV coordinates (0–1 range) to a unit-length direction vector. */
    FORCEINLINE FVector3d DirectionFromEquirectUV(const FVector2d& UV, double PoleEpsilon = 0.0)
    {
        const double WrappedU = FMath::Frac(UV.X);
        const double ClampedV = FMath::Clamp(UV.Y, PoleEpsilon, 1.0 - PoleEpsilon);

        const double Longitude = (WrappedU - 0.5) * 2.0 * PI;
        const double Latitude = (0.5 - ClampedV) * PI;

        const double CosLat = FMath::Cos(Latitude);
        const double SinLat = FMath::Sin(Latitude);
        const double CosLon = FMath::Cos(Longitude);
        const double SinLon = FMath::Sin(Longitude);

        return FVector3d(
            CosLat * CosLon,
            CosLat * SinLon,
            SinLat).GetSafeNormal();
    }

    /** Convert a unit-length direction vector to the exporter’s equirectangular UV convention. */
    FORCEINLINE FVector2d EquirectUVFromDirection(const FVector3d& Direction)
    {
        const FVector3d Normalized = Direction.GetSafeNormal();
        const double Longitude = FMath::Atan2(Normalized.Y, Normalized.X);
        const double Latitude = FMath::Asin(FMath::Clamp(Normalized.Z, -1.0, 1.0));

        const double U = FMath::Frac(0.5 + Longitude / (2.0 * PI));
        double V = 0.5 - Latitude / PI;
        V = FMath::Clamp(V, 0.0, 1.0);

        return FVector2d(U, V);
    }

    struct FStageBRescueSummary
    {
        int32 ImageWidth = 0;
        int32 ImageHeight = 0;
        uint64 TotalPixels = 0;
        uint64 FinalHits = 0;
        uint64 FinalMisses = 0;

        uint64 FallbackAttempts = 0;
        uint64 FallbackSuccesses = 0;
        uint64 FallbackFailures = 0;

        uint64 ExpandedAttempts = 0;
        uint64 ExpandedSuccesses = 0;

        uint64 SanitizedFallbacks = 0;
        uint64 DirectNudgeFallbacks = 0;
        uint64 ExpandedFallbacks = 0;
        uint64 WrappedFallbacks = 0;
        uint64 HintFallbacks = 0;
        uint64 RowReuseFallbacks = 0;

        bool bStageBReadyAtStart = false;
        bool bStageBReadyAtFinish = false;
        bool bUsedAmplifiedData = false;
        bool bRescueAttempted = false;
        bool bRescueSucceeded = false;
        bool bUsedSnapshotFloatBuffer = false;

        EStageBAmplificationReadyReason ReadyReasonAtStart = EStageBAmplificationReadyReason::None;
        EStageBAmplificationReadyReason ReadyReasonAtFinish = EStageBAmplificationReadyReason::None;
    };

    struct FStageB_UnifiedParameters
    {
        float OceanicFaultAmplitude = 150.0f;
        float OceanicFaultFrequency = 0.05f;
        float OceanicAgeFalloff = 0.02f;
        float TransitionAgeMy = 10.0f;
        float ContinentalMinDetailScale = 0.5f;
        float ContinentalNormalizationEpsilon = 1.0e-3f;
        float OceanicVarianceScale = 1.5f;
        float ExtraVarianceAmplitude = 150.0f;

        // Anisotropy params (STG-07 class-weighted blend)
        bool bEnableAnisotropy = false;
        float ContinentalAnisoAlong = 1.0f;
        float ContinentalAnisoAcross = 0.6f;
        float AnisoClassWeights[4] = {0.0f, 0.6f, 1.0f, 0.3f};
    };

    // Epsilon for clamping exemplar UVs to [ε, 1-ε] to avoid border sampling issues
    // Shared between CPU and GPU exemplar sampling (decoupled from PoleAvoidanceEpsilon)
    static constexpr double StageB_UVWrapEpsilon = 1.0e-6;
} // namespace StageB
} // namespace PlanetaryCreation
