// Copyright 2025 Michael Hall. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"
#include "Editor.h"

/**
 * Milestone 5 Task 1.3: Undo/Redo UI Test
 *
 * Validates:
 * - History snapshot capture after each step
 * - Undo/redo state restoration
 * - History stack boundaries (CanUndo/CanRedo)
 * - Timeline scrubbing via JumpToHistoryIndex
 * - History truncation on new step after undo
 * - Max history size enforcement
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUndoRedoUITest,
	"PlanetaryCreation.Milestone5.UndoRedoUI",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUndoRedoUITest::RunTest(const FString& Parameters)
{
	UE_LOG(LogTemp, Log, TEXT("=== Starting Milestone 5 Task 1.3: Undo/Redo UI Test ==="));

	// Get simulation service
	UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
	TestNotNull(TEXT("Service should exist"), Service);
	if (!Service)
	{
		return false;
	}

	// === Test 1: Reset initializes history with initial snapshot ===
	UE_LOG(LogTemp, Log, TEXT("Test 1: Reset initializes history..."));
	Service->ResetSimulation();
	TestEqual(TEXT("History should have 1 snapshot after reset"), Service->GetHistorySize(), 1);
	TestEqual(TEXT("Current history index should be 0"), Service->GetHistoryIndex(), 0);
	TestFalse(TEXT("CanUndo() should be false at initial state"), Service->CanUndo());
	TestFalse(TEXT("CanRedo() should be false at initial state"), Service->CanRedo());

	// === Test 2: AdvanceSteps() captures history snapshots ===
	UE_LOG(LogTemp, Log, TEXT("Test 2: AdvanceSteps captures history..."));
	Service->AdvanceSteps(3);
	TestEqual(TEXT("History should have 4 snapshots (initial + 3 steps)"), Service->GetHistorySize(), 4);
	TestEqual(TEXT("Current history index should be 3"), Service->GetHistoryIndex(), 3);
	TestTrue(TEXT("CanUndo() should be true after steps"), Service->CanUndo());
	TestFalse(TEXT("CanRedo() should be false (no future)"), Service->CanRedo());

	const double TimeAfter3Steps = Service->GetCurrentTimeMy();
	TestEqual(TEXT("Time should be 6 My after 3 steps (2 My each)"), TimeAfter3Steps, 6.0);

	// === Test 3: Undo restores previous state ===
	UE_LOG(LogTemp, Log, TEXT("Test 3: Undo restores state..."));
	TestTrue(TEXT("Undo should succeed"), Service->Undo());
	TestEqual(TEXT("Current time should be 4 My after undo"), Service->GetCurrentTimeMy(), 4.0);
	TestEqual(TEXT("Current history index should be 2"), Service->GetHistoryIndex(), 2);
	TestTrue(TEXT("CanUndo() should still be true"), Service->CanUndo());
	TestTrue(TEXT("CanRedo() should now be true"), Service->CanRedo());

	// === Test 4: Multiple undos ===
	UE_LOG(LogTemp, Log, TEXT("Test 4: Multiple undos..."));
	Service->Undo();
	Service->Undo();
	TestEqual(TEXT("Current time should be 0 My after 2 more undos"), Service->GetCurrentTimeMy(), 0.0);
	TestEqual(TEXT("Current history index should be 0"), Service->GetHistoryIndex(), 0);
	TestFalse(TEXT("CanUndo() should be false at start"), Service->CanUndo());
	TestTrue(TEXT("CanRedo() should be true"), Service->CanRedo());

	// === Test 5: Undo at boundary returns false ===
	UE_LOG(LogTemp, Log, TEXT("Test 5: Undo at boundary..."));
	TestFalse(TEXT("Undo should fail at history start"), Service->Undo());

	// === Test 6: Redo restores forward state ===
	UE_LOG(LogTemp, Log, TEXT("Test 6: Redo restores state..."));
	TestTrue(TEXT("Redo should succeed"), Service->Redo());
	TestEqual(TEXT("Current time should be 2 My after redo"), Service->GetCurrentTimeMy(), 2.0);
	TestEqual(TEXT("Current history index should be 1"), Service->GetHistoryIndex(), 1);

	// === Test 7: Multiple redos ===
	UE_LOG(LogTemp, Log, TEXT("Test 7: Multiple redos..."));
	Service->Redo();
	Service->Redo();
	TestEqual(TEXT("Current time should be 6 My after 2 more redos"), Service->GetCurrentTimeMy(), 6.0);
	TestEqual(TEXT("Current history index should be 3"), Service->GetHistoryIndex(), 3);
	TestTrue(TEXT("CanUndo() should be true"), Service->CanUndo());
	TestFalse(TEXT("CanRedo() should be false at end"), Service->CanRedo());

	// === Test 8: Redo at boundary returns false ===
	UE_LOG(LogTemp, Log, TEXT("Test 8: Redo at boundary..."));
	TestFalse(TEXT("Redo should fail at history end"), Service->Redo());

	// === Test 9: New step after undo truncates future history ===
	UE_LOG(LogTemp, Log, TEXT("Test 9: New step truncates future history..."));
	Service->Undo(); // Go back to 4 My (index 2)
	TestEqual(TEXT("After undo, history size should be 4"), Service->GetHistorySize(), 4);
	TestEqual(TEXT("After undo, current index should be 2"), Service->GetHistoryIndex(), 2);

	Service->AdvanceSteps(1); // Create branching history
	TestEqual(TEXT("After branching step, history size should be 4 (2 kept, 1 truncated, 1 new)"), Service->GetHistorySize(), 4);
	TestEqual(TEXT("After branching step, current index should be 3"), Service->GetHistoryIndex(), 3);
	TestEqual(TEXT("After branching step, time should be 6 My"), Service->GetCurrentTimeMy(), 6.0);
	TestFalse(TEXT("CanRedo() should be false after branching"), Service->CanRedo());

	// === Test 10: JumpToHistoryIndex ===
	UE_LOG(LogTemp, Log, TEXT("Test 10: JumpToHistoryIndex..."));
	TestTrue(TEXT("Jump to index 0 should succeed"), Service->JumpToHistoryIndex(0));
	TestEqual(TEXT("Time should be 0 My after jump"), Service->GetCurrentTimeMy(), 0.0);
	TestEqual(TEXT("Current index should be 0"), Service->GetHistoryIndex(), 0);

	TestTrue(TEXT("Jump to index 2 should succeed"), Service->JumpToHistoryIndex(2));
	TestEqual(TEXT("Time should be 4 My after jump"), Service->GetCurrentTimeMy(), 4.0);
	TestEqual(TEXT("Current index should be 2"), Service->GetHistoryIndex(), 2);

	TestFalse(TEXT("Jump to invalid index should fail"), Service->JumpToHistoryIndex(10));
	TestFalse(TEXT("Jump to negative index should fail"), Service->JumpToHistoryIndex(-1));

	// === Test 11: GetHistorySnapshotAt ===
	UE_LOG(LogTemp, Log, TEXT("Test 11: GetHistorySnapshotAt..."));
	const auto* Snapshot0 = Service->GetHistorySnapshotAt(0);
	TestNotNull(TEXT("Snapshot at index 0 should exist"), Snapshot0);
	if (Snapshot0)
	{
		TestEqual(TEXT("Snapshot 0 should have time 0 My"), Snapshot0->CurrentTimeMy, 0.0);
	}

	const auto* Snapshot2 = Service->GetHistorySnapshotAt(2);
	TestNotNull(TEXT("Snapshot at index 2 should exist"), Snapshot2);
	if (Snapshot2)
	{
		TestEqual(TEXT("Snapshot 2 should have time 4 My"), Snapshot2->CurrentTimeMy, 4.0);
	}

	const auto* InvalidSnapshot = Service->GetHistorySnapshotAt(100);
	TestNull(TEXT("Snapshot at invalid index should be null"), InvalidSnapshot);

	// === Test 12: Max history size enforcement (limit to 100 by default) ===
	UE_LOG(LogTemp, Log, TEXT("Test 12: Max history size enforcement..."));
	Service->ResetSimulation();

	// Simulate many steps to test history size limit
	// We'll do 105 steps to exceed the 100 limit
	for (int32 i = 0; i < 105; ++i)
	{
		Service->AdvanceSteps(1);
	}

	// History should be capped at 100 (the sliding window removes oldest)
	TestEqual(TEXT("History size should be capped at 100"), Service->GetHistorySize(), 100);
	TestEqual(TEXT("Current index should be 99 (last in window)"), Service->GetHistoryIndex(), 99);

	UE_LOG(LogTemp, Log, TEXT("=== Milestone 5 Task 1.3: Undo/Redo UI Test PASSED ==="));
	return true;
}
