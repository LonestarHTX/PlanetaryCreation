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
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ShaderCore.h"

static const FName TectonicToolTabName(TEXT("TectonicTool"));

void FPlanetaryCreationEditorModule::StartupModule()
{
    // Milestone 6: Register shader directory for GPU compute shaders
    // CRITICAL: Must be ABSOLUTE path, not relative, and registered even during automation runs
    const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    const FString ShaderDirectory = FPaths::Combine(ProjectDir, TEXT("Source/PlanetaryCreationEditor/Shaders"));

    UE_LOG(LogTemp, Log, TEXT("[M6 GPU] Registering shader directory: %s"), *ShaderDirectory);
    AddShaderSourceDirectoryMapping(TEXT("/Plugin/PlanetaryCreation"), ShaderDirectory);

    const FString CmdLine(FCommandLine::Get());
    const bool bAutomationOrCmdlet = IsRunningCommandlet()
        || FApp::IsUnattended()
        || GIsAutomationTesting
        || CmdLine.Contains(TEXT("-ExecCmds="))
        || CmdLine.Contains(TEXT("-run=Automation"));

    if (bAutomationOrCmdlet)
    {
        UE_LOG(LogTemp, Log, TEXT("[M6 GPU] Skipping UI registration for automation/cmdlet run"));
        return; // Skip editor-only UI registration for commandlets/automation runs
    }

    SimulationController = MakeShared<FTectonicSimulationController>();
    SimulationController->Initialize();

    FPlanetaryCreationEditorCommands::Register();
    CommandList = MakeShared<FUICommandList>();
    BindCommands();

    const IWorkspaceMenuStructure& MenuStructure = WorkspaceMenu::GetMenuStructure();

    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        TectonicToolTabName,
        FOnSpawnTab::CreateRaw(this, &FPlanetaryCreationEditorModule::HandleSpawnTectonicTab))
        .SetDisplayName(NSLOCTEXT("PlanetaryCreation", "TectonicToolTabTitle", "Tectonic Tool"))
        .SetTooltipText(NSLOCTEXT("PlanetaryCreation", "TectonicToolTabTooltip", "Control the procedural tectonic simulation."))
        .SetGroup(MenuStructure.GetLevelEditorCategory());

    RegisterMenus();
}

void FPlanetaryCreationEditorModule::ShutdownModule()
{
    const FString CmdLine(FCommandLine::Get());
    const bool bAutomationOrCmdlet = IsRunningCommandlet()
        || FApp::IsUnattended()
        || GIsAutomationTesting
        || CmdLine.Contains(TEXT("-ExecCmds="))
        || CmdLine.Contains(TEXT("-run=Automation"));

    if (bAutomationOrCmdlet)
    {
        return;
    }

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

    // Note: Tab auto-invoke removed - user opens via Window → Tectonic Tool or toolbar button
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
