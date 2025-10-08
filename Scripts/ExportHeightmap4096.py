import unreal

service = unreal.get_editor_subsystem(unreal.TectonicSimulationService)
if service is None:
    raise RuntimeError("TectonicSimulationService is unavailable")

output = service.export_heightmap_visualization(4096, 2048)
unreal.log(f"[HeightmapExport] wrote {output}")
