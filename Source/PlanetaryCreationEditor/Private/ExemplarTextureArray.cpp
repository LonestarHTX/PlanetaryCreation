// Milestone 6 GPU Acceleration: Exemplar Texture Array Management

#include "ExemplarTextureArray.h"
#include "PlanetaryCreationLogging.h"
#include "Hash/CityHash.h"
#include "Containers/StringConv.h"

#include "Engine/Texture2DArray.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "TextureResource.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "UObject/UObjectBaseUtility.h"

namespace PlanetaryCreation::GPU
{
	static FORCEINLINE uint64 CombineHash64(uint64 A, uint64 B)
	{
		return A ^ (B + 0x9e3779b97f4a7c15ull + (A << 6) + (A >> 2));
	}

	FExemplarTextureArray::~FExemplarTextureArray()
	{
		Shutdown();
	}

	bool FExemplarTextureArray::Initialize(const FString& ProjectContentDir)
	{
		if (bInitialized)
		{
			UE_LOG(LogPlanetaryCreation, Warning, TEXT("[ExemplarGPU] Already initialized, skipping"));
			return true;
		}

		UE_LOG(LogPlanetaryCreation, Log, TEXT("[ExemplarGPU] Initializing Texture2DArray from ExemplarLibrary.json"));
		LibraryFingerprint = 0;

		// Load ExemplarLibrary.json
		const FString JsonPath = ProjectContentDir / TEXT("PlanetaryCreation/Exemplars/ExemplarLibrary.json");
		FString JsonString;
		if (!FFileHelper::LoadFileToString(JsonString, *JsonPath))
		{
			UE_LOG(LogPlanetaryCreation, Error, TEXT("[ExemplarGPU] Failed to load: %s"), *JsonPath);
			return false;
		}

		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(JsonString);
		if (!FJsonSerializer::Deserialize(JsonReader, JsonObject) || !JsonObject.IsValid())
		{
			UE_LOG(LogPlanetaryCreation, Error, TEXT("[ExemplarGPU] Failed to parse ExemplarLibrary.json"));
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* ExemplarsArray = nullptr;
		if (!JsonObject->TryGetArrayField(TEXT("exemplars"), ExemplarsArray))
		{
			UE_LOG(LogPlanetaryCreation, Error, TEXT("[ExemplarGPU] Missing 'exemplars' array"));
			return false;
		}

		// First pass: load all PNG16 data and collect metadata
		struct FExemplarData
		{
			FExemplarInfo Info;
			TArray<uint16> RawData;
			int32 OriginalWidth;
			int32 OriginalHeight;
		};
		TArray<FExemplarData> ExemplarData;
		int32 LibraryIndexCounter = 0;

		for (const TSharedPtr<FJsonValue>& ExemplarValue : *ExemplarsArray)
		{
			const TSharedPtr<FJsonObject>& ExemplarObj = ExemplarValue->AsObject();
			if (!ExemplarObj.IsValid())
				continue;

			const int32 CurrentLibraryIndex = LibraryIndexCounter++;

			FExemplarData Data;
			Data.Info.ID = ExemplarObj->GetStringField(TEXT("id"));
			Data.Info.Region = ExemplarObj->GetStringField(TEXT("region"));
			Data.Info.ElevationMin_m = static_cast<float>(ExemplarObj->GetNumberField(TEXT("elevation_min_m")));
			Data.Info.ElevationMax_m = static_cast<float>(ExemplarObj->GetNumberField(TEXT("elevation_max_m")));
			Data.Info.ElevationMean_m = static_cast<float>(ExemplarObj->GetNumberField(TEXT("elevation_mean_m")));

			const FString PNG16RelPath = ExemplarObj->GetStringField(TEXT("png16_path"));
			const FString PNG16Path = ProjectContentDir / PNG16RelPath;

			// Load PNG16
			if (!LoadPNG16(PNG16Path, Data.RawData, Data.OriginalWidth, Data.OriginalHeight))
			{
				UE_LOG(LogPlanetaryCreation, Warning, TEXT("[ExemplarGPU] Failed to load: %s (skipping)"), *PNG16Path);
				continue;
			}

			Data.Info.ArrayIndex = ExemplarData.Num();
			Data.Info.LibraryIndex = CurrentLibraryIndex;
			ExemplarData.Add(MoveTemp(Data));
		}

		if (ExemplarData.Num() == 0)
		{
			UE_LOG(LogPlanetaryCreation, Error, TEXT("[ExemplarGPU] No valid exemplars loaded"));
			return false;
		}

		ExemplarCount = ExemplarData.Num();
		ExemplarInfo.Reserve(ExemplarCount);
		LibraryFingerprint = 1469598103934665603ull;

		UE_LOG(LogPlanetaryCreation, Log, TEXT("[ExemplarGPU] Loaded %d PNG16 exemplars, creating Texture2DArray (%dx%d)"),
			ExemplarCount, TextureWidth, TextureHeight);

		// Create Texture2DArray
		TextureArray = NewObject<UTexture2DArray>(GetTransientPackage(), NAME_None, RF_Transient);
		if (!TextureArray)
		{
			UE_LOG(LogPlanetaryCreation, Error, TEXT("[ExemplarGPU] Failed to create UTexture2DArray"));
			return false;
		}

		// Configure texture properties
		TextureArray->SetPlatformData(new FTexturePlatformData());
		TextureArray->GetPlatformData()->SizeX = TextureWidth;
		TextureArray->GetPlatformData()->SizeY = TextureHeight;
		TextureArray->GetPlatformData()->PixelFormat = PF_G16;  // 16-bit grayscale
		TextureArray->NeverStream = true;
		TextureArray->SRGB = false;  // Height data is linear
		TextureArray->Filter = TF_Bilinear;
		TextureArray->AddressX = TA_Clamp;
		TextureArray->AddressY = TA_Clamp;

		// Allocate mip level for all slices
		FTexture2DMipMap* Mip = new FTexture2DMipMap();
		Mip->SizeX = TextureWidth;
		Mip->SizeY = TextureHeight;
		Mip->SizeZ = ExemplarCount;  // Number of array slices

		const int32 BytesPerPixel = 2;  // PF_G16 = 16-bit = 2 bytes
		const int32 SliceSize = TextureWidth * TextureHeight * BytesPerPixel;
		const int32 TotalSize = SliceSize * ExemplarCount;

		Mip->BulkData.Lock(LOCK_READ_WRITE);
		uint8* MipData = reinterpret_cast<uint8*>(Mip->BulkData.Realloc(TotalSize));

		// Second pass: resize and copy each exemplar into Texture2DArray
		for (int32 i = 0; i < ExemplarCount; ++i)
		{
			const FExemplarData& Data = ExemplarData[i];
			TArray<uint16> ResizedData;

			// Resize to common resolution
			if (Data.OriginalWidth != TextureWidth || Data.OriginalHeight != TextureHeight)
			{
				ResizeHeightfield(Data.RawData, Data.OriginalWidth, Data.OriginalHeight,
				                  ResizedData, TextureWidth, TextureHeight);
			}
			else
			{
				ResizedData = Data.RawData;
			}

		double SumElevation = 0.0;
		double SumElevationSquared = 0.0;
        const double ElevationRange = static_cast<double>(Data.Info.ElevationMax_m - Data.Info.ElevationMin_m);
		const double ElevationMin = static_cast<double>(Data.Info.ElevationMin_m);
		const int32 SampleCount = ResizedData.Num();
		for (uint16 SampleValue : ResizedData)
		{
			const double Normalized = static_cast<double>(SampleValue) / 65535.0;
			const double Elevation = ElevationMin + Normalized * ElevationRange;
			SumElevation += Elevation;
			SumElevationSquared += Elevation * Elevation;
		}

		// Copy into Texture2DArray slice
		uint8* SliceData = MipData + (i * SliceSize);
		FMemory::Memcpy(SliceData, ResizedData.GetData(), SliceSize);

		FExemplarInfo& Info = ExemplarInfo.Add_GetRef(Data.Info);
		if (SampleCount > 0)
		{
			const double Mean = SumElevation / static_cast<double>(SampleCount);
			const double MeanSquare = SumElevationSquared / static_cast<double>(SampleCount);
			const double Variance = FMath::Max(MeanSquare - Mean * Mean, 0.0);
			Info.ElevationStdDev_m = static_cast<float>(FMath::Sqrt(Variance));
		}
		else
		{
			Info.ElevationStdDev_m = 0.0f;
		}
#if UE_BUILD_DEVELOPMENT
		Info.DebugHeightData = ResizedData;
		Info.DebugWidth = TextureWidth;
		Info.DebugHeight = TextureHeight;
#endif
		{
			FTCHARToUTF8 IdUtf8(*Info.ID);
			FTCHARToUTF8 RegionUtf8(*Info.Region);
			uint64 HashValue = 1469598103934665603ull;
			HashValue = CombineHash64(HashValue, CityHash64(reinterpret_cast<const char*>(IdUtf8.Get()), static_cast<size_t>(IdUtf8.Length())));
			HashValue = CombineHash64(HashValue, CityHash64(reinterpret_cast<const char*>(RegionUtf8.Get()), static_cast<size_t>(RegionUtf8.Length())));
			HashValue = CombineHash64(HashValue, CityHash64(reinterpret_cast<const char*>(&Info.ElevationMin_m), sizeof(float) * 3));
			HashValue = CombineHash64(HashValue, static_cast<uint64>(Info.LibraryIndex >= 0 ? Info.LibraryIndex : 0));
			HashValue = CombineHash64(HashValue, static_cast<uint64>(Info.ArrayIndex));
			LibraryFingerprint = CombineHash64(LibraryFingerprint, HashValue);
		}
		UE_LOG(LogPlanetaryCreation, Verbose, TEXT("[ExemplarGPU]   [%d] %s (%s) elev=[%.0f, %.0f]m"),
			i, *Data.Info.ID, *Data.Info.Region, Data.Info.ElevationMin_m, Data.Info.ElevationMax_m);
	}

		Mip->BulkData.Unlock();
		TextureArray->GetPlatformData()->Mips.Add(Mip);

		// Update GPU resource
		TextureArray->UpdateResource();

		bInitialized = true;
		UE_LOG(LogPlanetaryCreation, Log, TEXT("[ExemplarGPU] Texture2DArray initialized: %d exemplars, %dx%d PF_G16"),
			ExemplarCount, TextureWidth, TextureHeight);

		return true;
	}

	void FExemplarTextureArray::Shutdown()
	{
		if (!bInitialized)
			return;

		UE_LOG(LogPlanetaryCreation, Log, TEXT("[ExemplarGPU] Shutting down Texture2DArray"));

	if (TextureArray)
	{
		if (TextureArray->IsValidLowLevelFast())
		{
			TextureArray->ConditionalBeginDestroy();
		}
#if UE_BUILD_DEVELOPMENT
		else
		{
			UE_LOG(LogPlanetaryCreation, Verbose, TEXT("[ExemplarGPU] TextureArray already invalid at shutdown (skipping destroy)"));
		}
#endif
		TextureArray = nullptr;
	}

		ExemplarInfo.Empty();
		ExemplarCount = 0;
		bInitialized = false;
		LibraryFingerprint = 0;
	}

	bool FExemplarTextureArray::LoadPNG16(const FString& FilePath, TArray<uint16>& OutData, int32& OutWidth, int32& OutHeight)
	{
		// Load PNG file
		TArray<uint8> RawFileData;
		if (!FFileHelper::LoadFileToArray(RawFileData, *FilePath))
		{
			return false;
		}

		// Decode PNG
		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
		TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

		if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(RawFileData.GetData(), RawFileData.Num()))
		{
			return false;
		}

		OutWidth = ImageWrapper->GetWidth();
		OutHeight = ImageWrapper->GetHeight();
		const int32 BitDepth = ImageWrapper->GetBitDepth();

		if (BitDepth != 16)
		{
			UE_LOG(LogPlanetaryCreation, Error, TEXT("[ExemplarGPU] PNG bit depth is %d, expected 16: %s"), BitDepth, *FilePath);
			return false;
		}

		// Extract 16-bit raw data
		TArray64<uint8> RawData;
		if (!ImageWrapper->GetRaw(ERGBFormat::Gray, 16, RawData))
		{
			return false;
		}

		// Convert to uint16 array
		const int32 PixelCount = OutWidth * OutHeight;
		OutData.SetNumUninitialized(PixelCount);
		FMemory::Memcpy(OutData.GetData(), RawData.GetData(), PixelCount * sizeof(uint16));

		return true;
	}

