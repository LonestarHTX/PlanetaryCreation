#include "PlanetaryCreationLogging.h"
#include "Misc/AutomationTest.h"
#include "TectonicSimulationService.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContinentalBlendCacheTest,
    "PlanetaryCreation.Milestone6.ContinentalBlendCache",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContinentalBlendCacheTest::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
    UTectonicSimulationService* Service = GEditor->GetEditorSubsystem<UTectonicSimulationService>();
    TestNotNull(TEXT("TectonicSimulationService must exist"), Service);
    if (!Service)
    {
        return false;
    }

    FTectonicSimulationParameters Params;
    Params.Seed = 12345;
    Params.SubdivisionLevel = 0;
    Params.RenderSubdivisionLevel = 5;
    Params.bEnableOceanicAmplification = true;
    Params.bEnableOceanicDampening = true;
    Params.bEnableContinentalAmplification = true;
    Params.MinAmplificationLOD = 5;
    Service->SetParameters(Params);

    // Run a few simulation steps to populate Stage B data.
    Service->AdvanceSteps(3);

    // Force cache refresh and validate blend cache serial tracking.
    const TArray<FContinentalAmplificationCacheEntry>& CacheEntries = Service->GetContinentalAmplificationCacheEntries();
    const TArray<FContinentalBlendCache>& BlendCache = Service->GetContinentalAmplificationBlendCacheForTests();

    TestEqual(TEXT("Blend cache size matches cache entries"), BlendCache.Num(), CacheEntries.Num());

    int32 CachedIndex = INDEX_NONE;
    for (int32 Index = 0; Index < CacheEntries.Num(); ++Index)
    {
        if (CacheEntries[Index].bHasCachedData && CacheEntries[Index].ExemplarCount > 0)
        {
            CachedIndex = Index;
            break;
        }
    }

    TestTrue(TEXT("Found at least one cached continental vertex"), CachedIndex != INDEX_NONE);

    if (CachedIndex != INDEX_NONE)
    {
        const uint64 StageBSerial = Service->GetOceanicAmplificationDataSerial();
        const FContinentalBlendCache& BlendEntry = BlendCache[CachedIndex];

        TestEqual(TEXT("Blend cache serial matches current Stage B serial"), BlendEntry.CachedSerial, StageBSerial);
        TestTrue(TEXT("Blend cache retains reference mean"), BlendEntry.bHasReferenceMean);
    }

    return true;
#else
    TestTrue(TEXT("Continental blend cache test skipped (editor only)"), true);
    return true;
#endif
}
