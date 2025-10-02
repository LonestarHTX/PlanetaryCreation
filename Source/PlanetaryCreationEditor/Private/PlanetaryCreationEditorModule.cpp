#include "PlanetaryCreationEditorModule.h"

#include "PlanetaryCreationEditorCommands.h"
#include "SPTectonicToolPanel.h"
#include "TectonicSimulationController.h"
#include "TectonicSimulationService.h"

#include "Framework/Commands/UICommandList.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "LevelEditor.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

static const FName TectonicToolTabName(TEXT("TectonicTool"));

void FPlanetaryCreationEditorModule::StartupModule()
{
    SimulationController = MakeShared<FTectonicSimulationController>();
    SimulationController->Initialize();

    FPlanetaryCreationEditorCommands::Register();
    CommandList = MakeShared<FUICommandList>();
    BindCommands();

    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        TectonicToolTabName,
        FOnSpawnTab::CreateRaw(this, &FPlanetaryCreationEditorModule::HandleSpawnTectonicTab))
        .SetDisplayName(NSLOCTEXT("PlanetaryCreation", "TectonicToolTabTitle", "Tectonic Tool"))
        .SetTooltipText(NSLOCTEXT("PlanetaryCreation", "TectonicToolTabTooltip", "Control the procedural tectonic simulation."))
        .SetGroup(WorkspaceMenuStructure::GetMenuStructure().GetLevelEditorCategory());

    RegisterMenus();
}

void FPlanetaryCreationEditorModule::ShutdownModule()
{
    UnregisterMenus();

    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TectonicToolTabName);

    CommandList.Reset();

    if (SimulationController.IsValid())
    {
        SimulationController->Shutdown();
        SimulationController.Reset();
    }

    FPlanetaryCreationEditorCommands::Unregister();
}

void FPlanetaryCreationEditorModule::RegisterMenus()
{
    if (!CommandList.IsValid())
    {
        return;
    }

    FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");

    LevelEditorToolbarExtender = MakeShared<FExtender>();
    LevelEditorToolbarExtender->AddToolBarExtension(
        "Settings",
        EExtensionHook::After,
        CommandList,
        FToolBarExtensionDelegate::CreateRaw(this, &FPlanetaryCreationEditorModule::ExtendToolbar));

    LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(LevelEditorToolbarExtender);

    FGlobalTabmanager::Get()->TryInvokeTab(TectonicToolTabName);
}

void FPlanetaryCreationEditorModule::UnregisterMenus()
{
    if (LevelEditorToolbarExtender.IsValid())
    {
        if (FModuleManager::Get().IsModuleLoaded("LevelEditor"))
        {
            FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
            LevelEditorModule.GetToolBarExtensibilityManager()->RemoveExtender(LevelEditorToolbarExtender);
        }

        LevelEditorToolbarExtender.Reset();
    }
}

void FPlanetaryCreationEditorModule::BindCommands()
{
    if (!CommandList.IsValid())
    {
        return;
    }

    const FPlanetaryCreationEditorCommands& Commands = FPlanetaryCreationEditorCommands::Get();

    CommandList->MapAction(
        Commands.OpenTectonicTool,
        FExecuteAction::CreateRaw(this, &FPlanetaryCreationEditorModule::HandleOpenTectonicTool));

    CommandList->MapAction(
        Commands.StepSimulation,
        FExecuteAction::CreateRaw(this, &FPlanetaryCreationEditorModule::HandleStepSimulation));
}

void FPlanetaryCreationEditorModule::ExtendToolbar(FToolBarBuilder& Builder)
{
    Builder.BeginSection("PlanetaryCreation");
    Builder.AddToolBarButton(FPlanetaryCreationEditorCommands::Get().OpenTectonicTool);
    Builder.AddToolBarButton(FPlanetaryCreationEditorCommands::Get().StepSimulation);
    Builder.EndSection();
}

TSharedRef<SDockTab> FPlanetaryCreationEditorModule::HandleSpawnTectonicTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab)
        .Label(NSLOCTEXT("PlanetaryCreation", "TectonicToolTabTitle", "Tectonic Tool"))
        [
            SNew(SPTectonicToolPanel)
            .Controller(SimulationController)
        ];
}

void FPlanetaryCreationEditorModule::HandleOpenTectonicTool() const
{
    FGlobalTabmanager::Get()->TryInvokeTab(TectonicToolTabName);
}

void FPlanetaryCreationEditorModule::HandleStepSimulation() const
{
    if (SimulationController.IsValid())
    {
        SimulationController->StepSimulation(1);
    }
}

IMPLEMENT_MODULE(FPlanetaryCreationEditorModule, PlanetaryCreationEditor);
