import sys
import unreal


def main() -> int:
    service = unreal.get_editor_subsystem(unreal.TectonicSimulationService)
    if service is None:
        unreal.log_error("[WarmStageB] TectonicSimulationService unavailable")
        return 1

    unreal.log("[WarmStageB] Forcing Stage B rebuild with CPU amplification enabled")
    unreal.SystemLibrary.execute_console_command(None, "r.PlanetaryCreation.SkipCPUAmplification 0")
    service.force_stage_b_amplification_rebuild("WarmStageB")

    if not service.is_stage_b_amplification_ready():
        reason = service.get_stage_b_amplification_not_ready_reason()
        unreal.log_error(f"[WarmStageB] Stage B failed to reach ready state (Reason={reason})")
        return 2

    serial = service.get_oceanic_amplification_data_serial()
    unreal.log(f"[WarmStageB] Stage B ready (Serial={serial})")
    unreal.SystemLibrary.execute_console_command(None, "r.PlanetaryCreation.SkipCPUAmplification 1")

    return 0


if __name__ == "__main__":
    sys.exit(main())
