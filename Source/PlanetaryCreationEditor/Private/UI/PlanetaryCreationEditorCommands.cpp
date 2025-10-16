#include "UI/PlanetaryCreationEditorCommands.h"

#include "InputCoreTypes.h"

#define LOCTEXT_NAMESPACE "FPlanetaryCreationEditorCommands"

void FPlanetaryCreationEditorCommands::RegisterCommands()
{
    UI_COMMAND(OpenTectonicTool, "Tectonic Tool", "Open the procedural tectonic planets tool panel.", EUserInterfaceActionType::Button, FInputGesture());
    UI_COMMAND(StepSimulation, "Step (2 My)", "Advance the tectonic simulation by one iteration (2 My).", EUserInterfaceActionType::Button, FInputGesture(EKeys::SpaceBar));
}

#undef LOCTEXT_NAMESPACE
