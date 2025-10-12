import unreal


BASELINE_WIDTH = 512
BASELINE_HEIGHT = 256


def resolve_stage_b_state(service: unreal.TectonicSimulationService) -> tuple[bool, str]:
    ready = True
    reason_label = "Unknown"
    description = ""

    if hasattr(service, "is_stage_b_amplification_ready"):
        ready = bool(service.is_stage_b_amplification_ready())
    if hasattr(service, "get_stage_b_amplification_not_ready_reason"):
        try:
            reason = service.get_stage_b_amplification_not_ready_reason()
            reason_label = getattr(reason, "name", str(reason))
        except Exception as exc:  # pragma: no cover - defensive guard
            unreal.log_warning(f"[HeightmapTelemetry] Unable to query Stage B reason: {exc}")
    if hasattr(service, "get_stage_b_amplification_ready_description"):
        try:
            description = str(service.get_stage_b_amplification_ready_description())
        except Exception as exc:  # pragma: no cover - defensive guard
            unreal.log_warning(f"[HeightmapTelemetry] Unable to query Stage B description: {exc}")

    return ready, f"{reason_label}: {description}" if description else reason_label


def export_heightmap(service: unreal.TectonicSimulationService, width: int, height: int, *, allow_large: bool) -> bool:
    exceeds_baseline = width > BASELINE_WIDTH or height > BASELINE_HEIGHT
    if exceeds_baseline and not allow_large:
        unreal.log_error(
            f"[HeightmapTelemetry] Requested {width}x{height} exceeds the {BASELINE_WIDTH}x{BASELINE_HEIGHT} safety baseline."
            " Set allow_large=True to proceed."
        )
        return False

    unreal.log(f"[HeightmapTelemetry] Starting export {width}x{height} (allow_large={allow_large})")
    output_path = service.export_heightmap_visualization(width, height)
    if not output_path:
        unreal.log_error(f"[HeightmapTelemetry] Export failed for {width}x{height}; aborting sequence.")
        return False

    unreal.log(f"[HeightmapTelemetry] Export complete {width}x{height}: {output_path}")
    return True


def main() -> None:
    service = unreal.get_editor_subsystem(unreal.TectonicSimulationService)
    if service is None:
        raise RuntimeError("TectonicSimulationService is unavailable; ensure the editor subsystem is registered.")

    ready, reason = resolve_stage_b_state(service)
    unreal.log(
        f"[HeightmapTelemetry] Stage B ready={ready} ({reason})"
    )

    sequence: list[tuple[int, int, bool]] = [
        (BASELINE_WIDTH, BASELINE_HEIGHT, False),
        (1024, 512, True),
        (2048, 1024, True),
    ]

    for width, height, allow_large in sequence:
        if not export_heightmap(service, width, height, allow_large=allow_large):
            unreal.log_error(f"[HeightmapTelemetry] Halting telemetry pass at {width}x{height}.")
            break


if __name__ == "__main__":
    main()
