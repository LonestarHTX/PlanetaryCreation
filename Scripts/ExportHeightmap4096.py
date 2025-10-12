import argparse
import shutil
import sys
from pathlib import Path

import unreal


def parse_arguments(argv) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Export the current tectonic heightmap visualization to a PNG."
    )
    parser.add_argument("--width", type=int, default=4096, help="Output width in pixels (default: 4096).")
    parser.add_argument("--height", type=int, default=2048, help="Output height in pixels (default: 2048).")
    parser.add_argument(
        "--output",
        type=str,
        default="",
        help="Optional path where the generated image will be copied (relative paths resolve against the project directory).",
    )

    palette_group = parser.add_mutually_exclusive_group()
    palette_group.add_argument(
        "--normalized",
        action="store_true",
        help="Force the normalized (min/max stretched) palette."
    )
    palette_group.add_argument(
        "--absolute",
        action="store_true",
        help="Force the absolute hypsometric palette."
    )
    parser.add_argument(
        "--force-large-export",
        action="store_true",
        help="Acknowledge that requested dimensions exceed the safety baseline (512x256) and should proceed."
    )

    return parser.parse_args(argv)


def resolve_output_path(raw_path: str) -> Path:
    path = Path(raw_path).expanduser()
    if path.is_absolute():
        return path

    project_dir = Path(unreal.Paths.project_dir())
    return (project_dir / path).resolve()


def log_stage_b_warning(description: str, reason_label: str | None) -> None:
    message = "[HeightmapExport] Stage B not ready"
    if reason_label:
        message += f" ({reason_label})"
    if description:
        message += f": {description}"
    message += " Falling back to baseline elevations."
    unreal.log_warning(message)


def main(argv) -> None:
    args = parse_arguments(argv)

    baseline_width = 512
    baseline_height = 256
    exceeds_baseline = args.width > baseline_width or args.height > baseline_height
    if exceeds_baseline and not args.force_large_export:
        unreal.log_error(
            "[HeightmapExport] Requested dimensions exceed the 512x256 safety baseline. "
            "Re-run with --width 512 --height 256 first, then retry with --force-large-export."
        )
        return

    service = unreal.get_editor_subsystem(unreal.TectonicSimulationService)
    if service is None:
        raise RuntimeError("TectonicSimulationService is unavailable")

    target_mode = service.get_heightmap_palette_mode()
    if args.normalized:
        target_mode = unreal.EHeightmapPaletteMode.NORMALIZED_RANGE
    elif args.absolute:
        target_mode = unreal.EHeightmapPaletteMode.ABSOLUTE_HYPSOMETRIC

    stage_b_ready = True
    if hasattr(service, "is_stage_b_amplification_ready"):
        stage_b_ready = service.is_stage_b_amplification_ready()
    else:
        unreal.log_warning("[HeightmapExport] Stage B readiness accessor unavailable; assuming ready state.")

    if not stage_b_ready:
        description = ""
        reason_label = None

        if hasattr(service, "get_stage_b_amplification_ready_description"):
            description = service.get_stage_b_amplification_ready_description()

        if hasattr(service, "get_stage_b_amplification_not_ready_reason"):
            try:
                reason = service.get_stage_b_amplification_not_ready_reason()
                reason_label = getattr(reason, "name", str(reason))
            except Exception as exc:  # pragma: no cover - defensive safeguard
                unreal.log_warning(f"[HeightmapExport] Unable to query Stage B reason: {exc}")

        log_stage_b_warning(description, reason_label)

    if hasattr(service, "set_heightmap_palette_mode"):
        service.set_heightmap_palette_mode(target_mode)
    else:
        unreal.log_warning("[HeightmapExport] Palette setter unavailable; keeping current mode.")

    unreal.log(f"[HeightmapExport] palette mode: {target_mode.name}")

    output = service.export_heightmap_visualization(args.width, args.height)
    if not output:
        unreal.log_error("[HeightmapExport] Export failed; see log for details.")
        return

    unreal.log(f"[HeightmapExport] wrote {output}")

    if args.output:
        destination = resolve_output_path(args.output)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(output, destination)
        unreal.log(f"[HeightmapExport] copied to {destination}")


if __name__ == "__main__":
    main(sys.argv[1:])
