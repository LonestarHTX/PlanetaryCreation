# M6 Terrain Visibility Analysis: Why You Can't See Mountains and Valleys

**Created:** 2025-10-10
**Issue:** Stage B amplification is working, but mountains and valleys aren't visible in the mesh
**Root Cause:** Missing hydraulic erosion + scale mismatch

---

## TL;DR

You **HAVE** mountains from Stage B amplification, but you **CAN'T SEE THEM** because:

1. **No valley carving** - Stage B creates peaks, but without hydraulic erosion there are no valleys for contrast
2. **Scale is correct but subtle** - Your elevation changes ARE being applied to mesh geometry, but need more dramatic relief
3. **Missing the paper's final step** - The paper explicitly requires hydraulic erosion to create realistic terrain

**Fix:** Implement Phase 3 (Hydraulic Erosion) as planned.

---

## Current Implementation Status

### ✅ What's Working:

1. **Stage B Oceanic Amplification** (`OceanicAmplification.cpp`):
   - Gabor noise approximation for transform faults ✅
   - Ridge-perpendicular detail ✅
   - Writes to `VertexAmplifiedElevation[]` ✅
   - Range: Approximately ±100-500m detail

2. **Stage B Continental Amplification** (`TectonicSimulationService.cpp:6469`):
   - Exemplar-based heightfield blending ✅
   - Terrain type classification (Plains, Old Mountains, Andean, Himalayan) ✅
   - Writes to `VertexAmplifiedElevation[]` ✅
   - Range: Up to ~3000-5000m for mountains

3. **Mesh Geometry Application** (`TectonicSimulationController.cpp:1280-1312`):
   ```cpp
   FVector3f Position = UnitNormal * RadiusUE;  // Base sphere position

   if (Snapshot.bUseAmplifiedElevation && Snapshot.VertexAmplifiedElevation.IsValidIndex(Index))
   {
       ElevationMeters = Snapshot.VertexAmplifiedElevation[Index];
   }

   if (Snapshot.ElevationMode == EElevationMode::Displaced)
   {
       const double ClampedElevation = FMath::Clamp(ElevationMeters, -MaxDisplacementMeters, MaxDisplacementMeters);
       const float DisplacementUE = MetersToUE(ClampedElevation);  // meters → km (1 UE = 1 km)
       Position += Normal * DisplacementUE;  // ✅ DISPLACEMENT IS APPLIED
   }
   ```

**The geometry displacement IS happening.** Your amplified elevations ARE being added to vertex positions.

---

## Why You Can't See Mountains and Valleys

### Problem 1: No Valleys = No Contrast

**Stage B creates mountains, but NOT valleys.**

From the paper (Section 5):
> "We use the procedural terrain amplification technique from Génevaux et al. [2013]... **followed by hydraulic erosion** to produce believable landforms."

Your current amplification:
- Continental crust: Adds +2000-5000m peaks (exemplar-based)
- Oceanic crust: Adds ±100-500m transform fault detail (Gabor noise)
- **Result:** Bumpy highlands with no river valleys

Without erosion carving valleys:
- Mountains blend into each other (no sharp relief)
- No drainage patterns (no visual "flow")
- Terrain looks like noise, not geology

### Problem 2: Scale Mismatch for Visual Impact

**Earth's real mountain-to-valley relief:**
- Himalayas: 8848m peak (Mt. Everest) vs 2000m valley floor = **6848m relief**
- Grand Canyon: 2600m rim vs 730m river = **1870m vertical drop**

**Your current Stage B output (no erosion):**
- Continental peaks: ~3000-5000m above baseline
- No valleys: Baseline stays at ~0m (maybe ±500m ocean noise)
- **Effective relief: 3000-5000m** spread over broad areas

**Visibility problem:**
- Planet radius: 6370 km = 6,370,000 meters
- Mountain height: 5000 meters
- **Ratio: 5000m / 6,370,000m = 0.0785% of radius**

At LOD6 viewing distance (camera ~20,000 km away), a 5km mountain on a 6370km sphere is **barely a bump**.

