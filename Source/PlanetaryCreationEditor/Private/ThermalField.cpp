// Milestone 4 Task 2.3: Thermal & Stress Coupling (Analytic Model)

#include "TectonicSimulationService.h"

void UTectonicSimulationService::ComputeThermalField()
{
    // Milestone 4 Task 2.3: Analytic temperature field T(r) = T_max * exp(-r^2 / σ^2)
    // Combines hotspot thermal plumes + subduction zone heating

    VertexTemperatureValues.SetNum(RenderVertices.Num());

    // Baseline mantle temperature (Kelvin)
    constexpr double BaselineMantleTemp = 1600.0; // ~1600K at 100km depth

    for (int32 VertexIdx = 0; VertexIdx < RenderVertices.Num(); ++VertexIdx)
    {
        const FVector3d& VertexPos = RenderVertices[VertexIdx];
        double Temperature = BaselineMantleTemp;

        // Contribution 1: Hotspot plumes (Gaussian falloff)
        if (Parameters.bEnableHotspots && Hotspots.Num() > 0)
        {
            for (const FMantleHotspot& Hotspot : Hotspots)
            {
                // Angular distance from hotspot (great circle distance)
                const double CosDistance = FMath::Clamp(FVector3d::DotProduct(VertexPos, Hotspot.Position), -1.0, 1.0);
                const double AngularDistance = FMath::Acos(CosDistance);

                if (AngularDistance > Hotspot.InfluenceRadius)
                    continue; // Outside influence radius

                // Analytic Gaussian: T(r) = T_max * exp(-r^2 / σ^2)
                // T_max scales with thermal output (major hotspots = hotter)
                const double T_max = 400.0 * Hotspot.ThermalOutput; // Major: 800K, Minor: 400K
                const double Sigma = Hotspot.InfluenceRadius / 2.0;
                const double Falloff = FMath::Exp(-FMath::Square(AngularDistance) / FMath::Square(Sigma));

                Temperature += T_max * Falloff;
            }
        }

        // Contribution 2: Subduction zone heating (linear proximity to convergent boundaries)
        // Subduction generates heat from friction and mantle wedge melting
        for (const auto& BoundaryPair : Boundaries)
        {
            const FPlateBoundary& Boundary = BoundaryPair.Value;

            // Only convergent boundaries contribute thermal heating
            if (Boundary.BoundaryType != EBoundaryType::Convergent)
                continue;

            // Skip low-stress boundaries (not actively subducting)
            if (Boundary.AccumulatedStress < 50.0)
                continue;

            // Compute distance to boundary (approximate using plate centroids)
            const int32 PlateA_ID = BoundaryPair.Key.Key;
            const int32 PlateB_ID = BoundaryPair.Key.Value;

            if (!Plates.IsValidIndex(PlateA_ID) || !Plates.IsValidIndex(PlateB_ID))
                continue;

            // Midpoint between plate centroids as boundary location approximation
            const FVector3d BoundaryPos = ((Plates[PlateA_ID].Centroid + Plates[PlateB_ID].Centroid) * 0.5).GetSafeNormal();

            // Angular distance from boundary
            const double CosDistance = FMath::Clamp(FVector3d::DotProduct(VertexPos, BoundaryPos), -1.0, 1.0);
            const double AngularDistance = FMath::Acos(CosDistance);

            // Subduction heating influence radius (~0.1 rad ≈ 5.7°)
            constexpr double SubductionInfluenceRadius = 0.1;

            if (AngularDistance < SubductionInfluenceRadius)
            {
                // Linear falloff from boundary: T = T_max * (1 - r/R)
                // T_max scales with stress (higher stress = more friction heating)
                const double T_max_subduction = Boundary.AccumulatedStress * 2.0; // 100 MPa → +200K
                const double Falloff = 1.0 - (AngularDistance / SubductionInfluenceRadius);

                Temperature += T_max_subduction * Falloff;
            }
        }

        // Clamp temperature to realistic range (0K - 3000K mantle max)
        VertexTemperatureValues[VertexIdx] = FMath::Clamp(Temperature, 0.0, 3000.0);
    }
}
