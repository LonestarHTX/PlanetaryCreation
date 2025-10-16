#pragma once

#include "CoreMinimal.h"

/**
 * Exemplar metadata loaded from ExemplarLibrary.json.
 * Shared between CPU amplification and GPU exemplar cache.
 */
struct FExemplarMetadata
{
	FString ID;
	FString Name;
	FString Region;  // "Himalayan", "Andean", or "Ancient"
	FString Feature;
	FString PNG16Path;
	double ElevationMin_m = 0.0;
	double ElevationMax_m = 0.0;
	double ElevationMean_m = 0.0;
	double ElevationStdDev_m = 0.0;
	int32 Width_px = 0;
	int32 Height_px = 0;
	double WestLonDeg = 0.0;
	double EastLonDeg = 0.0;
	double SouthLatDeg = 0.0;
	double NorthLatDeg = 0.0;
	bool bHasBounds = false;

	// Cached texture data (loaded once, reused)
	TArray<uint16> HeightData;
	bool bDataLoaded = false;

	/**
	 * Compute forced exemplar padding for seam/margin sampling.
	 * Uses 50% of range (clamped to max 5°) or minimum 1.5° for safety.
	 * Shared by HeightmapSampler and Stage B continental cache.
	 */
	void ComputeForcedPadding(double& OutLonPad, double& OutLatPad) const
	{
		const double LonRange = FMath::Abs(EastLonDeg - WestLonDeg);
		const double LatRange = FMath::Abs(NorthLatDeg - SouthLatDeg);
		
		// Use 50% of range for robust seam coverage, but clamp max to avoid soaking huge regions
		OutLonPad = FMath::Clamp(LonRange * 0.5, 1.5, 5.0);
		OutLatPad = FMath::Clamp(LatRange * 0.5, 1.5, 5.0);
	}
};

/** Returns the effective forced exemplar ID from CVars/env (empty when unset). */
FString GetStageBForcedExemplarId();

/** Returns true when Stage B random UV offsets should be disabled for deterministic runs. */
bool StageBShouldDisableRandomOffset();
