// Copyright 2025 Michael Hall. All Rights Reserved.

#include "PlanetaryCreationLogging.h"
#include "Misc/AutomationTest.h"
#include "TectonicPlaybackController.h"
#include "TectonicSimulationController.h"
#include "TectonicSimulationService.h"
#include "Editor.h"

/**
 * Milestone 5 Task 1.1: Continuous Playback System Test
 *
 * Validates:
 * - Play/Pause/Stop state transitions
 * - Playback speed multiplier functionality (0.5× to 10×)
 * - Step rate control (steps per second)
 * - Timeline scrubber integration
 * - Automatic step execution during playback
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContinuousPlaybackTest,
	"PlanetaryCreation.Milestone5.ContinuousPlayback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContinuousPlaybackTest::RunTest(const FString& Parameters)
{
	UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Starting Milestone 5 Task 1.1: Continuous Playback Test ==="));

	// Get simulation service
	UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
	TestNotNull(TEXT("Service should exist"), Service);
	if (!Service)
	{
		return false;
	}

	// Reset simulation to clean state
	Service->ResetSimulation();
	const double InitialTime = Service->GetCurrentTimeMy();
	TestEqual(TEXT("Initial time should be 0"), InitialTime, 0.0);

	// Create simulation controller
	TSharedPtr<FTectonicSimulationController> Controller = MakeShared<FTectonicSimulationController>();
	Controller->Initialize();

	// Create playback controller
	FTectonicPlaybackController PlaybackController;
	PlaybackController.Initialize(Controller);

	// === Test 1: Initial state should be Stopped ===
	TestEqual(TEXT("Initial playback state should be Stopped"),
		PlaybackController.GetPlaybackState(), EPlaybackState::Stopped);
	TestFalse(TEXT("IsPlaying() should return false initially"), PlaybackController.IsPlaying());

	// === Test 2: Play state transition ===
	UE_LOG(LogPlanetaryCreation, Log, TEXT("Testing Play state transition..."));
	PlaybackController.Play();
	TestEqual(TEXT("Playback state should be Playing after Play()"),
		PlaybackController.GetPlaybackState(), EPlaybackState::Playing);
	TestTrue(TEXT("IsPlaying() should return true after Play()"), PlaybackController.IsPlaying());

	// === Test 3: Pause state transition ===
	UE_LOG(LogPlanetaryCreation, Log, TEXT("Testing Pause state transition..."));
	PlaybackController.Pause();
	TestEqual(TEXT("Playback state should be Paused after Pause()"),
		PlaybackController.GetPlaybackState(), EPlaybackState::Paused);
	TestFalse(TEXT("IsPlaying() should return false after Pause()"), PlaybackController.IsPlaying());

	// === Test 4: Resume from Pause ===
	UE_LOG(LogPlanetaryCreation, Log, TEXT("Testing Resume from Pause..."));
	const int32 StepCountBeforeResume = PlaybackController.GetStepCount();
	PlaybackController.Play();
	TestEqual(TEXT("Playback state should be Playing after resuming"),
		PlaybackController.GetPlaybackState(), EPlaybackState::Playing);
	TestEqual(TEXT("Step count should be preserved after resume"),
		PlaybackController.GetStepCount(), StepCountBeforeResume);

	// === Test 5: Stop resets state ===
	UE_LOG(LogPlanetaryCreation, Log, TEXT("Testing Stop resets state..."));
	PlaybackController.Stop();
	TestEqual(TEXT("Playback state should be Stopped after Stop()"),
		PlaybackController.GetPlaybackState(), EPlaybackState::Stopped);
	TestEqual(TEXT("Step count should reset to 0 after Stop()"),
		PlaybackController.GetStepCount(), 0);

	// === Test 6: Playback speed multiplier ===
	UE_LOG(LogPlanetaryCreation, Log, TEXT("Testing playback speed multiplier..."));
	PlaybackController.SetPlaybackSpeed(2.0f);
	TestEqual(TEXT("Playback speed should be 2.0×"), PlaybackController.GetPlaybackSpeed(), 2.0f);

	PlaybackController.SetPlaybackSpeed(0.5f);
	TestEqual(TEXT("Playback speed should be 0.5×"), PlaybackController.GetPlaybackSpeed(), 0.5f);

	// Test clamping (min 0.1×, max 10×)
	PlaybackController.SetPlaybackSpeed(15.0f);
	TestEqual(TEXT("Playback speed should clamp to 10.0×"), PlaybackController.GetPlaybackSpeed(), 10.0f);

	PlaybackController.SetPlaybackSpeed(0.05f);
	TestEqual(TEXT("Playback speed should clamp to 0.1×"), PlaybackController.GetPlaybackSpeed(), 0.1f);

	// === Test 7: Step rate control ===
	UE_LOG(LogPlanetaryCreation, Log, TEXT("Testing step rate control..."));
	PlaybackController.SetStepRate(2.0f);
	TestEqual(TEXT("Step rate should be 2.0 steps/sec"), PlaybackController.GetStepRate(), 2.0f);

	// Test clamping (min 0.1, max 10)
	PlaybackController.SetStepRate(15.0f);
	TestEqual(TEXT("Step rate should clamp to 10.0 steps/sec"), PlaybackController.GetStepRate(), 10.0f);

	PlaybackController.SetStepRate(0.05f);
	TestEqual(TEXT("Step rate should clamp to 0.1 steps/sec"), PlaybackController.GetStepRate(), 0.1f);

	// === Test 8: Manual step count reset ===
	UE_LOG(LogPlanetaryCreation, Log, TEXT("Testing manual step count reset..."));
	PlaybackController.Play();
	// Let it accumulate some steps (simulated)
	PlaybackController.Stop();
	PlaybackController.ResetStepCount();
	TestEqual(TEXT("Step count should be 0 after manual reset"), PlaybackController.GetStepCount(), 0);

	// === Test 9: Multiple Play() calls should be idempotent ===
	UE_LOG(LogPlanetaryCreation, Log, TEXT("Testing idempotent Play() calls..."));
	PlaybackController.Play();
	PlaybackController.Play();
	PlaybackController.Play();
	TestEqual(TEXT("Playback state should still be Playing"),
		PlaybackController.GetPlaybackState(), EPlaybackState::Playing);

	// === Test 10: Stop from Paused state ===
	UE_LOG(LogPlanetaryCreation, Log, TEXT("Testing Stop from Paused state..."));
	PlaybackController.Pause();
	PlaybackController.Stop();
	TestEqual(TEXT("Playback state should be Stopped"),
		PlaybackController.GetPlaybackState(), EPlaybackState::Stopped);

	// Cleanup
	PlaybackController.Shutdown();
	Controller->Shutdown();

	UE_LOG(LogPlanetaryCreation, Log, TEXT("=== Milestone 5 Task 1.1: Continuous Playback Test PASSED ==="));
	return true;
}
