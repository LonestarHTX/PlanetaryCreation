#pragma once

#include "CoreMinimal.h"
#include "Simulation/BoundaryField.h"

namespace Erosion
{
    struct PLANETARYCREATIONEDITOR_API FErosionMetrics
    {
        int32 ContinentalVertsChanged = 0;
        int32 OceanicVertsChanged = 0;
        int32 TrenchVertsChanged = 0;
        double ErosionDelta_m = 0.0;   // Sum of -Δz on continental
        double DampeningDelta_m = 0.0; // Sum of -Δz on oceanic
        double AccretionDelta_m = 0.0; // Sum of +Δz in trench band
        double ApplyMs = 0.0;
    };

    // Apply Section 4.5 updates with strict units and masking:
    // - Continental erosion:  z ← z − (z/zc)·εc·δt  (on continental plates, z>0)
    // - Oceanic dampening:    z ← z − (1 − z/zt)·εo·δt (on oceanic plates, toward trench depth)
    // - Trench accretion:     z ← z + εt·δt          (within band near subduction fronts)
    // Time step δt is PaperConstants::TimeStep_My. Rates ε* are per My.
    // Feature toggles (EnableContinental/EnableOceanic/EnableTrench) are read via CVars.
    PLANETARYCREATIONEDITOR_API FErosionMetrics ApplyErosionAndDampening(
        const TArray<FVector3d>& Points,
        const TArray<int32>& PlateIdPerVertex,
        const TArray<uint8>& PlateCrustTypePerPlate, // 0=oceanic, 1=continental
        const BoundaryField::FBoundaryFieldResults& Boundary,
        TArray<double>& InOutElevation_m,
        double TrenchBandKm);

    // Emit Phase 6 metrics JSON under Docs/Automation/Validation/Phase6/summary_<timestamp>.json
    PLANETARYCREATIONEDITOR_API FString WritePhase6MetricsJson(
        const FString& BackendName,
        int32 SampleCount,
        int32 Seed,
        const FErosionMetrics& Metrics);
}