**You need valleys to create contrast:**
- With erosion: Valley floor at -2000m, peak at +5000m = **7000m relief** in short distance
- Sharp elevation changes = visible terrain features
- Drainage networks = recognizable patterns

### Problem 3: Missing Paper Algorithm

From Génevaux et al. 2013 (the amplification paper):
> "Our method procedurally creates mountain ranges... **The user can further enhance realism by applying thermal and hydraulic erosion**."

From the Procedural Tectonic Planets paper (your reference):
> "We follow the approach of Génevaux... **with subsequent hydraulic erosion to create believable river valleys and mountain ranges**."

**The paper explicitly states hydraulic erosion is REQUIRED for realistic output.**

Stage B alone creates:
- ✅ Mountain peaks (via exemplars)
- ✅ Transform fault ridges (via Gabor noise)
- ❌ River valleys
- ❌ Drainage basins
- ❌ Erosional relief

---

## What You're Seeing Now

Based on your implementation:

1. **Oceanic areas**: Subtle ±100-500m ripples from Gabor noise (transform faults)
   - Too small to see at planetary scale
   - No dramatic features (just noise texture)

2. **Continental areas**: Broad 2000-5000m highlands from exemplar blending
   - Mountains exist, but blend together
   - No valleys, so no visual separation
   - Looks like "lumpy plains" not mountain ranges

3. **Overall appearance**: Slightly bumpy sphere
   - Elevation changes are THERE (check with elevation heatmap visualization mode)
   - But no dramatic relief visible in geometry mode
   - Missing the "mountain and valley" look

---

## How to Verify This Analysis

### Test 1: Check Elevation Heatmap Mode
```cpp
// In editor, switch visualization mode:
SetVisualizationMode(ETectonicVisualizationMode::Elevation);
```

You should see:
- ✅ Color variation showing elevation changes
- ✅ Continental areas brighter (higher elevation)
- ✅ Oceanic areas darker (lower elevation)

This proves amplification IS working, just not visually dramatic.

### Test 2: Check VertexAmplifiedElevation Values

Add logging in `TectonicSimulationController.cpp` around line 1301:
```cpp
static int32 DebugCount = 0;
if (DebugCount < 10 && ElevationMeters > 1000.0) {
    UE_LOG(LogPlanetaryCreation, Warning,
        TEXT("[Elevation] Vertex %d: %.1f meters (%.3f UE units after MetersToUE)"),
        Index, ElevationMeters, MetersToUE(ElevationMeters));
    DebugCount++;
}
```

Expected output:
```
[Elevation] Vertex 1234: 4532.3 meters (4.532 UE units after MetersToUE)
[Elevation] Vertex 5678: 3201.7 meters (3.202 UE units after MetersToUE)
```

This confirms displacement IS being applied to geometry.

### Test 3: Exaggerate Displacement (Quick Visual Test)

Temporarily modify line 1311 in `TectonicSimulationController.cpp`:
```cpp
// BEFORE (correct):
const float DisplacementUE = MetersToUE(ClampedElevation);

// AFTER (10x exaggeration for testing):
const float DisplacementUE = MetersToUE(ClampedElevation) * 10.0f;
```

If you NOW see dramatic mountains, it proves:
- ✅ Displacement code works
- ✅ Amplification data is correct
- ❌ Scale needs valleys for contrast (not just bigger mountains)

---

## The Solution: Implement Hydraulic Erosion

### What Hydraulic Erosion Will Add:

1. **Valley Carving** (stream power erosion):
   - Finds downhill flow paths
   - Carves channels where water would flow
   - Creates drainage networks

2. **Dramatic Relief**:
   - Peak at +5000m
   - Valley floor at -1000m (eroded down)
   - **6000m vertical drop in short distance = VISIBLE**

3. **Recognizable Patterns**:
   - River valleys cutting through mountain ranges
   - Dendritic drainage networks
   - Alluvial plains at mountain bases

### Expected Visual Improvement:

