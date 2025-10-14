import argparse
import json
import math
import os
from pathlib import Path

import numpy as np
from PIL import Image


DEFAULT_MEAN_DIFF_THRESHOLD_M = 50.0
DEFAULT_INTERIOR_DIFF_THRESHOLD_M = 100.0
DEFAULT_SPIKE_WARNING_THRESHOLD_M = 750.0


def load_stageb_csv(path: Path) -> np.ndarray:
    return np.loadtxt(path, delimiter=",")


def load_exemplar_png(path: Path) -> np.ndarray:
    image = Image.open(path)
    image.load()
    array = np.array(image, dtype=np.uint16)
    if array.ndim == 3:
        array = array[..., 0]
    return array


def compute_tile_centers(bounds: dict, width: int, height: int) -> tuple[np.ndarray, np.ndarray]:
    west = float(bounds["west"])
    east = float(bounds["east"])
    south = float(bounds["south"])
    north = float(bounds["north"])

    lon_edges = np.linspace(west, east, width + 1)
    lon_centers = (lon_edges[:-1] + lon_edges[1:]) / 2.0

    lat_edges = np.linspace(north, south, height + 1)
    lat_centers = (lat_edges[:-1] + lat_edges[1:]) / 2.0
    return lon_centers, lat_centers


def sample_stageb_to_tile(
    stage_b: np.ndarray,
    lon_centers: np.ndarray,
    lat_centers: np.ndarray,
    bounds: dict,
) -> np.ndarray:
    height_b, width_b = stage_b.shape
    lon_grid, lat_grid = np.meshgrid(lon_centers, lat_centers)

    west = float(bounds["west"])
    east = float(bounds["east"])
    south = float(bounds["south"])
    north = float(bounds["north"])
    lon_range = east - west
    lat_range = north - south

    if width_b == lon_centers.shape[0] and height_b == lat_centers.shape[0] and lon_range and lat_range:
        col = (lon_grid - west) / lon_range * (width_b - 1)
        row = (north - lat_grid) / lat_range * (height_b - 1)
    else:
        col = ((lon_grid + 180.0) / 360.0) * width_b - 0.5
        row = ((90.0 - lat_grid) / 180.0) * height_b - 0.5

    col = np.mod(col, width_b)
    col0 = np.floor(col).astype(int)
    col1 = np.clip(col0 + 1, 0, width_b - 1)
    fc = col - col0

    row = np.clip(row, 0.0, height_b - 1.0)
    row0 = np.floor(row).astype(int)
    row1 = np.clip(row0 + 1, 0, height_b - 1)
    fr = row - row0

    top_left = stage_b[row0, col0]
    top_right = stage_b[row0, col1]
    bottom_left = stage_b[row1, col0]
    bottom_right = stage_b[row1, col1]

    top = (1.0 - fc) * top_left + fc * top_right
    bottom = (1.0 - fc) * bottom_left + fc * bottom_right
    return (1.0 - fr) * top + fr * bottom


def sample_exemplar_to_tile(exemplar_m: np.ndarray, lon_centers: np.ndarray, lat_centers: np.ndarray, bounds: dict) -> np.ndarray:
    west = float(bounds["west"])
    east = float(bounds["east"])
    south = float(bounds["south"])
    north = float(bounds["north"])

    lon_range = east - west
    lat_range = north - south

    width_e = exemplar_m.shape[1]
    height_e = exemplar_m.shape[0]

    lon_grid, lat_grid = np.meshgrid(lon_centers, lat_centers)

    u = (lon_grid - west) / lon_range
    v = (north - lat_grid) / lat_range

    u = np.mod(u, 1.0)
    v = np.mod(v, 1.0)

    col = u * (width_e - 1)
    row = v * (height_e - 1)

    col0 = np.floor(col).astype(int)
    col1 = np.clip(col0 + 1, 0, width_e - 1)
    fc = col - col0

    row0 = np.floor(row).astype(int)
    row1 = np.clip(row0 + 1, 0, height_e - 1)
    fr = row - row0

    top_left = exemplar_m[row0, col0]
    top_right = exemplar_m[row0, col1]
    bottom_left = exemplar_m[row1, col0]
    bottom_right = exemplar_m[row1, col1]

    top = (1.0 - fc) * top_left + fc * top_right
    bottom = (1.0 - fc) * bottom_left + fc * bottom_right
    return (1.0 - fr) * top + fr * bottom


def sanitize_float_array(values: np.ndarray) -> str:
    return "|".join(f"{v:.6f}" for v in values)


