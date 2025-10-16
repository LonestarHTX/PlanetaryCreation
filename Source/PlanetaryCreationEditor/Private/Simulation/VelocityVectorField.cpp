// Milestone 4 Task 3.2: Velocity Vector Field Rendering

#include "Utilities/PlanetaryCreationLogging.h"
#include "Simulation/TectonicSimulationController.h"
#include "Simulation/TectonicSimulationService.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Components/LineBatchComponent.h"

void FTectonicSimulationController::DrawVelocityVectorField()
{
#if WITH_EDITOR
    if (!GEditor)
    {
        return;
    }

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        return;
    }

    ULineBatchComponent* LineBatcher = World->PersistentLineBatcher;
    if (!LineBatcher)
    {
        LineBatcher = World->LineBatcher;
    }
    if (!LineBatcher)
    {
        return;
    }

    constexpr uint32 VelocityFieldBatchId = 0x56454C46; // 'VELF' (Velocity Field)

    // Always clear the batch first (removes arrows when feature is disabled)
    LineBatcher->ClearBatch(VelocityFieldBatchId);

    UTectonicSimulationService* Service = GetService();
    if (!Service)
    {
        return;
    }

    const bool bVelocityModeActive = Service->GetVisualizationMode() == ETectonicVisualizationMode::Velocity;
    if (!bVelocityModeActive)
    {
        return;
    }

    const TArray<FTectonicPlate>& Plates = Service->GetPlates();
    if (Plates.Num() == 0)
    {
        return;
    }

    constexpr float RadiusUnits = 6370.0f; // 1 unit = 1 km
    constexpr float LineDuration = 0.0f; // Persistent

    // Milestone 4 Task 3.2: Render velocity vectors at plate centroids
    // Arrow length proportional to velocity magnitude
    // Color modulation: Blue (slow) → Yellow (medium) → Red (fast)

    // Find min/max velocity for color scaling
    double MinVelocityMagnitude = TNumericLimits<double>::Max();
    double MaxVelocityMagnitude = 0.0;

    for (const FTectonicPlate& Plate : Plates)
    {
        const double VelocityMagnitude = FMath::Abs(Plate.AngularVelocity);
        MinVelocityMagnitude = FMath::Min(MinVelocityMagnitude, VelocityMagnitude);
        MaxVelocityMagnitude = FMath::Max(MaxVelocityMagnitude, VelocityMagnitude);
    }

    // Avoid divide-by-zero if all plates are stationary
    if (MaxVelocityMagnitude < 1e-6)
    {
        MaxVelocityMagnitude = 0.1; // Default scale
    }

    UE_LOG(LogPlanetaryCreation, Verbose, TEXT("[VelocityField] Velocity range: %.4f - %.4f rad/My"),
        MinVelocityMagnitude, MaxVelocityMagnitude);

    auto GetVelocityColor = [](double NormalizedVelocity) -> FColor
    {
        // Color ramp: Blue (0.0) → Cyan (0.25) → Green (0.5) → Yellow (0.75) → Red (1.0)
        if (NormalizedVelocity < 0.25)
        {
            // Blue → Cyan
            const float T = NormalizedVelocity / 0.25f;
            return FColor(0, static_cast<uint8>(T * 255), 255, 255);
        }
        else if (NormalizedVelocity < 0.5)
        {
            // Cyan → Green
            const float T = (NormalizedVelocity - 0.25f) / 0.25f;
            return FColor(0, 255, static_cast<uint8>((1.0f - T) * 255), 255);
        }
        else if (NormalizedVelocity < 0.75)
        {
            // Green → Yellow
            const float T = (NormalizedVelocity - 0.5f) / 0.25f;
            return FColor(static_cast<uint8>(T * 255), 255, 0, 255);
        }
        else
        {
            // Yellow → Red
            const float T = (NormalizedVelocity - 0.75f) / 0.25f;
            return FColor(255, static_cast<uint8>((1.0f - T) * 255), 0, 255);
        }
    };

    auto GetArrowLength = [](double VelocityMagnitude, double MaxVelocity) -> float
    {
        // Scale arrow length based on velocity magnitude
        // Base length: 500 km (typical plate velocity ~ 5 cm/yr ~ 0.05 rad/My)
        // Max length: 2000 km
        constexpr float BaseLength = 500.0f;
        constexpr float MaxLength = 2000.0f;

        const float NormalizedVelocity = static_cast<float>(VelocityMagnitude / MaxVelocity);
        return BaseLength + (MaxLength - BaseLength) * NormalizedVelocity;
    };

    for (const FTectonicPlate& Plate : Plates)
    {
        const FVector3d Centroid = Plate.Centroid.GetSafeNormal();
        const FVector3d EulerPoleAxis = Plate.EulerPoleAxis.GetSafeNormal();
        const double AngularVelocity = Plate.AngularVelocity;

        const double VelocityMagnitude = FMath::Abs(AngularVelocity);
        const double NormalizedVelocity = VelocityMagnitude / MaxVelocityMagnitude;

        // Compute surface velocity at centroid: v = ω × r
        // where ω is the angular velocity vector (axis * magnitude)
        // and r is the position vector (centroid)
        const FVector3d AngularVelocityVector = EulerPoleAxis * AngularVelocity;
        const FVector3d SurfaceVelocity = FVector3d::CrossProduct(AngularVelocityVector, Centroid);
        const FVector3d VelocityDirection = SurfaceVelocity.GetSafeNormal();

        const float ArrowLength = GetArrowLength(VelocityMagnitude, MaxVelocityMagnitude);
        const FColor ArrowColor = GetVelocityColor(NormalizedVelocity);

        // Convert to world space
        const FVector WorldCentroid = FVector(Centroid * RadiusUnits);
        const FVector WorldTip = FVector((Centroid + VelocityDirection * (ArrowLength / RadiusUnits)) * RadiusUnits);

        // Draw arrow shaft
        constexpr float ShaftThickness = 10.0f;
        LineBatcher->DrawLine(
            WorldCentroid,
            WorldTip,
            ArrowColor,
            SDPG_World,
            ShaftThickness,
            LineDuration,
            VelocityFieldBatchId
        );

        // Draw arrowhead (two lines forming a V)
        const FVector3d ArrowheadLeft = (VelocityDirection * 0.8 + FVector3d::CrossProduct(VelocityDirection, Centroid).GetSafeNormal() * 0.3).GetSafeNormal();
        const FVector3d ArrowheadRight = (VelocityDirection * 0.8 - FVector3d::CrossProduct(VelocityDirection, Centroid).GetSafeNormal() * 0.3).GetSafeNormal();

        const float ArrowheadLength = ArrowLength * 0.25f;

        const FVector WorldArrowheadLeft = FVector((Centroid + ArrowheadLeft * (ArrowheadLength / RadiusUnits)) * RadiusUnits);
        const FVector WorldArrowheadRight = FVector((Centroid + ArrowheadRight * (ArrowheadLength / RadiusUnits)) * RadiusUnits);

        LineBatcher->DrawLine(
            WorldTip,
            WorldArrowheadLeft,
            ArrowColor,
            SDPG_World,
            ShaftThickness,
            LineDuration,
            VelocityFieldBatchId
        );

        LineBatcher->DrawLine(
            WorldTip,
            WorldArrowheadRight,
            ArrowColor,
            SDPG_World,
            ShaftThickness,
            LineDuration,
            VelocityFieldBatchId
        );
    }

    UE_LOG(LogPlanetaryCreation, Verbose, TEXT("[VelocityField] Drew %d velocity vectors"), Plates.Num());
#endif
}
