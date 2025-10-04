#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "Editor.h"

/**
 * Milestone 3 Task 2.2: Validate velocity field computation (v = ω × r).
 * Ensures correctness and orthogonality properties.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVelocityFieldValidation,
    "PlanetaryCreation.Milestone3.VelocityField",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVelocityFieldValidation::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
    if (!GEditor)
    {
        AddError(TEXT("GEditor is null - test requires editor context"));
        return false;
    }

    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    if (!Service)
    {
        AddError(TEXT("Failed to get UTectonicSimulationService"));
        return false;
    }

    // Test at subdivision level 3 (642 vertices)
    FTectonicSimulationParameters Params;
    Params.Seed = 42;
    Params.RenderSubdivisionLevel = 3;
    Service->SetParameters(Params);

    const TArray<FVector3d>& RenderVertices = Service->GetRenderVertices();
    const TArray<FVector3d>& VertexVelocities = Service->GetVertexVelocities();
    const TArray<int32>& VertexPlateAssignments = Service->GetVertexPlateAssignments();

    if (RenderVertices.Num() == 0 || VertexVelocities.Num() == 0)
    {
        AddError(TEXT("Velocity field not initialized"));
        return false;
    }

    TestEqual(TEXT("Velocity array size matches vertex count"), VertexVelocities.Num(), RenderVertices.Num());

    // ====================
    // Test 1: Orthogonality (v ⊥ r)
    // ====================
    // For a point on a sphere rotating around an axis, the velocity v = ω × r
    // must be perpendicular to the position vector r.
    // Dot product should be ~0 (within floating point tolerance).

    const double DotProductEpsilon = 1e-6;
    int32 NonOrthogonalCount = 0;

    for (int32 i = 0; i < RenderVertices.Num(); ++i)
    {
        const FVector3d& Position = RenderVertices[i];
        const FVector3d& Velocity = VertexVelocities[i];

        const double DotProduct = FVector3d::DotProduct(Position.GetSafeNormal(), Velocity.GetSafeNormal());

        if (FMath::Abs(DotProduct) > DotProductEpsilon)
        {
            NonOrthogonalCount++;
            if (NonOrthogonalCount <= 3)
            {
                AddError(FString::Printf(TEXT("Vertex %d: v·r = %.9f (expected ≈0)"), i, DotProduct));
            }
        }
    }

    TestEqual(TEXT("Velocity vectors orthogonal to position (v ⊥ r)"), NonOrthogonalCount, 0);

    // ====================
    // Test 2: Velocity Magnitude Range
    // ====================
    // Angular velocities are initialized in range [0.01, 0.1] rad/My
    // For unit sphere (r=1), velocity magnitude |v| = |ω × r| = |ω| * |r| * sin(θ)
    // where θ is angle between ω and r.
    // Maximum magnitude: 0.1 rad/My (when θ = 90°)
    // Minimum magnitude: 0.01 rad/My (when θ = 90°)

    double MinVel = TNumericLimits<double>::Max();
    double MaxVel = TNumericLimits<double>::Min();

    for (int32 i = 0; i < VertexVelocities.Num(); ++i)
    {
        const double VelMag = VertexVelocities[i].Length();
        MinVel = FMath::Min(MinVel, VelMag);
        MaxVel = FMath::Max(MaxVel, VelMag);
    }

    AddInfo(FString::Printf(TEXT("Velocity magnitude range: [%.4f, %.4f] rad/My"), MinVel, MaxVel));

    TestTrue(TEXT("Minimum velocity >= 0"), MinVel >= 0.0);
    TestTrue(TEXT("Maximum velocity <= 0.1 rad/My"), MaxVel <= 0.1 + 1e-6);

    // ====================
    // Test 3: Determinism (same seed → same velocities)
    // ====================
    Service->SetParameters(Params); // Regenerate with same seed

    const TArray<FVector3d>& RegenVelocities = Service->GetVertexVelocities();

    int32 Mismatches = 0;
    for (int32 i = 0; i < VertexVelocities.Num(); ++i)
    {
        if (!VertexVelocities[i].Equals(RegenVelocities[i], 1e-9))
        {
            Mismatches++;
        }
    }

    TestEqual(TEXT("Velocity field deterministic (same seed)"), Mismatches, 0);

    // ====================
    // Test 4: Tangential Direction (v ⊥ ω)
    // ====================
    // Velocity should also be perpendicular to the Euler pole axis.
    // This validates the cross product computation.

    const TArray<FTectonicPlate>& Plates = Service->GetPlates();
    int32 NonTangentialCount = 0;

    for (int32 i = 0; i < RenderVertices.Num(); ++i)
    {
        const int32 PlateID = VertexPlateAssignments[i];
        if (PlateID == INDEX_NONE) continue;

        const FTectonicPlate* Plate = Plates.FindByPredicate([PlateID](const FTectonicPlate& P)
        {
            return P.PlateID == PlateID;
        });

        if (Plate && VertexVelocities[i].Length() > 1e-9)
        {
            const FVector3d Omega = Plate->EulerPoleAxis * Plate->AngularVelocity;
            const double DotProduct = FVector3d::DotProduct(Omega.GetSafeNormal(), VertexVelocities[i].GetSafeNormal());

            if (FMath::Abs(DotProduct) > DotProductEpsilon)
            {
                NonTangentialCount++;
                if (NonTangentialCount <= 3)
                {
                    AddError(FString::Printf(TEXT("Vertex %d: v·ω = %.9f (expected ≈0)"), i, DotProduct));
                }
            }
        }
    }

    TestEqual(TEXT("Velocity vectors perpendicular to Euler pole (v ⊥ ω)"), NonTangentialCount, 0);

    AddInfo(TEXT("=== Velocity Field Validation Complete ==="));

    return true;
#else
    AddError(TEXT("Test requires WITH_EDITOR"));
    return false;
#endif
}