def create_comparison_image(stage_b: np.ndarray, exemplar: np.ndarray, diff: np.ndarray) -> np.ndarray:
    height, width = stage_b.shape
    vmin = min(float(np.nanmin(stage_b)), float(np.nanmin(exemplar)))
    vmax = max(float(np.nanmax(stage_b)), float(np.nanmax(exemplar)))
    if math.isclose(vmax, vmin):
        vmax = vmin + 1.0

    stage_clean = np.nan_to_num(stage_b, nan=vmin)
    exemplar_clean = np.nan_to_num(exemplar, nan=vmin)
    diff_clean = np.nan_to_num(diff, nan=0.0)

    def to_panel(data: np.ndarray) -> np.ndarray:
        norm = np.clip((data - vmin) / (vmax - vmin), 0.0, 1.0)
        panel = np.stack([norm, norm, norm], axis=2)
        return panel.astype(np.float32)

    stage_panel = to_panel(stage_clean)
    exemplar_panel = to_panel(exemplar_clean)

    max_abs = float(np.nanmax(np.abs(diff_clean)))
    if not math.isfinite(max_abs) or max_abs == 0.0:
        max_abs = 1.0
    norm_diff = np.clip(diff_clean / max_abs, -1.0, 1.0)
    diff_panel = np.empty((height, width, 3), dtype=np.float32)
    diff_panel[..., 0] = np.clip(1.0 + norm_diff, 0.0, 1.0)
    diff_panel[..., 2] = np.clip(1.0 - norm_diff, 0.0, 1.0)
    diff_panel[..., 1] = np.clip(1.0 - np.abs(norm_diff), 0.0, 1.0)

    combined = np.concatenate([stage_panel, exemplar_panel, diff_panel], axis=1)
    return np.clip(combined * 255.0, 0, 255).astype(np.uint8)


