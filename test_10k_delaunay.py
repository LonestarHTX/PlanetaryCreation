#!/usr/bin/env python3
"""
Direct test of 10k Fibonacci Delaunay triangulation with diagnostics.
Bypasses Unreal automation framework to test the C++ code directly.
"""

import sys
import os
import math
import time
from array import array

# Add project paths
project_root = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(project_root, 'Source'))

# Test parameters
POINT_COUNT = 10000
ENABLE_SHUFFLE = True
SHUFFLE_SEED = 42

def generate_fibonacci_samples(n):
    """Generate n points on unit sphere using Fibonacci spiral."""
    points = []
    golden_angle = math.pi * (3.0 - math.sqrt(5.0))  # radians

    for i in range(n):
        # Height: -1 to 1
        y = 1.0 - (2.0 * i) / (n - 1)

        # Radius at this height
        radius = math.sqrt(1.0 - y * y)

        # Angle
        theta = golden_angle * i

        x = math.cos(theta) * radius
        z = math.sin(theta) * radius

        points.append((x, y, z))

    return points

def build_shuffle_mapping(n, seed):
    """Build deterministic shuffle mapping using Fisher-Yates."""
    import random
    rng = random.Random(seed)

    # Identity permutation
    mapping = list(range(n))

    # Fisher-Yates shuffle
    for i in range(n - 1, 0, -1):
        j = rng.randint(0, i)
        mapping[i], mapping[j] = mapping[j], mapping[i]

    return mapping

def test_stripack_directly():
    """Test STRIPACK triangulation directly via ctypes."""
    try:
        import ctypes
    except ImportError:
        print("ERROR: ctypes not available")
        return False

    # Load the DLL
    dll_path = os.path.join(
        project_root,
        'Binaries', 'Win64',
        'UnrealEditor-PlanetaryCreationEditor.dll'
    )

    if not os.path.exists(dll_path):
        print(f"ERROR: DLL not found at {dll_path}")
        return False

    print(f"Loading DLL from: {dll_path}")
    try:
        dll = ctypes.CDLL(dll_path)
    except Exception as e:
        print(f"ERROR: Failed to load DLL: {e}")
        return False

    # Get stripack_triangulate function
    try:
        stripack_func = dll.stripack_triangulate
        stripack_func.argtypes = [
            ctypes.c_int32,         # n
            ctypes.POINTER(ctypes.c_double),  # xyz
            ctypes.POINTER(ctypes.c_int32),   # ntri
            ctypes.POINTER(ctypes.c_int32),   # tri
        ]
    except Exception as e:
        print(f"ERROR: Failed to get stripack_triangulate: {e}")
        return False

    print(f"\n=== Testing {POINT_COUNT} point Fibonacci Delaunay ===")

    # Generate points
    print(f"Generating {POINT_COUNT} Fibonacci samples...")
    points = generate_fibonacci_samples(POINT_COUNT)
    print(f"✓ Generated {len(points)} points")

    # Apply shuffle if enabled
    if ENABLE_SHUFFLE:
        print(f"Building shuffle mapping (seed={SHUFFLE_SEED})...")
        shuffled_indices = build_shuffle_mapping(POINT_COUNT, SHUFFLE_SEED)
        shuffled_points = [points[i] for i in shuffled_indices]
        print(f"✓ Shuffled point order")
    else:
        shuffled_points = points

    # Convert to xyz(3,n) buffer
    xyz = (ctypes.c_double * (3 * POINT_COUNT))()
    for i, (x, y, z) in enumerate(shuffled_points):
        xyz[3 * i + 0] = x
        xyz[3 * i + 1] = y
        xyz[3 * i + 2] = z

    # Allocate triangle buffer
    max_tri = max(2 * POINT_COUNT, 16)
    tri_buf = (ctypes.c_int32 * (3 * max_tri))()
    ntri_out = ctypes.c_int32(0)

    # Call STRIPACK
    print(f"Calling stripack_triangulate(n={POINT_COUNT}, max_tri={max_tri})...")
    start_time = time.time()
    try:
        stripack_func(
            POINT_COUNT,
            xyz,
            ctypes.byref(ntri_out),
            tri_buf
        )
    except Exception as e:
        print(f"ERROR: stripack_triangulate call failed: {e}")
        return False

    duration = time.time() - start_time
    ntri = ntri_out.value

    print(f"✓ Triangulation completed in {duration:.3f}s")
    print(f"  Triangles: {ntri}")

    # Validate
    if ntri <= 0:
        print(f"ERROR: No triangles generated (ntri={ntri})")
        return False

    if ntri > max_tri:
        print(f"ERROR: Triangle count {ntri} exceeds buffer {max_tri}")
        return False

    print(f"✓ Triangle count valid")

    # Check topological properties
    print(f"\nChecking topological properties...")

    # Build edge set and degree histogram
    edges = set()
    degrees = [0] * POINT_COUNT

    for tri_idx in range(ntri):
        a = tri_buf[3 * tri_idx + 0]
        b = tri_buf[3 * tri_idx + 1]
        c = tri_buf[3 * tri_idx + 2]

        # Validate indices
        if a < 0 or a >= POINT_COUNT or b < 0 or b >= POINT_COUNT or c < 0 or c >= POINT_COUNT:
            print(f"ERROR: Invalid triangle indices at {tri_idx}: ({a}, {b}, {c})")
            return False

        # Add edges (normalized to avoid duplicates)
        for v1, v2 in [(a, b), (b, c), (c, a)]:
            edge = (min(v1, v2), max(v1, v2))
            if edge not in edges:
                edges.add(edge)
                degrees[v1] += 1
                degrees[v2] += 1

    # Euler characteristic: V - E + F = 2
    v_count = POINT_COUNT
    e_count = len(edges)
    f_count = ntri
    euler = v_count - e_count + f_count

    print(f"  V={v_count}, E={e_count}, F={f_count}")
    print(f"  Euler characteristic: {v_count} - {e_count} + {f_count} = {euler}")

    if euler != 2:
        print(f"ERROR: Euler characteristic is {euler}, expected 2")
        return False

    print(f"✓ Euler characteristic correct")

    # Check degree distribution
    min_degree = min(degrees)
    max_degree = max(degrees)
    avg_degree = sum(degrees) / len(degrees)

    print(f"  Degree: min={min_degree}, avg={avg_degree:.3f}, max={max_degree}")

    if min_degree < 3:
        print(f"ERROR: Minimum degree {min_degree} < 3")
        return False

    if not (5.5 <= avg_degree <= 6.5):
        print(f"ERROR: Average degree {avg_degree} not in [5.5, 6.5]")
        return False

    print(f"✓ Degree distribution valid")

    print(f"\n✓✓✓ All tests passed! ✓✓✓")
    print(f"Performance Summary:")
    print(f"  Points: {POINT_COUNT}")
    print(f"  Shuffle: {'enabled' if ENABLE_SHUFFLE else 'disabled'}")
    print(f"  Time: {duration:.3f}s")
    print(f"  Rate: {POINT_COUNT / duration:.0f} points/sec")

    return True

if __name__ == '__main__':
    success = test_stripack_directly()
    sys.exit(0 if success else 1)
