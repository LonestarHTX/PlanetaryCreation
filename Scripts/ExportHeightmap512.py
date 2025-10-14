import os
import unreal


def ensure_forced_exemplar_env() -> None:
    os.environ.setdefault("PLANETARY_STAGEB_FORCE_CPU", "1")
    os.environ.setdefault("PLANETARY_STAGEB_FORCE_EXEMPLAR", "O01")
    os.environ.setdefault("PLANETARY_STAGEB_DISABLE_RANDOM_OFFSET", "1")


def require_null_rhi() -> None:
    command_line = unreal.SystemLibrary.get_command_line()
    if "-NullRHI" in command_line or "-nullrhi" in command_line:
        return
    unreal.log_error("[HeightmapExport512] NullRHI is required for forced exemplar exports. Relaunch with -NullRHI.")
    raise SystemExit(1)


def main() -> int:
    service = unreal.get_editor_subsystem(unreal.TectonicSimulationService)
    if service is None:
        unreal.log_error("[HeightmapExport512] TectonicSimulationService subsystem unavailable")
        return 1

    ensure_forced_exemplar_env()
    require_null_rhi()

    render_lod_env = os.environ.get("PLANETARY_STAGEB_RENDER_LOD")
    if render_lod_env and hasattr(service, "set_render_subdivision_level"):
        try:
            render_lod_value = int(render_lod_env)
            unreal.log(f"[HeightmapExport512] Setting render subdivision level to {render_lod_value}")
            service.set_render_subdivision_level(render_lod_value)
        except ValueError:
            unreal.log_warning(f"[HeightmapExport512] Ignoring invalid PLANETARY_STAGEB_RENDER_LOD='{render_lod_env}'")

    unreal.SystemLibrary.execute_console_command(None, "r.PlanetaryCreation.AllowUnsafeHeightmapExport 1")
    try:
        output = service.export_heightmap_visualization(512, 256)
    finally:
        unreal.SystemLibrary.execute_console_command(None, "r.PlanetaryCreation.AllowUnsafeHeightmapExport 0")

    if output:
        unreal.log(f"[HeightmapExport] Exported 512x256 to: {output}")
        return 0

    unreal.log_error("[HeightmapExport] Export failed")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
