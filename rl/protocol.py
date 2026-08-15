"""TCP JSON protocol for ns-3 online DQN AP selection.

Messages are newline-delimited JSON. ns-3 sends one ``act`` request per
(candidate UE, step). The Python server returns one selected BS id.

State feature order for the extended OnlineDQN state.  The C++ side already
sends these fields from ``BuildDqnStateJson()``; missing values default to 0.0
for backward-compatible experiments.
"""

STATE_FEATURES = [
    "cycle_id",
    "current_bs_id",
    "app_type",
    "tp_mbps",
    "rtt_ms",
    "satisfaction",
    "num_users_on_current_bs",
    "harmonic_mean",
    "num_unsatisfied_users",
    "candidate_type",
    "num_users_ap0",
    "num_users_ap1",
    "num_users_ap2",
    "monitor_rtt_ap0",
    "monitor_rtt_ap1",
    "monitor_rtt_ap2",
    "estimated_satisfaction_if_ap0",
    "estimated_satisfaction_if_ap1",
    "estimated_satisfaction_if_ap2",
    "estimated_h_delta_if_ap0",
    "estimated_h_delta_if_ap1",
    "estimated_h_delta_if_ap2",
    "best_estimated_h_delta",
    "effective_max_switches",
    "applied_switches_in_cycle",
    "remaining_switch_budget",
]


def state_to_vector(state: dict) -> list[float]:
    return [float(state.get(name, 0.0)) for name in STATE_FEATURES]


def make_error(message: str) -> dict:
    return {"type": "error", "message": message}
