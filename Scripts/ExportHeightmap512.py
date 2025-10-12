import unreal

service = unreal.get_editor_subsystem(unreal.TectonicSimulationService)
if service is None:
    raise RuntimeError("TectonicSimulationService subsystem unavailable")

unreal.SystemLibrary.execute_console_command(None, "r.PlanetaryCreation.AllowUnsafeHeightmapExport 1")
output = service.export_heightmap_visualization(512, 256)
unreal.SystemLibrary.execute_console_command(None, "r.PlanetaryCreation.AllowUnsafeHeightmapExport 0")

if output:
    unreal.log(f"[HeightmapExport] Exported 512x256 to: {output}")
else:
    unreal.log_error("[HeightmapExport] Export failed")
