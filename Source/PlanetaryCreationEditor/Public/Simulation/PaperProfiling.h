#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"

// Centralized query for Paper profiling toggle
inline bool IsPaperProfilingEnabled()
{
    static IConsoleVariable* ProfilingVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PaperProfiling"));
    return ProfilingVar ? (ProfilingVar->GetInt() != 0) : true;
}

