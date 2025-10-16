import argparse
import csv
import json
import os
from pathlib import Path
from typing import Any

try:
    import unreal
except ImportError:  # pragma: no cover - script is intended for Unreal Python
    unreal = None  # type: ignore


def log_info(message: str) -> None:
    if unreal:
        unreal.log(message)
    else:
        print(message)


def log_error(message: str) -> None:
    if unreal:
        unreal.log_error(message)
    else:
        print(f"ERROR: {message}")


def resolve_project_dir() -> Path:
    if unreal:
        return Path(unreal.SystemLibrary.get_project_directory())
    return Path(__file__).resolve().parents[1]


def load_bounds(library_path: Path, tile_id: str) -> dict[str, Any]:
    with library_path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    for entry in data.get("exemplars", []):
        if entry.get("id") == tile_id:
            bounds = entry.get("bounds")
            if not bounds:
                raise RuntimeError(f"Tile {tile_id} missing bounds payload in library {library_path}")
            return bounds
    raise RuntimeError(f"Tile {tile_id} not found in library {library_path}")


def gather_samples(tile_id: str, bounds: dict[str, Any], render_lod: int | None) -> list[dict[str, float]]:
    if not unreal:
        raise RuntimeError("Unreal Python runtime is required to gather Stage B samples.")

    service = unreal.get_editor_subsystem(unreal.TectonicSimulationService)
    if service is None:
        raise RuntimeError("TectonicSimulationService subsystem unavailable.")

    original_skip: bool | None = None
    if hasattr(service, "is_skipping_cpu_amplification"):
        try:
            original_skip = bool(service.is_skipping_cpu_amplification())
        except Exception:  # pragma: no cover - defensive logging
            original_skip = None

    if hasattr(service, "set_skip_cpu_amplification"):
        service.set_skip_cpu_amplification(False)

    if render_lod is not None and hasattr(service, "set_render_subdivision_level"):
        log_info(f"[VertexSlice] Setting render subdivision level to {render_lod}")
        service.set_render_subdivision_level(int(render_lod))

    unreal.SystemLibrary.execute_console_command(None, "r.PlanetaryCreation.SkipCPUAmplification 0")
    if hasattr(service, "force_stage_b_amplification_rebuild"):
        service.force_stage_b_amplification_rebuild("VertexSlice")

    if hasattr(service, "is_stage_b_amplification_ready"):
        try:
            ready = bool(service.is_stage_b_amplification_ready())
        except Exception:  # pragma: no cover - defensive logging only
            ready = False
    else:
        ready = False
    if not ready and hasattr(service, "get_stage_b_amplification_not_ready_reason"):
        try:
            reason = service.get_stage_b_amplification_not_ready_reason()
            reason_label = getattr(reason, "name", str(reason))
            log_info(f"[VertexSlice] Stage B still not ready after rebuild (Reason={reason_label})")
        except Exception:  # pragma: no cover - defensive logging only
            log_info("[VertexSlice] Stage B readiness query failed.")

    west = float(bounds["west"])
    east = float(bounds["east"])
    south = float(bounds["south"])
    north = float(bounds["north"])

    samples = service.gather_stage_b_vertex_samples_in_bounds(west, east, south, north)
    if samples is None:
        log_error(f"[VertexSlice] gather_stage_b_vertex_samples_in_bounds returned None for tile {tile_id}")
        return []

    rows: list[dict[str, float]] = []
    for sample in samples:
        rows.append(
            {
                "vertex_index": int(sample.vertex_index),
                "longitude_deg": float(sample.longitude_deg),
                "latitude_deg": float(sample.latitude_deg),
                "elevation_m": float(sample.elevation_meters),
            }
        )

    if original_skip is not None and hasattr(service, "set_skip_cpu_amplification"):
        service.set_skip_cpu_amplification(original_skip)

    return rows


def write_csv(path: Path, rows: list[dict[str, float]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["vertex_index", "longitude_deg", "latitude_deg", "elevation_m"])
        for row in rows:
            writer.writerow(
                [
                    row["vertex_index"],
                    f"{row['longitude_deg']:.6f}",
                    f"{row['latitude_deg']:.6f}",
                    f"{row['elevation_m']:.6f}",
                ]
            )


def summarize(tile_id: str, rows: list[dict[str, float]]) -> None:
    if not rows:
        log_info(f"[VertexSlice] Tile {tile_id}: no samples captured.")
        return

    elevations = [row["elevation_m"] for row in rows]
    min_elev = min(elevations)
    max_elev = max(elevations)
    mean_elev = sum(elevations) / len(elevations)

    log_info(
        "[VertexSlice] Tile {}: {} vertices, elevation min={:.2f} m max={:.2f} m mean={:.2f} m".format(
            tile_id, len(rows), min_elev, max_elev, mean_elev
        )
    )

    preview = rows[:5]
    for entry in preview:
        log_info(
            "[VertexSlice] Sample vertex={} lon={:.4f} lat={:.4f} elev={:.2f} m".format(
                entry["vertex_index"],
                entry["longitude_deg"],
                entry["latitude_deg"],
                entry["elevation_m"],
            )
        )


def main() -> int:
    project_dir = resolve_project_dir()
    default_library = project_dir / "Content/PlanetaryCreation/Exemplars/ExemplarLibrary.json"

    parser = argparse.ArgumentParser(description="Export Stage B vertex samples within exemplar bounds.")
    parser.add_argument("--tile-id", default="O01", help="Exemplar tile identifier (default: O01)")
    parser.add_argument(
        "--library-json",
        default=str(default_library),
        help=f"Path to ExemplarLibrary.json (default: {default_library})",
    )
    parser.add_argument(
        "--output-csv",
        help="Destination CSV path (defaults to Docs/Automation/Validation/ExemplarAudit/<tile>_vertex_samples.csv)",
    )
    parser.add_argument(
        "--render-lod",
        type=int,
        help="Render subdivision level to apply before sampling (omitted = keep current setting)",
    )
    args = parser.parse_args()

    render_lod = args.render_lod
    if render_lod is None:
        env_lod = os.environ.get("PLANETARY_STAGEB_RENDER_LOD")
        if env_lod:
            try:
                render_lod = int(env_lod)
            except ValueError:
                log_error(f"[VertexSlice] Invalid PLANETARY_STAGEB_RENDER_LOD value '{env_lod}' ignored.")

    tile_id: str = args.tile_id
    library_path = Path(args.library_json)
    if not library_path.exists():
        raise SystemExit(f"Exemplar library not found at {library_path}")

    output_csv = Path(args.output_csv) if args.output_csv else project_dir / "Docs" / "Validation" / "ExemplarAudit" / f"{tile_id}_vertex_samples.csv"

    log_info(
        f"[VertexSlice] Arguments tile={tile_id} render_lod={render_lod} output={output_csv}"
    )

    bounds = load_bounds(library_path, tile_id)
    rows = gather_samples(tile_id, bounds, render_lod)
    write_csv(output_csv, rows)
    summarize(tile_id, rows)
    log_info(f"[VertexSlice] Wrote vertex samples to {output_csv}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
