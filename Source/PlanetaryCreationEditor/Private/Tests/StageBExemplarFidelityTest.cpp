#include "Misc/AutomationTest.h"

#include "Dom/JsonObject.h"
#include "Export/HeightmapSampling.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonSerializer.h"
#include "Simulation/TectonicSimulationService.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "RHI.h"
#include "Editor.h"
#include "Math/UnrealMathUtility.h"

namespace
{
	struct FExemplarMetadataRecord
	{
		FString Png16Path;
		double ElevationMin = 0.0;
		double ElevationMax = 0.0;
		double West = 0.0;
		double South = 0.0;
		double East = 0.0;
		double North = 0.0;
		int32 Width = 0;
		int32 Height = 0;
	};

	bool LoadExemplarMetadata(const FString& TileId, FExemplarMetadataRecord& OutRecord, FAutomationTestBase& Test)
	{
		const FString JsonPath = FPaths::ProjectContentDir() / TEXT("PlanetaryCreation/Exemplars/ExemplarLibrary.json");
		FString JsonString;
		if (!FFileHelper::LoadFileToString(JsonString, *JsonPath))
		{
			Test.AddError(FString::Printf(TEXT("Failed to load exemplar library: %s"), *JsonPath));
			return false;
		}

		TSharedPtr<FJsonObject> RootObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
		if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
		{
			Test.AddError(TEXT("Failed to parse exemplar library JSON."));
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* ExemplarsArray = nullptr;
		if (!RootObject->TryGetArrayField(TEXT("exemplars"), ExemplarsArray))
		{
			Test.AddError(TEXT("Exemplar library missing 'exemplars' array."));
			return false;
		}

		for (const TSharedPtr<FJsonValue>& Value : *ExemplarsArray)
		{
			const TSharedPtr<FJsonObject> Obj = Value->AsObject();
			if (!Obj.IsValid())
			{
				continue;
			}

			if (Obj->GetStringField(TEXT("id")) == TileId)
			{
				OutRecord.Png16Path = Obj->GetStringField(TEXT("png16_path"));
				OutRecord.ElevationMin = Obj->GetNumberField(TEXT("elevation_min_m"));
				OutRecord.ElevationMax = Obj->GetNumberField(TEXT("elevation_max_m"));

				const TSharedPtr<FJsonObject> Bounds = Obj->GetObjectField(TEXT("bounds"));
				OutRecord.West = Bounds->GetNumberField(TEXT("west"));
				OutRecord.South = Bounds->GetNumberField(TEXT("south"));
				OutRecord.East = Bounds->GetNumberField(TEXT("east"));
				OutRecord.North = Bounds->GetNumberField(TEXT("north"));

				const TSharedPtr<FJsonObject> Resolution = Obj->GetObjectField(TEXT("resolution"));
				OutRecord.Width = static_cast<int32>(Resolution->GetNumberField(TEXT("width_px")));
				OutRecord.Height = static_cast<int32>(Resolution->GetNumberField(TEXT("height_px")));
				return true;
			}
		}

		Test.AddError(FString::Printf(TEXT("Tile ID %s not found in exemplar library."), *TileId));
		return false;
	}

	bool LoadPng16Heights(const FString& RelativePath, int32 Width, int32 Height, double ElevationMin, double ElevationMax, TArray<double>& OutHeights, FAutomationTestBase& Test)
	{
		const FString FullPath = FPaths::ProjectContentDir() / RelativePath;
		TArray<uint8> CompressedData;
		if (!FFileHelper::LoadFileToArray(CompressedData, *FullPath))
		{
			Test.AddError(FString::Printf(TEXT("Failed to load PNG16 exemplar: %s"), *FullPath));
			return false;
		}

		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
		TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
		if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(CompressedData.GetData(), CompressedData.Num()))
		{
			Test.AddError(TEXT("Failed to parse exemplar PNG16 data."));
			return false;
		}

		TArray<uint8> RawData;
		if (!ImageWrapper->GetRaw(ERGBFormat::Gray, 16, RawData))
		{
			Test.AddError(TEXT("Failed to extract raw PNG16 channel data."));
			return false;
		}

		if (RawData.Num() != Width * Height * sizeof(uint16))
		{
			Test.AddError(TEXT("PNG16 raw data size unexpected."));
			return false;
		}

		const uint16* Samples = reinterpret_cast<const uint16*>(RawData.GetData());
		OutHeights.SetNum(Width * Height);
		const double Range = ElevationMax - ElevationMin;
		for (int32 Index = 0; Index < Width * Height; ++Index)
		{
			const double Normalized = static_cast<double>(Samples[Index]) / 65535.0;
			OutHeights[Index] = ElevationMin + Range * Normalized;
		}
		return true;
	}

