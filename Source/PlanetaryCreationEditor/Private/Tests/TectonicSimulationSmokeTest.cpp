#include "Utilities/PlanetaryCreationLogging.h"
#include "Misc/AutomationTest.h"
#include "Simulation/TectonicSimulationController.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTectonicSimulationSmokeTest, "PlanetaryCreation.Tectonics.Smoke", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTectonicSimulationSmokeTest::RunTest(const FString& Parameters)
{
    FTectonicSimulationController Controller;
    Controller.Initialize();
    Controller.StepSimulation(1);

    TestTrue(TEXT("Simulation time should advance"), Controller.GetCurrentTimeMy() > 0.0);

    Controller.Shutdown();

    UE_LOG(LogPlanetaryCreation, Display, TEXT("TODO: Capture performance metrics via Unreal Insights in Milestone 5."));
    return true;
}
