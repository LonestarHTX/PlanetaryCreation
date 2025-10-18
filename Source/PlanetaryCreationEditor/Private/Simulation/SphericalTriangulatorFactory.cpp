#include "Simulation/SphericalTriangulatorFactory.h"

#include "HAL/IConsoleManager.h"
#include "Logging/LogMacros.h"
#include "Simulation/Triangulators/GeogramTriangulator.h"
#include "Simulation/Triangulators/StripackTriangulator.h"

namespace
{
    static TAutoConsoleVariable<FString> CVarPaperTriangulationBackend(
        TEXT("r.PaperTriangulation.Backend"),
        TEXT("Auto"),
        TEXT("Select spherical triangulation backend (Auto, Geogram, Stripack)."),
        ECVF_Default);

    FString NormalizeBackend(const FString& InValue)
    {
        FString Value = InValue;
        Value.TrimStartAndEndInline();
        Value.ToLowerInline();
        return Value;
    }
}

ISphericalTriangulator& FSphericalTriangulatorFactory::Resolve(FString& OutBackendName, bool& OutUsedFallback)
{
    OutUsedFallback = false;

    const FString RequestedRaw = CVarPaperTriangulationBackend.GetValueOnAnyThread();
    const FString RequestedNormalized = NormalizeBackend(RequestedRaw);

    auto UseGeogram = [&OutBackendName]() -> ISphericalTriangulator&
    {
        OutBackendName = TEXT("Geogram");
        return FGeogramTriangulator::Get();
    };

    auto UseStripack = [&OutBackendName]() -> ISphericalTriangulator&
    {
        OutBackendName = TEXT("Stripack");
        return FStripackTriangulator::Get();
    };

    const bool bGeogramAvailable = FGeogramTriangulator::IsAvailable();
    const bool bStripackAvailable = FStripackTriangulator::IsAvailable();

    if (RequestedNormalized.IsEmpty() || RequestedNormalized == TEXT("auto"))
    {
        if (bGeogramAvailable)
        {
            return UseGeogram();
        }
        if (bStripackAvailable)
        {
            OutUsedFallback = true;
            return UseStripack();
        }
    }
    else if (RequestedNormalized == TEXT("geogram"))
    {
        if (bGeogramAvailable)
        {
            return UseGeogram();
        }
        if (bStripackAvailable)
        {
            UE_LOG(LogTemp, Warning, TEXT("Requested Geogram backend but it is unavailable. Falling back to STRIPACK."));
            OutUsedFallback = true;
            return UseStripack();
        }
    }
    else if (RequestedNormalized == TEXT("stripack"))
    {
        if (bStripackAvailable)
        {
            return UseStripack();
        }
        if (bGeogramAvailable)
        {
            UE_LOG(LogTemp, Warning, TEXT("Requested STRIPACK backend but it is unavailable. Falling back to Geogram."));
            OutUsedFallback = true;
            return UseGeogram();
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("Unknown triangulation backend '%s'; using auto-selection."), *RequestedRaw);
    }

    // Final fallback: return Geogram if compiled, otherwise Stripack even if unavailable.
    if (bGeogramAvailable)
    {
        OutUsedFallback = true;
        return UseGeogram();
    }

    OutBackendName = TEXT("Stripack");
    OutUsedFallback = !bStripackAvailable;
    return FStripackTriangulator::Get();
}

FString FSphericalTriangulatorFactory::GetConfiguredBackend()
{
    return CVarPaperTriangulationBackend.GetValueOnAnyThread();
}
