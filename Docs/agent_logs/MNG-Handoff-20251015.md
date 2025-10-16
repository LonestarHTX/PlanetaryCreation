# MNG Handoff — 2025-10-15

## Status Overview
- Phase 1.4 complete: Forced exemplar audits (A01/O01/H01/A09) all green; edge spikes eliminated; analyzer + automation thresholds in place.
- Phase 1.5 in progress: Fold direction + orogeny classes implemented and logged; anisotropy (class‑weighted Option B) wired on CPU + unified GPU with dual gating; Paper Ready UI still working.
- Plans updated with per‑phase checklists and progress notes (Docs/PathToParity/PathToParityPlan.md).

## Recent Changes (Key Files)
- Exemplar audits
  - Decode/wrap parity + diagnostics retained; library fingerprint logged; all audits green.
  - Artifacts: Docs/Validation/ExemplarAudit/; summary: Docs/Validation/EXEMPLAR_VALIDATION_COMPLETE.md.
- Fold/Orgeny (STG‑06)
  - Arrays + params + accessors added; per‑vertex compute + coverage logs + history snapshots wired.
  - TectonicSimulationService.h: thresholds/accessors/backing arrays.
  - TectonicSimulationService.cpp: ComputeFoldDirectionsAndClasses(), call in AdvanceSteps, init/prime, history capture/restore.
- Anisotropy (STG‑07, Option B)
  - Enum + params: StageBAmplificationTypes.h.
  - CVar: r.PlanetaryCreation.StageBEnableAnisotropy; per‑dispatch parameter.
  - CPU fallback gating + scaling + logs.
  - RDG wiring + snapshot/hash extension + [Aniso] logs.
  - Shader class‑weighted scale pre-output: StageB_Unified_V2.usf.
  - Sanity test (non‑blocking): StageBAnisotropySanityTest.cpp.
- Paper Ready UI
  - Handler/preset: SPTectonicToolPanel.cpp; logs “[PaperReady] Applied …”. Export button preflights Paper Ready.

## How To Verify Quickly
- Milestone 3 (CPU/NullRHI):
  - powershell.exe -ExecutionPolicy Bypass -File ".\Scripts\RunMilestone3Tests.ps1" -ArchiveLogs
- Heartbeat (profiling + coverage):
  - powershell.exe -ExecutionPolicy Bypass -File ".\Scripts\RunStageBHeartbeat.ps1" -ThrottleMs 50
  - Expect: [FoldDir] Coverage=… and, if enabled, [Aniso] Enabled=1 … Coverage=…%
- Paper Ready (editor)
  - Click “Paper Ready”; Output Log: “[PaperReady] Applied (Seed=42 RenderLOD=… StageBReady=true)”.

## Current Defaults / Toggles
- Anisotropy (default OFF):
  - Global CVar: r.PlanetaryCreation.StageBEnableAnisotropy (0/1)
  - Per‑dispatch: FStageB_UnifiedParameters.bEnableAnisotropy (tests override)
  - Scaling: class‑weighted blend (Option B); continental only
- Edge/export safety: NullRHI baseline cap (512×256), tiling for larger; force‑large flag required; large exports hazardous.
- GPU automation guarded; default to CPU/NullRHI unless explicitly allowed; always throttle Stage B in GPU suites.

## Next Actions (Phase 1.5 Remainder)
- Anisotropy validation
  - Enable CVar; re‑run unified GPU parity (< 0.1 m) and exemplar audits (stay green);
  - Capture [Aniso] config + any added cost (target ≲ 1 ms for Option B).
- STG‑08: Re‑enable conservative hydraulic erosion post Stage B
  - Verify anisotropy survives erosion; budget ≈ 1.7 ms (LOD 7).
- QLT hardening
  - QLT‑05: Ridge transect metric; QLT‑06: orogeny belt alignment; QLT‑07: CPU/GPU UV parity harness; integrate into Milestone 3.
- Legacy test alignment
  - Align/retire PlanetaryCreation.Milestone6.GPU.ContinentalParity to unified parity harness (< 0.1 m).
- Durability + LOD captures
  - Time‑lapse (20–50 steps) stability; LOD 6/7 forced exports archived; PNG round‑trip sanity.

## Commands To Resume
- Build editor target:
  - powershell.exe -Command "& 'C:\\Program Files\\Epic Games\\UE_5.5\\Engine\\Binaries\\DotNET\\UnrealBuildTool\\UnrealBuildTool.exe' PlanetaryCreationEditor Win64 Development -project='C:\\Users\\Michael\\Documents\\Unreal Projects\\PlanetaryCreation\\PlanetaryCreation.uproject' -WaitMutex -FromMsBuild"
- Milestone 3 (CPU/NullRHI):
  - powershell.exe -ExecutionPolicy Bypass -File ".\Scripts\RunMilestone3Tests.ps1" -ArchiveLogs
- Heartbeat:
  - powershell.exe -ExecutionPolicy Bypass -File ".\Scripts\RunStageBHeartbeat.ps1" -ThrottleMs 50
- (If GPU allowed) Unified GPU parity with anisotropy ON:
  - powershell.exe -Command "& 'C:\\Program Files\\Epic Games\\UE_5.5\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe' 'C:\\Users\\Michael\\Documents\\Unreal Projects\\PlanetaryCreation\\PlanetaryCreation.uproject' -SetCVar='r.PlanetaryCreation.StageBEnableAnisotropy=1,r.PlanetaryCreation.StageBProfiling=1,r.PlanetaryCreation.StageBThrottleMs=50' -ExecCmds='Automation RunTests PlanetaryCreation.StageB.UnifiedGPUParity' -TestExit='Automation Test Queue Empty' -unattended -nop4 -nosplash -log"

## Log Markers To Grep
- “[PaperReady] Applied …”
- “[FoldDir] Coverage=… Valid=… Active=… Nascent=… Dormant=… None=…”
- “[Aniso] Enabled=… Mode=ClassOnly Along=… Across=… ClassWeights=[…] Coverage=…%”
- “[StageB][Profile] …” timing lines
- “[StageB][ExemplarVersion] …” and “[ExemplarLibrary] Fingerprint=0x…”.

## Artifacts / Docs
- Exemplar audits: Docs/Validation/ExemplarAudit/
- Audit summary: Docs/Validation/EXEMPLAR_VALIDATION_COMPLETE.md
- Edge fix report: Docs/Validation/EDGE_SPIKE_FIX_VALIDATION.md
- Plan and checklists: Docs/PathToParity/PathToParityPlan.md
- QuickStart: Docs/Automation_QuickStart.md

## Risks / Watch Items
- Build hygiene: stale binaries can cause false negatives—clean rebuild after engine/slate changes.
- GPU suites: run only with throttles + allow flags; keep CPU/NullRHI for automation.
- Snapshot identity: anisotropy params included in hashing; verify with logs if behavior seems stale.