	void FExemplarTextureArray::ResizeHeightfield(const TArray<uint16>& InData, int32 InWidth, int32 InHeight,
	                                               TArray<uint16>& OutData, int32 OutWidth, int32 OutHeight)
	{
		// Bilinear resampling
		OutData.SetNumUninitialized(OutWidth * OutHeight);

		const float ScaleX = static_cast<float>(InWidth - 1) / FMath::Max(OutWidth - 1, 1);
		const float ScaleY = static_cast<float>(InHeight - 1) / FMath::Max(OutHeight - 1, 1);

		for (int32 y = 0; y < OutHeight; ++y)
		{
			for (int32 x = 0; x < OutWidth; ++x)
			{
				const float SrcX = x * ScaleX;
				const float SrcY = y * ScaleY;

				const int32 X0 = FMath::FloorToInt(SrcX);
				const int32 Y0 = FMath::FloorToInt(SrcY);
				const int32 X1 = FMath::Min(X0 + 1, InWidth - 1);
				const int32 Y1 = FMath::Min(Y0 + 1, InHeight - 1);

				const float FracX = SrcX - X0;
				const float FracY = SrcY - Y0;

				const uint16 V00 = InData[Y0 * InWidth + X0];
				const uint16 V10 = InData[Y0 * InWidth + X1];
				const uint16 V01 = InData[Y1 * InWidth + X0];
				const uint16 V11 = InData[Y1 * InWidth + X1];

				const float V0 = FMath::Lerp(static_cast<float>(V00), static_cast<float>(V10), FracX);
				const float V1 = FMath::Lerp(static_cast<float>(V01), static_cast<float>(V11), FracX);
				const float V = FMath::Lerp(V0, V1, FracY);

				OutData[y * OutWidth + x] = static_cast<uint16>(FMath::RoundToInt(V));
			}
		}
	}

	// Global singleton
	static FExemplarTextureArray GExemplarTextureArray;

	FExemplarTextureArray& GetExemplarTextureArray()
	{
		return GExemplarTextureArray;
	}
}
