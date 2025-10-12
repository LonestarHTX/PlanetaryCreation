import unreal

# Get simulation service
svc = unreal.get_editor_subsystem(unreal.TectonicSimulationService)

# Enable unsafe export via console variable
unreal.SystemLibrary.execute_console_command(None, "r.PlanetaryCreation.AllowUnsafeHeightmapExport 1")
output = svc.export_heightmap_visualization(1024, 512)
unreal.SystemLibrary.execute_console_command(None, "r.PlanetaryCreation.AllowUnsafeHeightmapExport 0")

if output:
    unreal.log(f"[HeightmapExport] Exported 1024x512 to: {output}")
else:
    unreal.log_error("[HeightmapExport] Export failed - unsafe export may be disabled in C++")
