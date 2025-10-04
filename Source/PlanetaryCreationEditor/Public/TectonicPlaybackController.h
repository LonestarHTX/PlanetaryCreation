#pragma once

#include "CoreMinimal.h"

class FTectonicSimulationController;

/** Milestone 5 Task 1.1: Playback modes for continuous simulation. */
enum class EPlaybackState : uint8
{
	Stopped,
	Playing,
	Paused
};

/**
 * Milestone 5 Task 1.1: Manages continuous playback of tectonic simulation.
 * Uses FTSTicker for frame-based step cadence.
 */
class FTectonicPlaybackController
{
public:
	FTectonicPlaybackController();
	~FTectonicPlaybackController();

	/** Initialize playback controller with simulation controller. */
	void Initialize(TWeakPtr<FTectonicSimulationController> InSimulationController);

	/** Shutdown and stop playback. */
	void Shutdown();

	/** Start continuous playback. */
	void Play();

	/** Pause playback (preserves state for resume). */
	void Pause();

	/** Stop playback (resets to stopped state). */
	void Stop();

	/** Get current playback state. */
	EPlaybackState GetPlaybackState() const { return CurrentState; }

	/** Check if playback is active. */
	bool IsPlaying() const { return CurrentState == EPlaybackState::Playing; }

	/** Set playback speed multiplier (0.5×, 1×, 2×, 5×, 10×). */
	void SetPlaybackSpeed(float SpeedMultiplier);

	/** Get current playback speed multiplier. */
	float GetPlaybackSpeed() const { return PlaybackSpeedMultiplier; }

	/** Set step rate (steps per second). */
	void SetStepRate(float StepsPerSecond);

	/** Get current step rate. */
	float GetStepRate() const { return StepsPerSecond; }

	/** Get total steps executed since playback started. */
	int32 GetStepCount() const { return StepCount; }

	/** Reset step counter. */
	void ResetStepCount() { StepCount = 0; }

private:
	/** Ticker callback for frame-based stepping. */
	bool TickPlayback(float DeltaTime);

	/** Execute one simulation step. */
	void ExecuteStep();

	TWeakPtr<FTectonicSimulationController> SimulationController;

	/** Current playback state. */
	EPlaybackState CurrentState = EPlaybackState::Stopped;

	/** Playback speed multiplier (default 1.0× = real-time). */
	float PlaybackSpeedMultiplier = 1.0f;

	/** Steps per second (default 1 step/sec). */
	float StepsPerSecond = 1.0f;

	/** Accumulated time for step cadence. */
	float AccumulatedTime = 0.0f;

	/** Steps executed since playback started. */
	int32 StepCount = 0;

	/** Ticker delegate handle. */
	FTSTicker::FDelegateHandle TickerHandle;
};
