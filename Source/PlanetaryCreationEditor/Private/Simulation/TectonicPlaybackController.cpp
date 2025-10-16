#include "Simulation/TectonicPlaybackController.h"

#include "Utilities/PlanetaryCreationLogging.h"
#include "Simulation/TectonicSimulationController.h"
#include "Containers/Ticker.h"

FTectonicPlaybackController::FTectonicPlaybackController()
{
}

FTectonicPlaybackController::~FTectonicPlaybackController()
{
	Shutdown();
}

void FTectonicPlaybackController::Initialize(TWeakPtr<FTectonicSimulationController> InSimulationController)
{
	SimulationController = InSimulationController;
	CurrentState = EPlaybackState::Stopped;
	AccumulatedTime = 0.0f;
	StepCount = 0;
}

void FTectonicPlaybackController::Shutdown()
{
	Stop();
	SimulationController.Reset();
}

void FTectonicPlaybackController::Play()
{
	if (!SimulationController.IsValid())
	{
		UE_LOG(LogPlanetaryCreation, Warning, TEXT("FTectonicPlaybackController::Play() - Invalid simulation controller"));
		return;
	}

	if (CurrentState == EPlaybackState::Playing)
	{
		// Already playing
		return;
	}

	CurrentState = EPlaybackState::Playing;
	AccumulatedTime = 0.0f;

	// Register ticker callback
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FTectonicPlaybackController::TickPlayback));

	UE_LOG(LogPlanetaryCreation, Log, TEXT("FTectonicPlaybackController::Play() - Playback started at %.1f×, %.1f steps/sec"),
		PlaybackSpeedMultiplier, StepsPerSecond);
}

void FTectonicPlaybackController::Pause()
{
	if (CurrentState != EPlaybackState::Playing)
	{
		return;
	}

	CurrentState = EPlaybackState::Paused;

	// Unregister ticker
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	UE_LOG(LogPlanetaryCreation, Log, TEXT("FTectonicPlaybackController::Pause() - Playback paused after %d steps"), StepCount);
}

void FTectonicPlaybackController::Stop()
{
	if (CurrentState == EPlaybackState::Stopped)
	{
		return;
	}

	CurrentState = EPlaybackState::Stopped;
	AccumulatedTime = 0.0f;
	StepCount = 0;

	// Unregister ticker
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	UE_LOG(LogPlanetaryCreation, Log, TEXT("FTectonicPlaybackController::Stop() - Playback stopped"));
}

void FTectonicPlaybackController::SetPlaybackSpeed(float SpeedMultiplier)
{
	PlaybackSpeedMultiplier = FMath::Clamp(SpeedMultiplier, 0.1f, 10.0f);
	UE_LOG(LogPlanetaryCreation, Log, TEXT("FTectonicPlaybackController::SetPlaybackSpeed() - Speed set to %.1f×"), PlaybackSpeedMultiplier);
}

void FTectonicPlaybackController::SetStepRate(float InStepsPerSecond)
{
	StepsPerSecond = FMath::Clamp(InStepsPerSecond, 0.1f, 10.0f);
	UE_LOG(LogPlanetaryCreation, Log, TEXT("FTectonicPlaybackController::SetStepRate() - Step rate set to %.1f steps/sec"), StepsPerSecond);
}

bool FTectonicPlaybackController::TickPlayback(float DeltaTime)
{
	if (!SimulationController.IsValid())
	{
		UE_LOG(LogPlanetaryCreation, Warning, TEXT("FTectonicPlaybackController::TickPlayback() - Invalid simulation controller, stopping playback"));
		Stop();
		return false;
	}

	if (CurrentState != EPlaybackState::Playing)
	{
		return false; // Stop ticking
	}

	// Accumulate time scaled by playback speed
	AccumulatedTime += DeltaTime * PlaybackSpeedMultiplier;

	// Calculate step interval
	float StepInterval = 1.0f / StepsPerSecond;

	// Execute steps based on accumulated time
	while (AccumulatedTime >= StepInterval)
	{
		ExecuteStep();
		AccumulatedTime -= StepInterval;
	}

	return true; // Continue ticking
}

void FTectonicPlaybackController::ExecuteStep()
{
	if (TSharedPtr<FTectonicSimulationController> Controller = SimulationController.Pin())
	{
		Controller->StepSimulation(1);
		StepCount++;

		// Log every 10 steps for debugging
		if (StepCount % 10 == 0)
		{
			UE_LOG(LogPlanetaryCreation, Log, TEXT("FTectonicPlaybackController::ExecuteStep() - Executed %d steps"), StepCount);
		}
	}
}
