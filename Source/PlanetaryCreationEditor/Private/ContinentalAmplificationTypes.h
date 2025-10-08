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

	// Cached texture data (loaded once, reused)
	TArray<uint16> HeightData;
	bool bDataLoaded = false;
};
