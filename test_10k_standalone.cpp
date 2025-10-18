#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <cstring>
#include <vector>
#include <set>
#include <algorithm>

// External Fortran function
extern "C" void stripack_triangulate(int32_t n, const double* xyz, int32_t* ntri, int32_t* tri);

struct Point3D {
    double x, y, z;
};

std::vector<Point3D> generate_fibonacci_samples(int n) {
    std::vector<Point3D> points(n);
    double golden_angle = M_PI * (3.0 - std::sqrt(5.0));

    for (int i = 0; i < n; ++i) {
        double y = 1.0 - (2.0 * i) / (n - 1.0);
        double radius = std::sqrt(1.0 - y * y);
        double theta = golden_angle * i;

        points[i].x = std::cos(theta) * radius;
        points[i].y = y;
        points[i].z = std::sin(theta) * radius;
    }

    return points;
}

std::vector<int> build_shuffle_mapping(int n, unsigned int seed) {
    std::vector<int> mapping(n);
    for (int i = 0; i < n; ++i) {
        mapping[i] = i;
    }

    // Simple LCG-based shuffle
    unsigned int state = seed;
    for (int i = n - 1; i > 0; --i) {
        state = (state * 1103515245 + 12345) & 0x7fffffff;
        int j = state % (i + 1);
        std::swap(mapping[i], mapping[j]);
    }

    return mapping;
}

int main() {
    const int POINT_COUNT = 10000;
    const bool ENABLE_SHUFFLE = true;
    const int SHUFFLE_SEED = 42;

    printf("=== Testing %d point Fibonacci Delaunay ===\n", POINT_COUNT);

    // Generate points
    printf("Generating %d Fibonacci samples...\n", POINT_COUNT);
    auto points = generate_fibonacci_samples(POINT_COUNT);
    printf("✓ Generated %zu points\n", points.size());

    // Apply shuffle
    std::vector<Point3D> shuffled_points = points;
    if (ENABLE_SHUFFLE) {
        printf("Building shuffle mapping (seed=%d)...\n", SHUFFLE_SEED);
        auto shuffled_indices = build_shuffle_mapping(POINT_COUNT, SHUFFLE_SEED);
        for (int i = 0; i < POINT_COUNT; ++i) {
            shuffled_points[i] = points[shuffled_indices[i]];
        }
        printf("✓ Shuffled point order\n");
    }

    // Convert to xyz(3,n) buffer
    double* xyz = new double[3 * POINT_COUNT];
    for (int i = 0; i < POINT_COUNT; ++i) {
        xyz[3 * i + 0] = shuffled_points[i].x;
        xyz[3 * i + 1] = shuffled_points[i].y;
        xyz[3 * i + 2] = shuffled_points[i].z;
    }

    // Allocate triangle buffer
    int max_tri = (2 * POINT_COUNT > 16) ? (2 * POINT_COUNT) : 16;
    int32_t* tri_buf = new int32_t[3 * max_tri];
    int32_t ntri = 0;

    // Call STRIPACK
    printf("Calling stripack_triangulate(n=%d, max_tri=%d)...\n", POINT_COUNT, max_tri);
    fflush(stdout);

    clock_t start = clock();
    stripack_triangulate(POINT_COUNT, xyz, &ntri, tri_buf);
    clock_t end = clock();

    double duration = double(end - start) / CLOCKS_PER_SEC;

    printf("✓ Triangulation completed in %.3f s\n", duration);
    printf("  Triangles: %d\n", ntri);

    // Validate
    if (ntri <= 0) {
        printf("ERROR: No triangles generated (ntri=%d)\n", ntri);
        return 1;
    }

    if (ntri > max_tri) {
        printf("ERROR: Triangle count %d exceeds buffer %d\n", ntri, max_tri);
        return 1;
    }

    printf("✓ Triangle count valid\n");

    // Check topological properties
    printf("\nChecking topological properties...\n");

    std::set<std::pair<int, int>> edges;
    std::vector<int> degrees(POINT_COUNT, 0);

    for (int tri_idx = 0; tri_idx < ntri; ++tri_idx) {
        int a = tri_buf[3 * tri_idx + 0];
        int b = tri_buf[3 * tri_idx + 1];
        int c = tri_buf[3 * tri_idx + 2];

        // Validate indices
        if (a < 0 || a >= POINT_COUNT || b < 0 || b >= POINT_COUNT || c < 0 || c >= POINT_COUNT) {
            printf("ERROR: Invalid triangle indices at %d: (%d, %d, %d)\n", tri_idx, a, b, c);
            return 1;
        }

        // Add edges
        for (auto [v1, v2] : std::vector<std::pair<int,int>>{{a,b},{b,c},{c,a}}) {
            auto edge = std::make_pair(std::min(v1, v2), std::max(v1, v2));
            if (edges.find(edge) == edges.end()) {
                edges.insert(edge);
                degrees[v1]++;
                degrees[v2]++;
            }
        }
    }

    // Euler characteristic
    int v_count = POINT_COUNT;
    int e_count = edges.size();
    int f_count = ntri;
    int euler = v_count - e_count + f_count;

    printf("  V=%d, E=%d, F=%d\n", v_count, e_count, f_count);
    printf("  Euler characteristic: %d - %d + %d = %d\n", v_count, e_count, f_count, euler);

    if (euler != 2) {
        printf("ERROR: Euler characteristic is %d, expected 2\n", euler);
        return 1;
    }

    printf("✓ Euler characteristic correct\n");

    // Degree distribution
    int min_degree = *std::min_element(degrees.begin(), degrees.end());
    int max_degree = *std::max_element(degrees.begin(), degrees.end());
    double avg_degree = 0;
    for (int d : degrees) avg_degree += d;
    avg_degree /= degrees.size();

    printf("  Degree: min=%d, avg=%.3f, max=%d\n", min_degree, avg_degree, max_degree);

    if (min_degree < 3) {
        printf("ERROR: Minimum degree %d < 3\n", min_degree);
        return 1;
    }

    if (avg_degree < 5.5 || avg_degree > 6.5) {
        printf("ERROR: Average degree %.3f not in [5.5, 6.5]\n", avg_degree);
        return 1;
    }

    printf("✓ Degree distribution valid\n");

    // Cleanup
    delete[] xyz;
    delete[] tri_buf;

    printf("\n✓✓✓ All tests passed! ✓✓✓\n");
    printf("Performance Summary:\n");
    printf("  Points: %d\n", POINT_COUNT);
    printf("  Shuffle: %s\n", ENABLE_SHUFFLE ? "enabled" : "disabled");
    printf("  Time: %.3f s\n", duration);
    printf("  Rate: %.0f points/sec\n", POINT_COUNT / duration);

    return 0;
}