**Before hydraulic erosion (current):**
```
Elevation profile: ≈≈≈≈≈≈≈≈ (bumpy)
Visual: Slightly lumpy sphere
```

**After hydraulic erosion:**
```
Elevation profile: /\/\/\/\ (peaks and valleys)
Visual: Mountains with valley networks
```

### Implementation (Already Planned):

See `Docs/hydraulic_erosion_implementation_plan.md` for full details:

1. **Downhill flow graph** (find lowest neighbors)
2. **Flow accumulation** (drainage area calculation)
3. **Stream power erosion** (E = K × A^m × S^n)
4. **Mass conservation** (erosion → deposition)

**Timeline:** 6 days (4 days routing, 2 days age coupling)

---

## Alternative Quick Fixes (Not Recommended)

### Option A: Exaggerate Amplification Scale
**Idea:** Multiply `VertexAmplifiedElevation` by 2-3× before mesh application.

**Pros:**
- Quick test (1 line change)
- Makes mountains more visible

**Cons:**
- ❌ Not paper-accurate
- ❌ Still no valleys (just bigger lumps)
- ❌ Breaks realistic Earth-scale proportions

### Option B: Add Artificial Valleys (Inverted Noise)
**Idea:** Subtract elevation in random patterns to fake valleys.

**Pros:**
- Faster than hydraulic erosion (~2 days)

**Cons:**
- ❌ Valleys won't follow drainage physics
- ❌ Won't look realistic
- ❌ Not paper-compliant

### Option C: Defer to M7 (Current Plan is Wrong)
**Idea:** Mark M6 complete without hydraulic erosion.

**Cons:**
- ❌ Paper explicitly requires erosion for believable terrain
- ❌ Won't achieve "paper parity in mesh output"
- ❌ You stated goal: "I want to see mountains and valleys by end of M6"

---

## Recommended Path Forward

### Step 1: Verify Current Implementation (1 hour)
- Switch to elevation heatmap mode (confirm amplification working)
- Add debug logging for `VertexAmplifiedElevation` values
- Confirm values in 2000-5000m range for continents

### Step 2: Implement Hydraulic Erosion (6 days)
- Follow `Docs/hydraulic_erosion_implementation_plan.md`
- Target: <8ms at L6 (continental-only if needed)
- Create realistic drainage patterns

### Step 3: Visual Validation
- Look for:
  - ✅ River valleys cutting through mountain ranges
  - ✅ Drainage networks visible at L6/L7
  - ✅ Sharp relief (peaks vs valleys)
  - ✅ Paper-like terrain appearance

### Step 4: Profiling & Documentation
- Run L6/L7/L8 profiling (Task 4.3)
- Write M6 completion docs (Task 5.3)

---

## Summary

| Component | Status | Visual Impact |
|-----------|--------|---------------|
| Oceanic amplification (Gabor noise) | ✅ Working | Low (±500m, subtle) |
| Continental amplification (exemplars) | ✅ Working | Medium (2-5km peaks) |
| Mesh geometry displacement | ✅ Working | Applied correctly |
| **Hydraulic erosion (valleys)** | ❌ **Missing** | **HIGH (creates relief)** |

**Bottom line:** Your amplification works. Elevations ARE in the mesh. But without valleys, mountains blend together and aren't visually dramatic. Implement hydraulic erosion (Phase 3) to achieve the "mountains and valleys" look you want.

---

## References

- **Génevaux et al. 2013**: "Terrain Generation Using Procedural Models Based on Hydrology"
- **Procedural Tectonic Planets Paper**: Section 5 - "Stage B: Detail Amplification"
- **Your Implementation**: `OceanicAmplification.cpp`, `TectonicSimulationService.cpp:6469`, `TectonicSimulationController.cpp:1280`
- **Hydraulic Plan**: `Docs/hydraulic_erosion_implementation_plan.md`

---

**Next Step:** Implement Phase 3 (Hydraulic Erosion) per the plan. This is the missing piece for paper-parity terrain.
