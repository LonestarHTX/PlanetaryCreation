#pragma once

#include "Framework/Commands/Commands.h"
#include "Styling/AppStyle.h"

class FPlanetaryCreationEditorCommands : public TCommands<FPlanetaryCreationEditorCommands>
{
public:
    FPlanetaryCreationEditorCommands()
        : TCommands<FPlanetaryCreationEditorCommands>(
            TEXT("PlanetaryCreationEditor"),
            NSLOCTEXT("PlanetaryCreation", "PlanetaryCreationEditor", "Planetary Creation"),
            NAME_None,
            FAppStyle::GetAppStyleSetName())
    {
    }

    virtual void RegisterCommands() override;

    TSharedPtr<FUICommandInfo> OpenTectonicTool;
    TSharedPtr<FUICommandInfo> StepSimulation;
};
