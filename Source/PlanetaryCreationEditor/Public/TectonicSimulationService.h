#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EditorSubsystem.h"
#include "TectonicSimulationService.generated.h"

/**
 * Editor-only subsystem that holds the canonical tectonic simulation state.
 * The state uses double precision so long-running editor sessions avoid drift.
 */
UCLASS()
class UTectonicSimulationService : public UEditorSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    /** Resets the simulation to the initial baseline. */
    void ResetSimulation();

    /** Advance the simulation by the requested number of steps (each 2 My). */
    void AdvanceSteps(int32 StepCount);

    /** Returns the accumulated tectonic time in mega-years. */
    double GetCurrentTimeMy() const { return CurrentTimeMy; }

    /** Accessor for the base sphere samples used to visualize placeholder geometry. */
    const TArray<FVector3d>& GetBaseSphereSamples() const { return BaseSphereSamples; }

private:
    void GenerateDefaultSphereSamples();

    double CurrentTimeMy = 0.0;
    TArray<FVector3d> BaseSphereSamples;
};