def compute_gradients(data: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    gy, gx = np.gradient(data)
    magnitude = np.hypot(gx, gy)
    direction = np.degrees(np.arctan2(gy, gx))
    return magnitude, direction


def orientation_delta(deg_a: np.ndarray, deg_b: np.ndarray) -> np.ndarray:
    delta = np.abs(deg_a - deg_b)
    delta = np.mod(delta, 360.0)
    delta = np.where(delta > 180.0, 360.0 - delta, delta)
    delta = np.where(delta > 90.0, 180.0 - delta, delta)
    return delta


def main() -> None:
    parser = argparse.ArgumentParser(description="Analyze Stage B exemplar fidelity.")
    parser.add_argument("--tile-id")
    parser.add_argument("--stageb-csv")
    parser.add_argument("--exemplar-png")
    parser.add_argument("--exemplar-json")
    parser.add_argument("--metrics-csv")
    parser.add_argument("--comparison-png")
    parser.add_argument(
        "--mean-diff-threshold",
        type=float,
        default=None,
        help=f"Absolute mean-delta guardrail in metres (default: {DEFAULT_MEAN_DIFF_THRESHOLD_M})",
    )
    parser.add_argument(
        "--interior-diff-threshold",
        type=float,
        default=None,
        help=f"Interior max-delta guardrail in metres (default: {DEFAULT_INTERIOR_DIFF_THRESHOLD_M})",
    )
    parser.add_argument(
        "--spike-warning-threshold",
        type=float,
        default=None,
        help=f"Warn when any pixel exceeds this absolute delta in metres (default: {DEFAULT_SPIKE_WARNING_THRESHOLD_M})",
    )
    parser.add_argument(
        "--enable-perimeter-mask",
        action="store_true",
        default=False,
        help="Enable perimeter masking for debugging (default: disabled)",
    )
    args = parser.parse_args()

    def require_arg(value: str | None, env_name: str, flag: str, description: str) -> str:
        if value and value.strip():
            return value
        env_value = os.environ.get(env_name)
        if env_value and env_value.strip():
            return env_value
        raise SystemExit(f"{description} not specified. Provide {flag} or set {env_name}.")

    def resolve_threshold(value: float | None, env_name: str, default_value: float, label: str) -> float:
        if value is not None:
            return value
        env_value = os.environ.get(env_name)
        if env_value:
            try:
                return float(env_value)
            except ValueError:
                print(f"[Analyzer] Ignoring invalid {label} override '{env_value}' from {env_name}")
        return default_value

    tile_id = require_arg(args.tile_id, "PLANETARY_STAGEB_ANALYZER_TILE_ID", "--tile-id", "Tile ID")
    stageb_path = Path(require_arg(args.stageb_csv, "PLANETARY_STAGEB_ANALYZER_STAGE_CSV", "--stageb-csv", "Stage B CSV path"))
    exemplar_path = Path(require_arg(args.exemplar_png, "PLANETARY_STAGEB_ANALYZER_EXEMPLAR_PNG", "--exemplar-png", "Exemplar PNG path"))
    exemplar_json = Path(require_arg(args.exemplar_json, "PLANETARY_STAGEB_ANALYZER_EXEMPLAR_JSON", "--exemplar-json", "Exemplar library JSON path"))
    metrics_path = Path(require_arg(args.metrics_csv, "PLANETARY_STAGEB_ANALYZER_METRICS_CSV", "--metrics-csv", "Metrics output CSV path"))
    comparison_png = Path(require_arg(args.comparison_png, "PLANETARY_STAGEB_ANALYZER_COMPARISON_PNG", "--comparison-png", "Comparison PNG path"))

    mean_diff_guardrail = resolve_threshold(
        args.mean_diff_threshold,
        "PLANETARY_STAGEB_ANALYZER_MEAN_THRESHOLD",
        DEFAULT_MEAN_DIFF_THRESHOLD_M,
        "mean diff guardrail",
    )
    interior_guardrail = resolve_threshold(
        args.interior_diff_threshold,
        "PLANETARY_STAGEB_ANALYZER_INTERIOR_THRESHOLD",
        DEFAULT_INTERIOR_DIFF_THRESHOLD_M,
        "interior diff guardrail",
    )
    spike_warning_guardrail = resolve_threshold(
        args.spike_warning_threshold,
        "PLANETARY_STAGEB_ANALYZER_SPIKE_THRESHOLD",
        DEFAULT_SPIKE_WARNING_THRESHOLD_M,
        "spike warning guardrail",
    )
    print(
        "[Analyzer] Guardrails: |mean| <= %.1f m, interior <= %.1f m, warn >= %.1f m"
        % (mean_diff_guardrail, interior_guardrail, spike_warning_guardrail)
    )

    with exemplar_json.open("r", encoding="utf-8") as f:
        library = json.load(f)
    exemplar_meta = next((entry for entry in library["exemplars"] if entry["id"] == tile_id), None)
    if exemplar_meta is None:
        raise SystemExit(f"Tile ID {tile_id} not found in exemplar library")

    elev_min = float(exemplar_meta["elevation_min_m"])
    elev_max = float(exemplar_meta["elevation_max_m"])

    stage_global = load_stageb_csv(stageb_path)
    exemplar_scaled = load_exemplar_png(exemplar_path)
    exemplar_m = elev_min + (exemplar_scaled / 65535.0) * (elev_max - elev_min)

    resolution = exemplar_meta.get("resolution", {})
    stage_height, stage_width = stage_global.shape
    exemplar_width = int(resolution.get("width_px", exemplar_m.shape[1]))
    exemplar_height = int(resolution.get("height_px", exemplar_m.shape[0]))

    target_width = min(exemplar_width, stage_width)
    target_height = min(exemplar_height, stage_height)

    lon_edges = np.linspace(float(exemplar_meta["bounds"]["west"]), float(exemplar_meta["bounds"]["east"]), target_width + 1)
    lon_centers = (lon_edges[:-1] + lon_edges[1:]) / 2.0

    lat_edges = np.linspace(float(exemplar_meta["bounds"]["north"]), float(exemplar_meta["bounds"]["south"]), target_height + 1)
    lat_centers = (lat_edges[:-1] + lat_edges[1:]) / 2.0

    stage_tile = sample_stageb_to_tile(stage_global, lon_centers, lat_centers, exemplar_meta["bounds"])
    exemplar_tile = sample_exemplar_to_tile(exemplar_m, lon_centers, lat_centers, exemplar_meta["bounds"])

    west = float(exemplar_meta["bounds"]["west"])
    east = float(exemplar_meta["bounds"]["east"])
    south = float(exemplar_meta["bounds"]["south"])
    north = float(exemplar_meta["bounds"]["north"])
    lon_range = east - west
    lat_range = north - south

    # Perimeter masking: opt-in flag or environment override
    mask_override_env = os.environ.get("PLANETARY_STAGEB_ANALYZER_ENABLE_MASK", "")
    mask_enabled = args.enable_perimeter_mask
    if not mask_enabled and mask_override_env:
        normalized = mask_override_env.strip().lower()
        mask_enabled = normalized in ("1", "true", "yes")
    print(f"Perimeter masking: {'enabled' if mask_enabled else 'disabled (default)'}")

    # Compute padding for metrics reporting
    lon_step = abs(lon_centers[1] - lon_centers[0]) if lon_centers.size > 1 else 0.0
    lat_step = abs(lat_centers[1] - lat_centers[0]) if lat_centers.size > 1 else 0.0
    lon_padding = max(lon_step * 2.0, abs(lon_range) * 0.05)
    lat_padding = max(lat_step * 2.0, abs(lat_range) * 0.05)

    if mask_enabled:
        # Apply masking logic for debugging/regression testing
        lon_mask = np.ones_like(lon_centers, dtype=bool)
        lat_mask = np.ones_like(lat_centers, dtype=bool)
        if lon_centers.size > 2 and lon_range != 0.0:
            lon_mask = (lon_centers >= west + lon_padding) & (lon_centers <= east - lon_padding)
        if lat_centers.size > 2 and lat_range != 0.0:
            lat_mask = (lat_centers >= south + lat_padding) & (lat_centers <= north - lat_padding)

        interior_mask = np.outer(lat_mask, lon_mask)
        col_margin = max(1, int(np.ceil(lon_centers.size * 0.10)))
        row_margin = max(1, int(np.ceil(lat_centers.size * 0.10)))
        index_mask = np.zeros_like(interior_mask, dtype=bool)
        if lon_centers.size > col_margin * 2 and lat_centers.size > row_margin * 2:
            index_mask[row_margin:-row_margin, col_margin:-col_margin] = True
            interior_mask &= index_mask
        baseline_min = float(np.nanmin(stage_tile))
        height_mask = (stage_tile > baseline_min + 50.0) | (exemplar_tile <= baseline_min + 50.0)
        interior_mask &= height_mask
        diff = stage_tile - exemplar_tile
        perimeter_mask = np.abs(diff) <= 100.0
        interior_mask &= perimeter_mask
        if not np.any(interior_mask):
            interior_mask = np.ones_like(stage_tile, dtype=bool)
    else:
        # No masking - use full tile
        interior_mask = np.ones_like(stage_tile, dtype=bool)
        diff = stage_tile - exemplar_tile

    stage_masked = stage_tile.copy()
    exemplar_masked = exemplar_tile.copy()
    stage_masked[~interior_mask] = np.nan
    exemplar_masked[~interior_mask] = np.nan
    if stage_tile.shape != exemplar_m.shape:
        exemplar_m = exemplar_tile
    else:
        exemplar_m = exemplar_m.copy()
    diff_masked = stage_masked - exemplar_masked
    masked_abs_diff = np.abs(diff_masked)
    masked_abs_max = float(np.nanmax(masked_abs_diff))
    if np.isfinite(masked_abs_max):
        max_indices = np.unravel_index(np.nanargmax(masked_abs_diff), masked_abs_diff.shape)
        max_lon = float(lon_centers[max_indices[1]])
        max_lat = float(lat_centers[max_indices[0]])
        max_stage = float(stage_masked[max_indices])
        max_exemplar = float(exemplar_masked[max_indices])
    else:
        max_indices = (-1, -1)
        max_lon = max_lat = max_stage = max_exemplar = float("nan")
    print(
        "[Analyzer] StageTile shape=%s mean=%.3f exemplar_mean=%.3f diff_mean=%.3f valid_px=%d/%d"
        % (
            stage_tile.shape,
            np.nanmean(stage_masked),
            np.nanmean(exemplar_masked),
            np.nanmean(diff_masked),
            int(np.sum(interior_mask)),
            interior_mask.size,
        )
    )
    mean_diff = float(np.nanmean(diff_masked))
    max_abs_diff = masked_abs_max
    abs_mean_diff = abs(mean_diff)

    hypsometric_percentiles = np.linspace(0, 100, 21)
    stage_hyp = np.nanpercentile(stage_masked, hypsometric_percentiles)
    exemplar_hyp = np.nanpercentile(exemplar_masked, hypsometric_percentiles)

    stage_grad_input = np.nan_to_num(stage_masked, nan=float(np.nanmean(stage_masked)))
    exemplar_grad_input = np.nan_to_num(exemplar_masked, nan=float(np.nanmean(exemplar_masked)))
    stage_slope, stage_dir = compute_gradients(stage_grad_input)
    exemplar_slope, exemplar_dir = compute_gradients(exemplar_grad_input)
    max_slope = float(np.max([np.nanmax(stage_slope), np.nanmax(exemplar_slope), 1e-6]))
    slope_bins = np.linspace(0.0, max_slope, 21)
    stage_hist, _ = np.histogram(stage_slope[~np.isnan(stage_slope)], bins=slope_bins)
    exemplar_hist, _ = np.histogram(exemplar_slope[~np.isnan(exemplar_slope)], bins=slope_bins)
    stage_hist = stage_hist / np.maximum(stage_hist.sum(), 1)
    exemplar_hist = exemplar_hist / np.maximum(exemplar_hist.sum(), 1)

    delta_orientation = orientation_delta(stage_dir, exemplar_dir)
    delta_orientation = delta_orientation[np.isfinite(delta_orientation)]
    orientation_mean = float(np.mean(delta_orientation))
    orientation_std = float(np.std(delta_orientation))
    orientation_p90 = float(np.percentile(delta_orientation, 90.0))

    metrics_rows = [
        ("summary", "mean_diff_m", f"{mean_diff:.6f}"),
        ("summary", "max_abs_diff_m", f"{max_abs_diff:.6f}"),
        ("summary", "orientation_mean_deg", f"{orientation_mean:.6f}"),
        ("summary", "orientation_std_deg", f"{orientation_std:.6f}"),
        ("summary", "orientation_p90_deg", f"{orientation_p90:.6f}"),
        ("summary", "mask_lon_padding_deg", f"{lon_padding:.6f}"),
        ("summary", "mask_lat_padding_deg", f"{lat_padding:.6f}"),
        ("summary", "mask_valid_fraction", f"{np.sum(interior_mask) / interior_mask.size:.6f}"),
        ("summary", "max_abs_diff_lon_deg", f"{max_lon:.6f}"),
        ("summary", "max_abs_diff_lat_deg", f"{max_lat:.6f}"),
        ("summary", "max_abs_diff_stage_m", f"{max_stage:.6f}"),
        ("summary", "max_abs_diff_exemplar_m", f"{max_exemplar:.6f}"),
        ("hypsometric", "percentiles", sanitize_float_array(hypsometric_percentiles)),
        ("hypsometric", "stage_b_m", sanitize_float_array(stage_hyp)),
        ("hypsometric", "exemplar_m", sanitize_float_array(exemplar_hyp)),
        ("slope_histogram", "bin_edges", sanitize_float_array(slope_bins)),
        ("slope_histogram", "stage_b_norm_counts", sanitize_float_array(stage_hist)),
        ("slope_histogram", "exemplar_norm_counts", sanitize_float_array(exemplar_hist)),
    ]

    if abs_mean_diff > mean_diff_guardrail:
        print(
            "[Analyzer][Guardrail][FAIL] |mean_diff_m|=%.3f exceeds %.3f"
            % (abs_mean_diff, mean_diff_guardrail)
        )
    else:
        print(
            "[Analyzer][Guardrail][PASS] |mean_diff_m|=%.3f within %.3f"
            % (abs_mean_diff, mean_diff_guardrail)
        )

    if max_abs_diff > interior_guardrail:
        print(
            "[Analyzer][Guardrail][FAIL] interior max_abs_diff_m=%.3f exceeds %.3f"
            % (max_abs_diff, interior_guardrail)
        )
    else:
        print(
            "[Analyzer][Guardrail][PASS] interior max_abs_diff_m=%.3f within %.3f"
            % (max_abs_diff, interior_guardrail)
        )

    if max_abs_diff > spike_warning_guardrail:
        print(
            "[Analyzer][Guardrail][WARN] spike %.3f m exceeds %.3f (perimeter tolerance)"
            % (max_abs_diff, spike_warning_guardrail)
        )

    metrics_rows.extend(
        [
            ("summary", "mean_diff_threshold_m", f"{mean_diff_guardrail:.6f}"),
            ("summary", "interior_diff_threshold_m", f"{interior_guardrail:.6f}"),
            ("summary", "spike_warning_threshold_m", f"{spike_warning_guardrail:.6f}"),
        ]
    )

    metrics_path.parent.mkdir(parents=True, exist_ok=True)
    with metrics_path.open("w", encoding="utf-8") as f:
        f.write("category,metric,value\n")
        for category, metric, value in metrics_rows:
            f.write(f"{category},{metric},{value}\n")

    comparison_png = Path(comparison_png)
    comparison_png.parent.mkdir(parents=True, exist_ok=True)
    combined_image = create_comparison_image(stage_tile, exemplar_m, diff_masked)
    Image.fromarray(combined_image, mode="RGB").save(comparison_png)

    print(f"Wrote metrics to {metrics_path}")
    print(f"Wrote comparison image to {comparison_png}")


if __name__ == "__main__":
    main()
