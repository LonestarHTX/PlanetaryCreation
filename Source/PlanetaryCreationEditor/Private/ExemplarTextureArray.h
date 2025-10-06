// Milestone 6 GPU Acceleration: Exemplar Texture Array Management
// Loads PNG16 heightfield exemplars once and uploads to GPU as Texture2DArray for compute shaders

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"

class UTexture2DArray;

namespace PlanetaryCreation::GPU
{
	/**
	 * Manages exemplar heightfield textures as a Texture2DArray for GPU compute shaders.
	 * Loaded once when Stage B amplification is enabled, persists until module shutdown.
	 */
	class FExemplarTextureArray
	{
	public:
		FExemplarTextureArray() = default;
		~FExemplarTextureArray();

		/**
		 * Load PNG16 exemplars from ExemplarLibrary.json and upload to GPU as Texture2DArray.
		 * @param ProjectContentDir Path to project Content directory
		 * @return true if successful, false on error
		 */
		bool Initialize(const FString& ProjectContentDir);

		/**
		 * Release GPU resources (called on module shutdown or when Stage B is disabled)
		 */
		void Shutdown();

		/**
		 * Check if exemplar textures are loaded and ready for GPU use
		 */
		bool IsInitialized() const { return bInitialized; }

		/**
		 * Get the Texture2DArray resource for binding to compute shaders
		 */
		UTexture2DArray* GetTextureArray() const { return TextureArray; }

		/**
		 * Get number of exemplars loaded into the array
		 */
		int32 GetExemplarCount() const { return ExemplarCount; }

		/**
		 * Get texture dimensions (all exemplars resized to common resolution)
		 */
		int32 GetTextureWidth() const { return TextureWidth; }
		int32 GetTextureHeight() const { return TextureHeight; }

		/**
		 * Get exemplar metadata by index (for shader parameter setup)
		 */
		struct FExemplarInfo
		{
			FString ID;
			FString Region;  // "Himalayan", "Andean", "Ancient"
			float ElevationMin_m;
			float ElevationMax_m;
			float ElevationMean_m;
			int32 ArrayIndex;  // Index in Texture2DArray
		};

		const TArray<FExemplarInfo>& GetExemplarInfo() const { return ExemplarInfo; }

	private:
		bool bInitialized = false;
		UTexture2DArray* TextureArray = nullptr;
		int32 ExemplarCount = 0;
		int32 TextureWidth = 512;   // Common resolution for all exemplars
		int32 TextureHeight = 512;  // Common resolution for all exemplars
		TArray<FExemplarInfo> ExemplarInfo;

		// Load PNG16 and decode to uint16 array
		bool LoadPNG16(const FString& FilePath, TArray<uint16>& OutData, int32& OutWidth, int32& OutHeight);

		// Resize heightfield data to common resolution
		void ResizeHeightfield(const TArray<uint16>& InData, int32 InWidth, int32 InHeight,
		                       TArray<uint16>& OutData, int32 OutWidth, int32 OutHeight);
	};

	/**
	 * Global singleton accessor for exemplar texture array.
	 * Lazy-initialized when Stage B amplification is first enabled.
	 */
	FExemplarTextureArray& GetExemplarTextureArray();
}
