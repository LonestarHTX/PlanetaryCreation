#pragma once

#include "CoreMinimal.h"

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
} // namespace StageB
} // namespace PlanetaryCreation
