import unreal

svc = unreal.get_editor_subsystem(unreal.TectonicSimulationService)
if svc is None:
    raise RuntimeError("TectonicSimulationService subsystem unavailable")

unreal.SystemLibrary.execute_console_command(None, "r.PlanetaryCreation.AllowUnsafeHeightmapExport 1")
output = svc.export_heightmap_visualization(4096, 2048)
unreal.SystemLibrary.execute_console_command(None, "r.PlanetaryCreation.AllowUnsafeHeightmapExport 0")

if output:
    unreal.log(f"[HeightmapExport] Exported 4096x2048 to: {output}")
else:
    unreal.log_error("[HeightmapExport] Export failed")
