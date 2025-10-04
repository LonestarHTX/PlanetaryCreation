@echo off
echo ============================================
echo Running Milestone 3 Tests
echo ============================================
echo.

set EDITOR="C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
set PROJECT="C:\Users\Michael\Documents\Unreal Projects\PlanetaryCreation\PlanetaryCreation.uproject"

echo Running subdivision test...
%EDITOR% %PROJECT% -ExecCmds="Automation RunTests PlanetaryCreation.Milestone3.IcosphereSubdivision; Quit" -unattended -nop4 -nosplash -log

echo.
echo Running Voronoi mapping test...
%EDITOR% %PROJECT% -ExecCmds="Automation RunTests PlanetaryCreation.Milestone3.VoronoiMapping; Quit" -unattended -nop4 -nosplash -log

echo.
echo Running KD-tree performance test...
%EDITOR% %PROJECT% -ExecCmds="Automation RunTests PlanetaryCreation.Milestone3.KDTreePerformance; Quit" -unattended -nop4 -nosplash -log

echo.
echo ============================================
echo Tests complete! Check Saved/Logs/ for results
echo ============================================
pause