	void BuildLonLatCenters(const FExemplarMetadataRecord& Record, TArray<double>& OutLon, TArray<double>& OutLat)
	{
		OutLon.SetNum(Record.Width);
		OutLat.SetNum(Record.Height);

		const double LonStep = (Record.East - Record.West) / static_cast<double>(Record.Width);
		const double LatStep = (Record.North - Record.South) / static_cast<double>(Record.Height);

		for (int32 X = 0; X < Record.Width; ++X)
		{
			const double EdgeWest = Record.West + LonStep * static_cast<double>(X);
			OutLon[X] = EdgeWest + LonStep * 0.5;
		}

		for (int32 Y = 0; Y < Record.Height; ++Y)
		{
			const double EdgeNorth = Record.North - LatStep * static_cast<double>(Y);
			OutLat[Y] = EdgeNorth - LatStep * 0.5;
		}
	}

	double SampleStageBAtUV(FHeightmapSampler& Sampler, const FVector2d& UV, bool& bOutHit)
	{
		FHeightmapSampler::FSampleInfo Info;
		const double Elev = Sampler.SampleElevationAtUV(UV, &Info);
		bOutHit = Info.bHit;
		return Elev;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStageBExemplarFidelityTest, "PlanetaryCreation.StageB.ExemplarFidelity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStageBExemplarFidelityTest::RunTest(const FString& Parameters)
{
	const FString TileId = TEXT("O01");
	constexpr double MeanDiffToleranceMeters = 50.0;
	constexpr double InteriorDiffToleranceMeters = 100.0;
	constexpr double SpikeWarningThresholdMeters = 750.0;

	FExemplarMetadataRecord Metadata;
	if (!LoadExemplarMetadata(TileId, Metadata, *this))
	{
		return false;
	}

	TArray<double> ExemplarHeights;
	if (!LoadPng16Heights(Metadata.Png16Path, Metadata.Width, Metadata.Height, Metadata.ElevationMin, Metadata.ElevationMax, ExemplarHeights, *this))
	{
		return false;
	}

	TArray<double> LonCenters;
	TArray<double> LatCenters;
	BuildLonLatCenters(Metadata, LonCenters, LatCenters);

	IConsoleManager& ConsoleManager = IConsoleManager::Get();
	if (IConsoleVariable* ForceId = ConsoleManager.FindConsoleVariable(TEXT("r.PlanetaryCreation.StageBForceExemplarId")))
	{
		ForceId->Set(*TileId, ECVF_SetByCode);
	}
	if (IConsoleVariable* DisableOffset = ConsoleManager.FindConsoleVariable(TEXT("r.PlanetaryCreation.StageBDisableRandomOffset")))
	{
		DisableOffset->Set(1, ECVF_SetByCode);
	}
	if (IConsoleVariable* SkipCpu = ConsoleManager.FindConsoleVariable(TEXT("r.PlanetaryCreation.SkipCPUAmplification")))
	{
		SkipCpu->Set(0, ECVF_SetByCode);
	}
	if (IConsoleVariable* UseGpu = ConsoleManager.FindConsoleVariable(TEXT("r.PlanetaryCreation.UseGPUAmplification")))
	{
		UseGpu->Set(0, ECVF_SetByCode);
	}

	const bool bNullRHIActive = (GDynamicRHI == nullptr) ||
		(FCString::Stristr(GDynamicRHI->GetName(), TEXT("Null")) != nullptr);
	if (!bNullRHIActive)
	{
		AddError(TEXT("StageBExemplarFidelityTest requires -NullRHI; relaunch automation with the NullRHI switch."));
		return false;
	}

	FPlatformMisc::SetEnvironmentVar(TEXT("PLANETARY_STAGEB_FORCE_EXEMPLAR"), *TileId);
	FPlatformMisc::SetEnvironmentVar(TEXT("PLANETARY_STAGEB_DISABLE_RANDOM_OFFSET"), TEXT("1"));
	FPlatformMisc::SetEnvironmentVar(TEXT("PLANETARY_STAGEB_FORCE_CPU"), TEXT("1"));

	UTectonicSimulationService* Service = GEditor ? GEditor->GetEditorSubsystem<UTectonicSimulationService>() : nullptr;
	if (!Service)
	{
		AddError(TEXT("Failed to acquire UTectonicSimulationService subsystem."));
		return false;
	}

	Service->ForceStageBAmplificationRebuild(TEXT("Automation.ExemplarFidelity"));
	if (!Service->IsStageBAmplificationReady())
	{
		AddError(TEXT("Stage B amplification not ready after rebuild."));
		return false;
	}

	FHeightmapSampler Sampler(*Service);
	if (!Sampler.IsValid())
	{
		AddError(TEXT("Failed to initialize heightmap sampler."));
		return false;
	}

	TArray<double> StageHeights;
	StageHeights.SetNum(Metadata.Width * Metadata.Height);
	TArray<uint8> SampleHitMask;
	SampleHitMask.Init(0, Metadata.Width * Metadata.Height);

	int32 MissingSamples = 0;
	for (int32 Y = 0; Y < Metadata.Height; ++Y)
	{
		const double Lat = LatCenters[Y];
		for (int32 X = 0; X < Metadata.Width; ++X)
		{
			const double Lon = LonCenters[X];
			const double U = FMath::Frac(0.5 + Lon / 360.0);
			const double V = 0.5 - (Lat / 180.0);
			bool bHit = false;
			const double Elev = SampleStageBAtUV(Sampler, FVector2d(U, V), bHit);
			if (!bHit)
			{
				++MissingSamples;
			}
			const int32 LinearIndex = Y * Metadata.Width + X;
			StageHeights[LinearIndex] = Elev;
			if (bHit)
			{
				SampleHitMask[LinearIndex] = 1;
			}
		}
	}

	if (MissingSamples > 0)
	{
		AddWarning(FString::Printf(TEXT("Stage B sampling missed %d pixels within exemplar bounds."), MissingSamples));
	}

	const double LonRange = Metadata.East - Metadata.West;
	const double LatRange = Metadata.North - Metadata.South;
	const double LonStep = (LonCenters.Num() >= 2) ? FMath::Abs(LonCenters[1] - LonCenters[0]) : 0.0;
	const double LatStep = (LatCenters.Num() >= 2) ? FMath::Abs(LatCenters[1] - LatCenters[0]) : 0.0;
	const double LonPadding = FMath::Max(LonStep * 2.0, FMath::Abs(LonRange) * 0.05);
	const double LatPadding = FMath::Max(LatStep * 2.0, FMath::Abs(LatRange) * 0.05);
	const double LonMin = FMath::Min(Metadata.West, Metadata.East);
	const double LonMax = FMath::Max(Metadata.West, Metadata.East);
	const double LatMin = FMath::Min(Metadata.South, Metadata.North);
	const double LatMax = FMath::Max(Metadata.South, Metadata.North);
	const int32 IndexMarginX = FMath::Max(1, FMath::CeilToInt(static_cast<double>(Metadata.Width) * 0.10));
	const int32 IndexMarginY = FMath::Max(1, FMath::CeilToInt(static_cast<double>(Metadata.Height) * 0.10));
	const bool bApplyIndexMarginX = Metadata.Width > IndexMarginX * 2;
	const bool bApplyIndexMarginY = Metadata.Height > IndexMarginY * 2;

	TArray<uint8> InteriorMask;
	InteriorMask.Init(0, StageHeights.Num());

	for (int32 Y = 0; Y < Metadata.Height; ++Y)
	{
		const double Lat = LatCenters[Y];
		const bool bLatInterior = (Lat <= LatMax - LatPadding) && (Lat >= LatMin + LatPadding);
		const bool bIndexInteriorY = !bApplyIndexMarginY || (Y >= IndexMarginY && Y < Metadata.Height - IndexMarginY);
		for (int32 X = 0; X < Metadata.Width; ++X)
		{
			const double Lon = LonCenters[X];
			const bool bLonInterior = (Lon >= LonMin + LonPadding) && (Lon <= LonMax - LonPadding);
			const bool bIndexInteriorX = !bApplyIndexMarginX || (X >= IndexMarginX && X < Metadata.Width - IndexMarginX);
			const bool bInterior = bLatInterior && bLonInterior && bIndexInteriorX && bIndexInteriorY;
			InteriorMask[Y * Metadata.Width + X] = bInterior ? 1 : 0;
		}
	}

	double SumDiff = 0.0;
	double MaxAbsDiff = 0.0;
	double MaxAbsDiffLon = 0.0;
	double MaxAbsDiffLat = 0.0;
	int32 SampleCount = 0;

	double InteriorMaxAbsDiff = 0.0;
	double InteriorMaxLon = 0.0;
	double InteriorMaxLat = 0.0;
	int32 InteriorSampleCount = 0;

	for (int32 Index = 0; Index < StageHeights.Num(); ++Index)
	{
		if (SampleHitMask[Index] == 0)
		{
			continue;
		}

		const double StageValue = StageHeights[Index];
		const double ExemplarValue = ExemplarHeights[Index];
		if (!FMath::IsFinite(StageValue) || !FMath::IsFinite(ExemplarValue))
		{
			continue;
		}

		const double Delta = StageValue - ExemplarValue;
		const double AbsDelta = FMath::Abs(Delta);
		SumDiff += Delta;
		if (AbsDelta > MaxAbsDiff)
		{
			MaxAbsDiff = AbsDelta;
			const int32 Y = Index / Metadata.Width;
			const int32 X = Index % Metadata.Width;
			MaxAbsDiffLon = LonCenters[X];
			MaxAbsDiffLat = LatCenters[Y];
		}
		++SampleCount;

		if (InteriorMask[Index] != 0)
		{
			++InteriorSampleCount;
			if (AbsDelta > InteriorMaxAbsDiff)
			{
				InteriorMaxAbsDiff = AbsDelta;
				const int32 Y = Index / Metadata.Width;
				const int32 X = Index % Metadata.Width;
				InteriorMaxLon = LonCenters[X];
				InteriorMaxLat = LatCenters[Y];
			}
		}
	}

	if (SampleCount == 0)
	{
		AddError(TEXT("No valid samples were collected for exemplar fidelity comparison."));
		return false;
	}

	const double MeanDiff = SumDiff / static_cast<double>(SampleCount);
	AddInfo(FString::Printf(TEXT("Exemplar %s: mean delta %.2f m over %d samples."), *TileId, MeanDiff, SampleCount));
	AddInfo(FString::Printf(TEXT("Exemplar %s: max absolute delta %.2f m at Lon=%.3f Lat=%.3f."),
		*TileId, MaxAbsDiff, MaxAbsDiffLon, MaxAbsDiffLat));

	if (InteriorSampleCount > 0)
	{
		AddInfo(FString::Printf(TEXT("Interior samples: %d (%.2f%%) max delta %.2f m at Lon=%.3f Lat=%.3f."),
			InteriorSampleCount,
			(static_cast<double>(InteriorSampleCount) / static_cast<double>(SampleCount)) * 100.0,
			InteriorMaxAbsDiff,
			InteriorMaxLon,
			InteriorMaxLat));
	}
	else
	{
		AddWarning(TEXT("Interior mask discarded all samples; perimeter-only guardrails inactive."));
	}

	bool bHasFailures = false;
	if (FMath::Abs(MeanDiff) > MeanDiffToleranceMeters)
	{
		AddError(FString::Printf(TEXT("Mean delta %.2f m exceeds ±%.2f m guardrail."),
			MeanDiff,
			MeanDiffToleranceMeters));
		bHasFailures = true;
	}

	if (InteriorSampleCount > 0 && InteriorMaxAbsDiff > InteriorDiffToleranceMeters)
	{
		AddError(FString::Printf(TEXT("Interior max delta %.2f m exceeds %.2f m guardrail."),
			InteriorMaxAbsDiff,
			InteriorDiffToleranceMeters));
		bHasFailures = true;
	}

	if (MaxAbsDiff > SpikeWarningThresholdMeters)
	{
		AddWarning(FString::Printf(TEXT("Worst-case perimeter spike %.2f m exceeds %.2f m warning threshold (allowed for now)."),
			MaxAbsDiff,
			SpikeWarningThresholdMeters));
	}

	return !bHasFailures;
}
