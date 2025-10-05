// Milestone 6: Heightmap Visualization Exporter
// Generates color-coded PNG showing elevation gradient from min to max

#include "TectonicSimulationService.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

#include "HeightmapColorPalette.h"

/**
 * Project sphere vertex to equirectangular UV coordinates
 */
static FVector2D VertexToEquirectangularUV(const FVector3d& Position)
{
	const FVector3d N = Position.GetSafeNormal();
	const double U = 0.5 + (FMath::Atan2(N.Y, N.X) / (2.0 * PI));
	const double V = 0.5 - (FMath::Asin(FMath::Clamp(N.Z, -1.0, 1.0)) / PI);
	return FVector2D(U, V);
}

/**
 * Export heightmap as color-coded PNG with elevation gradient
 * Returns path to exported file, or empty string on failure
 */
FString UTectonicSimulationService::ExportHeightmapVisualization(int32 ImageWidth, int32 ImageHeight)
{
	if (RenderVertices.Num() == 0 || VertexAmplifiedElevation.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot export heightmap: No render data available"));
		return FString();
	}

	// Find min/max elevation
	double MinElevation = TNumericLimits<double>::Max();
	double MaxElevation = TNumericLimits<double>::Lowest();

	for (const double Elevation : VertexAmplifiedElevation)
	{
		MinElevation = FMath::Min(MinElevation, Elevation);
		MaxElevation = FMath::Max(MaxElevation, Elevation);
	}

	UE_LOG(LogTemp, Log, TEXT("Heightmap range: %.1f m to %.1f m"), MinElevation, MaxElevation);

	// Create image buffer (RGBA8)
	TArray<FColor> ImageData;
	ImageData.SetNumZeroed(ImageWidth * ImageHeight);

	// Rasterize vertices to image using equirectangular projection
	// For each vertex, find nearest pixel and write elevation color
	int32 DebugSampleCount = 0;
	for (int32 VertexIdx = 0; VertexIdx < RenderVertices.Num(); ++VertexIdx)
	{
		const FVector3d& Position = RenderVertices[VertexIdx];
		const double Elevation = VertexAmplifiedElevation[VertexIdx];

		// Normalize elevation to [0, 1]
		const double NormalizedHeight = (MaxElevation > MinElevation) ?
			(Elevation - MinElevation) / (MaxElevation - MinElevation) : 0.5;

		// Project to equirectangular UV
		const FVector2D UV = VertexToEquirectangularUV(Position);

		// Convert UV to pixel coordinates
		const int32 X = FMath::Clamp(static_cast<int32>(UV.X * ImageWidth), 0, ImageWidth - 1);
		const int32 Y = FMath::Clamp(static_cast<int32>(UV.Y * ImageHeight), 0, ImageHeight - 1);
		const int32 PixelIdx = Y * ImageWidth + X;

		// Write color
                const FColor ElevColor = PlanetaryCreation::Heightmap::MakeElevationColor(NormalizedHeight);
		ImageData[PixelIdx] = ElevColor;

		// Debug: Log first few samples
		if (DebugSampleCount < 5)
		{
			UE_LOG(LogTemp, Log, TEXT("Vertex %d: Pos=(%.2f,%.2f,%.2f) Elev=%.1f Norm=%.3f UV=(%.3f,%.3f) Pixel=(%d,%d) Color=(%d,%d,%d)"),
				VertexIdx, Position.X, Position.Y, Position.Z, Elevation, NormalizedHeight,
				UV.X, UV.Y, X, Y, ElevColor.R, ElevColor.G, ElevColor.B);
			DebugSampleCount++;
		}
	}

	// Fill gaps (pixels that had no vertex mapped to them)
	// Multi-pass dilation to fill large gaps between sparse vertices
	const int32 MaxDilationPasses = 50; // Fill up to 50-pixel gaps
	int32 BlackPixelCount = 0;

	// Count initial unfilled pixels (alpha=0, since SetNumZeroed gives us 0,0,0,0)
	for (const FColor& Color : ImageData)
	{
		if (Color.A == 0) // Unfilled pixels have alpha=0
			BlackPixelCount++;
	}

	UE_LOG(LogTemp, Log, TEXT("Heightmap dilation: %d/%d pixels are unfilled (%.1f%%)"),
		BlackPixelCount, ImageData.Num(), 100.0 * BlackPixelCount / ImageData.Num());

	// Multi-pass dilation
	for (int32 Pass = 0; Pass < MaxDilationPasses; ++Pass)
	{
		TArray<FColor> PreviousFrame = ImageData;
		int32 PixelsFilledThisPass = 0;

		for (int32 Y = 0; Y < ImageHeight; ++Y)
		{
			for (int32 X = 0; X < ImageWidth; ++X)
			{
				const int32 PixelIdx = Y * ImageWidth + X;
				if (PreviousFrame[PixelIdx].A == 0) // Unfilled pixel
				{
					// Search 3x3 neighborhood for filled pixel (alpha > 0)
					FColor NeighborColor(0, 0, 0, 0);
					for (int32 DY = -1; DY <= 1; ++DY)
					{
						for (int32 DX = -1; DX <= 1; ++DX)
						{
							const int32 NX = X + DX;
							const int32 NY = Y + DY;
							if (NX >= 0 && NX < ImageWidth && NY >= 0 && NY < ImageHeight)
							{
								const int32 NeighborIdx = NY * ImageWidth + NX;
								if (PreviousFrame[NeighborIdx].A > 0) // Filled pixel
								{
									NeighborColor = PreviousFrame[NeighborIdx];
									break;
								}
							}
						}
						if (NeighborColor.A > 0)
							break;
					}

					if (NeighborColor.A > 0)
					{
						ImageData[PixelIdx] = NeighborColor;
						PixelsFilledThisPass++;
					}
				}
			}
		}

		if (PixelsFilledThisPass == 0)
		{
			UE_LOG(LogTemp, Log, TEXT("Heightmap dilation converged after %d passes"), Pass + 1);
			break;
		}
	}

	// Encode as PNG
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

	if (!ImageWrapper.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to create PNG image wrapper"));
		return FString();
	}

	// Convert FColor array to raw RGBA8 bytes
	TArray<uint8> RawData;
	RawData.SetNumUninitialized(ImageWidth * ImageHeight * 4);
	for (int32 i = 0; i < ImageData.Num(); ++i)
	{
		RawData[i * 4 + 0] = ImageData[i].R;
		RawData[i * 4 + 1] = ImageData[i].G;
		RawData[i * 4 + 2] = ImageData[i].B;
		RawData[i * 4 + 3] = 255; // Full opacity
	}

	if (!ImageWrapper->SetRaw(RawData.GetData(), RawData.Num(), ImageWidth, ImageHeight, ERGBFormat::RGBA, 8))
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to set raw image data for PNG encoding"));
		return FString();
	}

	const TArray64<uint8>& CompressedData = ImageWrapper->GetCompressed();
	if (CompressedData.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to compress PNG image"));
		return FString();
	}

	// Write to file in project root
	const FString OutputPath = FPaths::ProjectDir() / TEXT("Heightmap_Visualization.png");
	if (!FFileHelper::SaveArrayToFile(CompressedData, *OutputPath))
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to write PNG to: %s"), *OutputPath);
		return FString();
	}

	UE_LOG(LogTemp, Log, TEXT("✅ Exported heightmap visualization (%dx%d): %s"), ImageWidth, ImageHeight, *OutputPath);
	UE_LOG(LogTemp, Log, TEXT("   Elevation range: %.1f m (blue) to %.1f m (red)"), MinElevation, MaxElevation);

	return OutputPath;
}
