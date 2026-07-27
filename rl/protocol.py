"""TCP JSON protocol for ns-3 online DQN AP selection.

Messages are newline-delimited JSON. ns-3 sends one `act` request per
(candidate UE, step). The Python server returns one selected BS id.

State feature order (checkpoint-compatible 9 features):
cycle_id, current_bs_id, app_type, tp_mbps, rtt_ms, satisfaction,
num_users_on_current_bs, harmonic_mean, num_unsatisfied_users
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
]


def state_to_vector(state: dict) -> list[float]:
    return [float(state.get(name, 0.0)) for name in STATE_FEATURES]


def make_error(message: str) -> dict:
    return {"type": "error", "message": message}
