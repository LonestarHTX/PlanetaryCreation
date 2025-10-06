# PlanetaryCreation GPU Preview Debug & Recovery Plan

## 0. Overview

Symptoms:
- Plate movement frozen.
- Heightmap and plate boundary overlays showing no color.
- GPU preview logs confirm: `GPU preview mode ENABLED (CPU amplification SKIPPED)`.

Diagnosis:
The new GPU Preview path likely skips both kinematic updates and overlay color passes, resulting in static plates and blank debug materials.

---

## 1. Quick Localization Tests

**Test A — Disable GPU preview**
```ini
r.PlanetaryCreation.GPUPreview=0
```
➡️ If plate motion and colors return → CPU path is fine; preview path skipped required updates.

**Test B — Force red vertex colors**
In controller before streaming to RMC:
```cpp
for (auto& C : Colors) C = FColor::Red;
RMC->UpdateStream(ERealtimeMeshStream::Color, Colors);
```
➡️ If mesh still white → Material not sampling `VertexColor`.

---

## 2. Root Causes & Fixes

### A) Early Return in GPU Preview Path
**Problem:** `if (bGPUPreview) return;` bypasses kinematics + overlay logic.

**Fix:** Skip only CPU Stage‑B, not everything.
```cpp
Service.ApplyPlateKinematics(DeltaMyr);
Service.UpdatePlateAssignmentsIfNeeded();
Snapshot = Service.CreateMeshBuildSnapshot(/*NoCPUAmplify=*/true);
BuildDebugVertexColors(Snapshot, DebugMode);
RMC->UpdateStream(ERealtimeMeshStream::Color, Colors);
if (!bGPUPreview)
{
    ApplyStageBCPU(Snapshot);
    RMC->UpdateStream(ERealtimeMeshStream::Position, Positions);
}
RunPreviewHeightWriteCS(Snapshot); // Always update height texture
```

### B) Cached Unit Directions Not Transformed
**Problem:** Using cached vertex directions w/o applying per‑plate rotations.

**Fix:** Apply transforms each step.
```cpp
FQuat R = PlateRotation[PlateId[v]];
FVector3f WorldDir = R.RotateVector(BaseUnitDir[v]);
float2 UV = Equirect(WorldDir);
```

### C) Material Ignores Vertex Colors
**Fix:**
```hlsl
BaseColor = lerp(BaseAlbedo, VertexColor, DebugOverlayOpacity);
```
Set `MID->SetScalarParameterValue("DebugOverlayOpacity", 1.0f);`

### D) Stale Topology Stamp Blocking Overlay Rebuilds
**Fix:** Separate update triggers:
- `TopologyStamp` → adjacency only.
- `KinematicsStamp` → per‑step color/overlay refresh.

### E) Vertex Count Mismatch in Height Texture
**Problem:** Equirect scatter drops verts (382 missing at L7).

**Fix:**
- Post‑fill holes (3×3 kernel) **or**
- Replace with image‑space dispatch over pixels → derive direction from `(u,v)` and compute height.

---

## 3. Diagnostic Additions

- **Kinematics delta log:**
  ```cpp
  UE_LOG(LogPlanetary, Verbose, TEXT("Max plate Δθ: %.4f deg"), MaxDeltaDeg);
  ```
- **Overlay integrity test:** Compare `VertexColor.r` vs normalized height.
- **Smoke keybind:** Toggle all‑red vertex color.

---

## 4. Immediate Patch

```cpp
if (bGPUPreview)
{
    Service.ApplyPlateKinematics(DeltaMyr);
    Snapshot = Service.CreateMeshBuildSnapshot(true);
    BuildDebugVertexColors(Snapshot, DebugMode);
    RMC->UpdateStream(ERealtimeMeshStream::Color, Colors);
    RunPreviewHeightWriteCS(Snapshot);
    return;
}
```

---

## 5. Next Steps

1. Convert preview to **cube‑face height textures** (6 PF_R16F) → remove seams, uniform sampling.
2. Move **Sediment/Dampening** to SoA + two‑pass gather or GPU compute.
3. Implement **Ridge‑direction caching** with `TopologyStamp`.
4. Add **drift hysteresis** to re‑tessellation.

---

## 6. Verification Checklist

- ✅ Plate drift visible each step.
- ✅ Boundary overlays colored correctly.
- ✅ Heightmap updates per frame (check texture inspector).
- ✅ Vertex counts match (`164224` both paths).

When all four pass, GPU Preview is functionally identical to CPU visualization.

