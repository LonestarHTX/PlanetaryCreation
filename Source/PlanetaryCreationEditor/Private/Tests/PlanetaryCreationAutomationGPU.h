#pragma once

#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "RHI.h"

namespace PlanetaryCreation::Automation
{
inline bool ShouldRunGPUAmplificationAutomation(FAutomationTestBase& Test, const TCHAR* ContextTag = TEXT(""))
{
#if WITH_EDITOR
    if (!GDynamicRHI || FCString::Stricmp(GDynamicRHI->GetName(), TEXT("NullDrv")) == 0)
    {
        Test.AddWarning(TEXT("Skipping Milestone 6 GPU automation on NullRHI."));
        return false;
    }

    bool bAllow = false;

    if (FParse::Param(FCommandLine::Get(), TEXT("AllowGPUAutomation")) ||
        FParse::Param(FCommandLine::Get(), TEXT("PlanetaryCreationAllowGPUAutomation")))
    {
        bAllow = true;
    }

    if (!bAllow)
    {
        if (IConsoleVariable* AllowVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PlanetaryCreation.AllowGPUAutomation")))
        {
            bAllow = AllowVar->GetInt() != 0;
        }
    }

    if (!bAllow)
    {
        const FString EnvValue = FPlatformMisc::GetEnvironmentVariable(TEXT("PLANETARYCREATION_ALLOW_GPU_AUTOMATION"));
        if (!EnvValue.IsEmpty() && EnvValue != TEXT("0"))
        {
            bAllow = true;
        }
    }

    if (!bAllow)
    {
        const FString Context = (ContextTag && *ContextTag)
            ? FString::Printf(TEXT(" [%s]"), ContextTag)
            : FString();
        Test.AddWarning(FString::Printf(
            TEXT("Skipping Milestone 6 GPU automation%s: enable with -AllowGPUAutomation or r.PlanetaryCreation.AllowGPUAutomation=1."),
            *Context));
        return false;
    }

    return true;
#else
    return false;
#endif
}

class FScopedGPUAmplificationOverride
{
public:
    explicit FScopedGPUAmplificationOverride(int32 ForcedValue)
    {
        if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PlanetaryCreation.UseGPUAmplification")))
        {
            OriginalValue = Var->GetInt();
            bHasVar = true;
            Var->Set(ForcedValue, ECVF_SetByCode);
        }
    }

    ~FScopedGPUAmplificationOverride()
    {
        if (bHasVar)
        {
            if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PlanetaryCreation.UseGPUAmplification")))
            {
                Var->Set(OriginalValue, ECVF_SetByCode);
            }
        }
    }

    FScopedGPUAmplificationOverride(const FScopedGPUAmplificationOverride&) = delete;
    FScopedGPUAmplificationOverride& operator=(const FScopedGPUAmplificationOverride&) = delete;

private:
    int32 OriginalValue = 0;
    bool bHasVar = false;
};

class FScopedStageBThrottleGuard
{
public:
    FScopedStageBThrottleGuard(FAutomationTestBase& InTest, float MinThrottleMs)
        : Test(InTest)
    {
        if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PlanetaryCreation.StageBThrottleMs")))
        {
            bHasVar = true;
            OriginalValue = Var->GetFloat();
            AppliedValue = OriginalValue;

            if (OriginalValue + KINDA_SMALL_NUMBER < MinThrottleMs)
            {
                Var->Set(MinThrottleMs, ECVF_SetByCode);
                AppliedValue = MinThrottleMs;
                bRaised = true;
                Test.AddInfo(FString::Printf(TEXT("Enforcing r.PlanetaryCreation.StageBThrottleMs=%.0f for GPU automation safety."), MinThrottleMs));
            }
        }
        else
        {
            Test.AddWarning(TEXT("StageB throttle CVar not found; skipping GPU automation to avoid unsafe run."));
            bShouldSkip = true;
        }
    }

    ~FScopedStageBThrottleGuard()
    {
        if (bHasVar)
        {
            if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PlanetaryCreation.StageBThrottleMs")))
            {
                Var->Set(OriginalValue, ECVF_SetByCode);
            }
        }
    }

    FScopedStageBThrottleGuard(const FScopedStageBThrottleGuard&) = delete;
    FScopedStageBThrottleGuard& operator=(const FScopedStageBThrottleGuard&) = delete;

    bool ShouldSkipTest() const
    {
        return bShouldSkip;
    }

    float GetAppliedValue() const
    {
        return AppliedValue;
    }

private:
    FAutomationTestBase& Test;
    float OriginalValue = 0.0f;
    float AppliedValue = 0.0f;
    bool bHasVar = false;
    bool bRaised = false;
    bool bShouldSkip = false;
};
} // namespace PlanetaryCreation::Automation
