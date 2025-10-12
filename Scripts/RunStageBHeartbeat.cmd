@echo off
setlocal
cd /d "C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation"
REM CRITICAL: CVars must use -SetCVar, NOT -ExecCmds. Do NOT include Quit when using -TestExit.
"C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" ^
  "C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\PlanetaryCreation.uproject" ^
  -SetCVar=r.PlanetaryCreation.StageBThrottleMs=50,r.PlanetaryCreation.UseGPUAmplification=0,r.PlanetaryCreation.StageBProfiling=1 ^
  -ExecCmds="Automation RunTests PlanetaryCreation.Heightmap.SampleInterpolation" ^
  -TestExit="Automation Test Queue Empty" ^
  -unattended -nop4 -nosplash -NullRHI -log
endlocal
