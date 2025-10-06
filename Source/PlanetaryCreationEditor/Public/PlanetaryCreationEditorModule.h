#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FPlanetaryCreationEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    void HandlePostEngineInit();
    void RegisterMenus();
    void UnregisterMenus();
    void BindCommands();
    void ExtendToolbar(FToolBarBuilder& Builder);
    void HandleLevelEditorReady();

    TSharedRef<class SDockTab> HandleSpawnTectonicTab(const class FSpawnTabArgs& Args);
    void HandleOpenTectonicTool() const;
    void HandleStepSimulation() const;

    TSharedPtr<class FUICommandList> CommandList;
    TSharedPtr<class FTectonicSimulationController> SimulationController;
    TSharedPtr<class FExtender> LevelEditorToolbarExtender;
    FDelegateHandle OnPostEngineInitHandle;
};
