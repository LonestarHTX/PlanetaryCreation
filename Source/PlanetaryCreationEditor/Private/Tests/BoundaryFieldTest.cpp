#include "Misc/AutomationTest.h"
#include "Simulation/FibonacciSampling.h"
#include "Simulation/SphericalDelaunay.h"
#include "Simulation/SphericalTriangulatorFactory.h"
#include "Simulation/BoundaryField.h"
#include "Simulation/PaperConstants.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/DefaultValueHelper.h"

using namespace BoundaryField;

static void BuildHemispherePartition(int32 N, TArray<int32>& OutPlateAssignments)
{
    OutPlateAssignments.SetNumUninitialized(N);
}

// CVar: r.PaperBoundary.TestPointCount controls test point count (>= 1000)
static TAutoConsoleVariable<int32> CVarPaperBoundaryTestPointCount(
    TEXT("r.PaperBoundary.TestPointCount"),
    10000,
    TEXT("BoundaryField test point count (smoke=10000, integration=50000). Clamped to >= 1000."),
    ECVF_Default);

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoundaryFieldTest, "PlanetaryCreation.Paper.BoundaryField",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBoundaryFieldTest::RunTest(const FString& Parameters)
{
    using namespace PaperConstants;

    int32 RequestedN = CVarPaperBoundaryTestPointCount.GetValueOnAnyThread();
    if (IConsoleVariable* OverrideVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PaperBoundary.TestPointCount")))
    {
        RequestedN = OverrideVar->GetInt();
    }
    // Fallback: parse -SetCVar tokens directly for robustness in automation harness
    {
        const TCHAR* CmdLine = FCommandLine::Get();
        const TCHAR* Search = CmdLine;
        const TCHAR* const SetCVarLiteral = TEXT("SetCVar=");
        const int32 SetCVarLiteralLen = FCString::Strlen(SetCVarLiteral);
        const FString TargetName = TEXT("r.PaperBoundary.TestPointCount");

        while ((Search = FCString::Strstr(Search, SetCVarLiteral)) != nullptr)
        {
            const TCHAR* AfterLiteral = Search + SetCVarLiteralLen;
            const TCHAR* ParseCursor = AfterLiteral;

            FString Token;
            if (!FParse::Token(ParseCursor, Token, false))
            {
                Search = AfterLiteral;
                continue;
            }
            Search = ParseCursor;

            Token.TrimStartAndEndInline();
            if (Token.StartsWith(TEXT("\"")) && Token.EndsWith(TEXT("\"")) && Token.Len() >= 2)
            {
                Token.RightChopInline(1);
                Token.LeftChopInline(1);
            }

            FString Name;
            FString ValueString;
            if (!Token.Split(TEXT("="), &Name, &ValueString))
            {
                continue;
            }
            Name.TrimStartAndEndInline();
            ValueString.TrimStartAndEndInline();

            if (!Name.Equals(TargetName, ESearchCase::IgnoreCase))
            {
                continue;
            }

            // Extract only the value for this CVar (stop at comma if multiple are present)
            FString FirstValue = ValueString;
            {
                int32 CommaIdx = INDEX_NONE;
                if (FirstValue.FindChar(TEXT(','), CommaIdx) && CommaIdx > 0)
                {
                    FirstValue.LeftInline(CommaIdx, /*bAllowShrinking*/ false);
                }
            }

            int32 Parsed = RequestedN;
            if (FDefaultValueHelper::ParseInt(FirstValue, Parsed))
            {
                RequestedN = Parsed;
            }
        }
    }
    const int32 N = FMath::Max(1000, RequestedN);
    FString BackendCfg = FSphericalTriangulatorFactory::GetConfiguredBackend();
    UE_LOG(LogTemp, Display, TEXT("BoundaryField Test: EffectiveN=%d Backend=%s"), N, *BackendCfg);
    TArray<FVector3d> Points;
    FFibonacciSampling::GenerateSamples(N, Points);

    // Triangulate then compute Voronoi neighbors (order-agnostic)
    TArray<FSphericalDelaunay::FTriangle> Tris;
    FSphericalDelaunay::Triangulate(Points, Tris);
    TArray<TArray<int32>> Neighbors;
    FSphericalDelaunay::ComputeVoronoiNeighbors(Points, Tris, Neighbors);

    // Hemisphere partition: z >= 0 => plate 0; z < 0 => plate 1
    TArray<int32> PlateAssign;
    PlateAssign.SetNumUninitialized(N);
    for (int32 i = 0; i < N; ++i)
    {
        PlateAssign[i] = (Points[i].Z >= 0.0) ? 0 : 1;
    }

    // Build helper to run a scenario and return results
    auto RunScenario = [&](const FVector3d& Omega0, const FVector3d& Omega1, FBoundaryFieldResults& Out)
    {
        TArray<FVector3d> Omegas;
        Omegas.SetNum(2);
        Omegas[0] = Omega0;
        Omegas[1] = Omega1;
        ComputeBoundaryFields(Points, Neighbors, PlateAssign, Omegas, Out);
    };

    // 1) Divergent across equator: use opposite angular velocities around X-axis.
    // This yields v_rel aligned with +/-Z at equator; with our boundary normal definition
    // n_b = normalize(m x t_edge), the dot tends to be positive (divergent) along the equator.
    {
        const double w = 0.02; // rad/My
        FBoundaryFieldResults Res;
        RunScenario(FVector3d(w, 0, 0), FVector3d(-w, 0, 0), Res);

        // Count boundary edges and classification ratios
        int32 BoundaryEdges = 0;
        int32 DivergentEdges = 0;
        for (int32 e = 0; e < Res.Edges.Num(); ++e)
        {
            const auto& edge = Res.Edges[e];
            const int32 pa = PlateAssign[edge.Key];
            const int32 pb = PlateAssign[edge.Value];
            if (pa != pb)
            {
                BoundaryEdges++;
                if (Res.Classifications[e] == EBoundaryClass::Divergent) DivergentEdges++;
            }
        }
        UE_LOG(LogTemp, Display, TEXT("BoundaryField Divergent: %d of %d boundary edges"), DivergentEdges, BoundaryEdges);
        TestTrue(TEXT("boundary edges exist"), BoundaryEdges > 0);
        TestTrue(TEXT("some divergent edges exist"), DivergentEdges > 0);

        // Distance seeds near boundary should be ~0; sample a few boundary vertices
        int32 Checked = 0;
        for (int32 e = 0; e < Res.Edges.Num() && Checked < 20; ++e)
        {
            if (Res.Classifications[e] == EBoundaryClass::Divergent)
            {
                const auto& edge = Res.Edges[e];
                TestTrue(TEXT("ridge seed a"), Res.DistanceToRidge_km[edge.Key] <= 1e-12);
                TestTrue(TEXT("ridge seed b"), Res.DistanceToRidge_km[edge.Value] <= 1e-12);
                Checked++;
            }
        }

        // Distances non-negative and finite
        for (int32 i = 0; i < N; ++i)
        {
            const double dr = Res.DistanceToRidge_km[i];
            const double ds = Res.DistanceToSubductionFront_km[i];
            TestTrue(TEXT("non-negative ridge"), dr >= 0.0);
            TestTrue(TEXT("non-negative subduction"), ds >= 0.0);
            TestTrue(TEXT("finite ridge"), FMath::IsFinite(dr));
            TestTrue(TEXT("finite subduction"), FMath::IsFinite(ds));
        }

        // Determinism: re-run and compare arrays
        FBoundaryFieldResults Res2;
        RunScenario(FVector3d(w, 0, 0), FVector3d(-w, 0, 0), Res2);
        TestEqual(TEXT("edge count deterministic"), Res.Edges.Num(), Res2.Edges.Num());
        for (int32 i = 0; i < N; ++i)
        {
            TestTrue(TEXT("ridge deterministic"), FMath::IsNearlyEqual(Res.DistanceToRidge_km[i], Res2.DistanceToRidge_km[i], 1e-12));
            TestTrue(TEXT("subduction deterministic"), FMath::IsNearlyEqual(Res.DistanceToSubductionFront_km[i], Res2.DistanceToSubductionFront_km[i], 1e-12));
        }
    }

    // 2) Convergent swap: flip signs, expect convergent predominates and subduction seeds at zero
    {
        const double w = 0.02; // rad/My
        FBoundaryFieldResults Res;
        RunScenario(FVector3d(-w, 0, 0), FVector3d(w, 0, 0), Res);

        int32 BoundaryEdges = 0;
        int32 ConvergentEdges = 0;
        for (int32 e = 0; e < Res.Edges.Num(); ++e)
        {
            const auto& edge = Res.Edges[e];
            const int32 pa = PlateAssign[edge.Key];
            const int32 pb = PlateAssign[edge.Value];
            if (pa != pb)
            {
                BoundaryEdges++;
                if (Res.Classifications[e] == EBoundaryClass::Convergent) ConvergentEdges++;
            }
        }
        UE_LOG(LogTemp, Display, TEXT("BoundaryField Convergent: %d of %d boundary edges"), ConvergentEdges, BoundaryEdges);
        TestTrue(TEXT("boundary edges exist (swap)"), BoundaryEdges > 0);
        TestTrue(TEXT("some convergent edges exist"), ConvergentEdges > 0);

        // Subduction seeds ~ 0
        int32 Checked = 0;
        for (int32 e = 0; e < Res.Edges.Num() && Checked < 20; ++e)
        {
            if (Res.Classifications[e] == EBoundaryClass::Convergent)
            {
                const auto& edge = Res.Edges[e];
                TestTrue(TEXT("subduction seed a"), Res.DistanceToSubductionFront_km[edge.Key] <= 1e-12);
                TestTrue(TEXT("subduction seed b"), Res.DistanceToSubductionFront_km[edge.Value] <= 1e-12);
                Checked++;
            }
        }
    }

    // 3) Transform case: identical omegas -> p ~ 0 along boundary, expect transform predominates
    {
        const double w = 0.02; // rad/My
        FBoundaryFieldResults Res;
        RunScenario(FVector3d(w, 0, 0), FVector3d(w, 0, 0), Res);

        int32 BoundaryEdges = 0;
        int32 TransformEdges = 0;
        for (int32 e = 0; e < Res.Edges.Num(); ++e)
        {
            const auto& edge = Res.Edges[e];
            const int32 pa = PlateAssign[edge.Key];
            const int32 pb = PlateAssign[edge.Value];
            if (pa != pb)
            {
                BoundaryEdges++;
                if (Res.Classifications[e] == EBoundaryClass::Transform) TransformEdges++;
            }
        }
        TestTrue(TEXT("transform predominates"), TransformEdges >= BoundaryEdges * 0.5);

        // Distances may remain large due to unseeded sets; just verify arrays are finite
        for (int32 i = 0; i < N; ++i)
        {
            TestTrue(TEXT("ridge finite"), FMath::IsFinite(Res.DistanceToRidge_km[i]));
            TestTrue(TEXT("subduction finite"), FMath::IsFinite(Res.DistanceToSubductionFront_km[i]));
        }
    }

    return true;
}
