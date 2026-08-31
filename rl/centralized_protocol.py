"""Protocol helpers for centralized DQN AP selection.

The ns-3 side sends one full-network state per decision step.  The state uses a
fixed 80-UE padding layout so checkpoints are stable for OUTPUT/80 experiments.
"""

from __future__ import annotations

NUM_UES = 80
NUM_APS = 3
UE_FEATURES = [
    "ue_id_normalized",
    "current_bs_id",
    "app_type",
    "tp_mbps",
    "rtt_ms",
    "satisfaction",
    "measurement_valid",
    "handover_cooldown_flag",
    "last_switch_age",
    "estimated_satisfaction_if_ap0",
    "estimated_satisfaction_if_ap1",
    "estimated_satisfaction_if_ap2",
    "estimated_h_delta_if_ap0",
    "estimated_h_delta_if_ap1",
    "estimated_h_delta_if_ap2",
    "best_estimated_h_delta",
]
AP_FEATURES = [
    "num_users",
    "monitor_rtt",
    "mean_tp",
    "mean_satisfaction",
    "num_unsatisfied",
]
GLOBAL_FEATURES = [
    "cycle_id",
    "harmonic_mean",
    "num_unsatisfied_users",
    "previous_switch_count",
    "previous_num_degraded_users",
    "previous_measured_reward",
]
STATE_DIM = NUM_UES * len(UE_FEATURES) + NUM_APS * len(AP_FEATURES) + len(GLOBAL_FEATURES)
ACTION_DIM = NUM_UES * NUM_APS


def state_to_vector(state: dict) -> list[float]:
    """Return fixed-length centralized state vector.

    Current C++ sends ``state.features`` directly.  The fallback paths make the
    server easier to test by hand and keep the protocol explicit.
    """
    features = state.get("features")
    if isinstance(features, list):
        out = [float(x) for x in features]
        if len(out) < STATE_DIM:
            out.extend([0.0] * (STATE_DIM - len(out)))
        return out[:STATE_DIM]

    out: list[float] = []
    for ue in state.get("ues", []):
        for name in UE_FEATURES:
            out.append(float(ue.get(name, 0.0)))
    while len(out) < NUM_UES * len(UE_FEATURES):
        out.append(0.0)
    out = out[: NUM_UES * len(UE_FEATURES)]

    ap_part: list[float] = []
    for ap in state.get("aps", []):
        for name in AP_FEATURES:
            ap_part.append(float(ap.get(name, 0.0)))
    while len(ap_part) < NUM_APS * len(AP_FEATURES):
        ap_part.append(0.0)
    out.extend(ap_part[: NUM_APS * len(AP_FEATURES)])

    global_state = state.get("global", state)
    for name in GLOBAL_FEATURES:
        out.append(float(global_state.get(name, 0.0)))
    return out[:STATE_DIM]


def make_error(message: str) -> dict:
    return {"type": "error", "message": message}
