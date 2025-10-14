import argparse
import math
import os
import unreal


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--indices", help="Comma-separated list of render vertex indices")
    args = parser.parse_args()

    indices_source = args.indices or os.environ.get("PLANETARY_DEBUG_VERTEX_INDICES")
    if not indices_source:
        raise SystemExit("No vertex indices provided. Pass --indices or set PLANETARY_DEBUG_VERTEX_INDICES.")

    indices = []
    for token in indices_source.split(","):
        token = token.strip()
        if not token:
            continue
        try:
            indices.append(int(token))
        except ValueError:
            raise SystemExit(f"Invalid vertex index '{token}'")

    service = unreal.get_editor_subsystem(unreal.TectonicSimulationService)
    if service is None:
        raise SystemExit("TectonicSimulationService subsystem unavailable.")

    pos_x, pos_y, pos_z, *_ = service.get_render_vertex_float_soa()
    if not pos_x or not pos_y or not pos_z:
        raise SystemExit("Render vertex buffers unavailable.")

    for index in indices:
        if index < 0 or index >= len(pos_x):
            unreal.log_warning(f"[VertexDebug] Index {index} out of range (0..{len(pos_x)-1})")
            continue

        x = float(pos_x[index])
        y = float(pos_y[index])
        z = float(pos_z[index])

        lon = math.degrees(math.atan2(y, x))
        lat = math.degrees(math.asin(max(-1.0, min(1.0, z / math.sqrt(x * x + y * y + z * z)))))

        unreal.log(f"[VertexDebug] Index={index} Lon={lon:.6f} Lat={lat:.6f} Pos=({x:.6f},{y:.6f},{z:.6f})")


if __name__ == "__main__":
    main()
