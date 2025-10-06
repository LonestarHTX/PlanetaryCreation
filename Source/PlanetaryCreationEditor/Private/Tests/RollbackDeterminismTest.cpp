// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlanetaryCreationLogging.h"
#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "Editor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRollbackDeterminismTest,
    "PlanetaryCreation.Milestone4.RollbackDeterminism",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRollbackDeterminismTest::RunTest(const FString& Parameters)
{
    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    if (!Service)
    {
        AddError(TEXT("Failed to get UTectonicSimulationService"));
        return false;
    }

    Service->ResetSimulation();

    const double InitialTime = Service->GetCurrentTimeMy();
    TestTrue(TEXT("Undo disabled on fresh reset"), !Service->CanUndo());
    TestTrue(TEXT("Redo disabled on fresh reset"), !Service->CanRedo());

    constexpr int32 TotalSteps = 3;
    constexpr double StepDurationMy = 2.0;

    // Capture snapshots after each step for later comparison
    TArray<double> StepTimes;
    StepTimes.Reserve(TotalSteps);

    TArray<TArray<FVector3d>> StepVertices;
    StepVertices.Reserve(TotalSteps + 1);
    StepVertices.Add(Service->GetRenderVertices()); // baseline state

    TArray<TArray<int32>> StepPlateAssignments;
    StepPlateAssignments.Reserve(TotalSteps + 1);
    StepPlateAssignments.Add(Service->GetVertexPlateAssignments());

    for (int32 Step = 0; Step < TotalSteps; ++Step)
    {
        Service->AdvanceSteps(1);
        StepTimes.Add(Service->GetCurrentTimeMy());
        StepVertices.Add(Service->GetRenderVertices());
        StepPlateAssignments.Add(Service->GetVertexPlateAssignments());
    }

    TestTrue(TEXT("Undo available after advancing"), Service->CanUndo());
    TestTrue(TEXT("Redo still unavailable"), !Service->CanRedo());

    // Helper lambdas for state comparison
    auto PlatesMatch = [](const TArray<int32>& A, const TArray<int32>& B)
    {
        if (A.Num() != B.Num())
        {
            return false;
        }
        for (int32 Index = 0; Index < A.Num(); ++Index)
        {
            if (A[Index] != B[Index])
            {
                return false;
            }
        }
        return true;
    };

    auto VerticesMatch = [](const TArray<FVector3d>& A, const TArray<FVector3d>& B)
    {
        if (A.Num() != B.Num())
        {
            return false;
        }
        for (int32 Index = 0; Index < A.Num(); ++Index)
        {
            if (!A[Index].Equals(B[Index], 1e-6))
            {
                return false;
            }
        }
        return true;
    };

    // Perform undo operations back to the initial snapshot
    for (int32 UndoIteration = TotalSteps; UndoIteration > 0; --UndoIteration)
    {
        TestTrue(TEXT("Undo succeeds"), Service->Undo());

        const double ExpectedTime = (UndoIteration > 1)
            ? StepTimes[UndoIteration - 2]
            : InitialTime;

        TestTrue(TEXT("Undo rewinds simulation time"),
            FMath::IsNearlyEqual(Service->GetCurrentTimeMy(), ExpectedTime, 1e-6));

        const TArray<FVector3d>& ExpectedVertices = StepVertices[UndoIteration - 1];
        const TArray<int32>& ExpectedAssignments = StepPlateAssignments[UndoIteration - 1];

        TestTrue(TEXT("Undo restores vertex positions"),
            VerticesMatch(Service->GetRenderVertices(), ExpectedVertices));
        TestTrue(TEXT("Undo restores plate assignments"),
            PlatesMatch(Service->GetVertexPlateAssignments(), ExpectedAssignments));

        TestTrue(TEXT("Redo becomes available after undo"), Service->CanRedo());
    }

    TestTrue(TEXT("All undo operations consumed"), !Service->CanUndo());

    // Redo back to the latest snapshot
    for (int32 RedoIteration = 0; RedoIteration < TotalSteps; ++RedoIteration)
    {
        TestTrue(TEXT("Redo succeeds"), Service->Redo());

        const double ExpectedTime = StepTimes[RedoIteration];
        const TArray<FVector3d>& ExpectedVertices = StepVertices[RedoIteration + 1];
        const TArray<int32>& ExpectedAssignments = StepPlateAssignments[RedoIteration + 1];

        TestTrue(TEXT("Redo restores simulation time"),
            FMath::IsNearlyEqual(Service->GetCurrentTimeMy(), ExpectedTime, 1e-6));
        TestTrue(TEXT("Redo restores vertex positions"),
            VerticesMatch(Service->GetRenderVertices(), ExpectedVertices));
        TestTrue(TEXT("Redo restores plate assignments"),
            PlatesMatch(Service->GetVertexPlateAssignments(), ExpectedAssignments));
    }

    TestTrue(TEXT("Redo stack exhausted"), !Service->CanRedo());
    TestTrue(TEXT("Undo stack available after redo chain"), Service->CanUndo());

    return true;
}
