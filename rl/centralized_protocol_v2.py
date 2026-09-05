"""Version-2 feature schema for centralized DQN.

V2 replaces categorical integer features with one-hot/type features while keeping
``action_id = (ue_id - 1) * NUM_APS + ap_id`` so C++ action decoding remains
compatible with the existing centralized_dqn path.
"""

from __future__ import annotations

NUM_UES = 80
NUM_APS = 3

APP_BROWSER = 1
APP_VIDEO = 2
APP_VOICE = 3
APP_GAME = 4

TP_NEED_MBPS = {
    # Match APConstants::BROWSER_REQUIRED_TP in the current ns-3 code.
    APP_BROWSER: 3.0,
    APP_VIDEO: 8.0,
    APP_VOICE: 0.0,
    APP_GAME: 0.0,
}
RTT_NEED_MS = {
    APP_BROWSER: 0.0,
    APP_VIDEO: 0.0,
    APP_VOICE: 100.0,
    APP_GAME: 40.0,
}

UE_FEATURES_V2 = [
    "ue_id_normalized",
    "current_is_ap0",
    "current_is_ap1",
    "current_is_ap2",
    "app_browser",
    "app_video",
    "app_voice",
    "app_game",
    "is_tp_app",
    "is_rtt_app",
    "tp_need_mbps",
    "rtt_need_ms",
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
AP_FEATURES_V2 = [
    "ap_id_is_0",
    "ap_id_is_1",
    "ap_id_is_2",
    "ap_type_5g",
    "ap_type_wifi",
    "num_users",
    "monitor_rtt",
    "mean_tp",
    "mean_satisfaction",
    "num_unsatisfied",
]
GLOBAL_FEATURES_V2 = [
    "cycle_id",
    "harmonic_mean",
    "num_unsatisfied_users",
    "previous_switch_count",
    "previous_num_degraded_users",
    "previous_measured_reward",
]

UE_DIM_V2 = len(UE_FEATURES_V2)
AP_DIM_V2 = len(AP_FEATURES_V2)
GLOBAL_DIM_V2 = len(GLOBAL_FEATURES_V2)
STATE_DIM_V2 = NUM_UES * UE_DIM_V2 + NUM_APS * AP_DIM_V2 + GLOBAL_DIM_V2
ACTION_DIM = NUM_UES * NUM_APS
SCHEMA_VERSION_V2 = "centralized_state_v2_onehot"


def app_features(app_type: int) -> list[float]:
    app = int(app_type)
    is_tp = 1.0 if app in (APP_BROWSER, APP_VIDEO) else 0.0
    is_rtt = 1.0 if app in (APP_VOICE, APP_GAME) else 0.0
    return [
        1.0 if app == APP_BROWSER else 0.0,
        1.0 if app == APP_VIDEO else 0.0,
        1.0 if app == APP_VOICE else 0.0,
        1.0 if app == APP_GAME else 0.0,
        is_tp,
        is_rtt,
        float(TP_NEED_MBPS.get(app, 0.0)),
        float(RTT_NEED_MS.get(app, 0.0)),
    ]


def current_ap_onehot(current_bs_id: int) -> list[float]:
    return [1.0 if int(current_bs_id) == ap else 0.0 for ap in range(NUM_APS)]


def ap_static_features(ap_id: int) -> list[float]:
    ap = int(ap_id)
    return [
        1.0 if ap == 0 else 0.0,
        1.0 if ap == 1 else 0.0,
        1.0 if ap == 2 else 0.0,
        1.0 if ap == 0 else 0.0,  # AP0 is 5G gNB.
        1.0 if ap in (1, 2) else 0.0,  # AP1/AP2 are Wi-Fi APs.
    ]


def feature_schema() -> dict:
    return {
        "schema_version": SCHEMA_VERSION_V2,
        "num_ues": NUM_UES,
        "num_aps": NUM_APS,
        "ue_features": UE_FEATURES_V2,
        "ap_features": AP_FEATURES_V2,
        "global_features": GLOBAL_FEATURES_V2,
        "ue_dim": UE_DIM_V2,
        "ap_dim": AP_DIM_V2,
        "global_dim": GLOBAL_DIM_V2,
        "state_dim": STATE_DIM_V2,
        "action_dim": ACTION_DIM,
    }


def state_to_vector_v2(state: dict) -> list[float]:
    features = state.get("features")
    if isinstance(features, list):
        out = [float(x) for x in features]
        if len(out) < STATE_DIM_V2:
            out.extend([0.0] * (STATE_DIM_V2 - len(out)))
        return out[:STATE_DIM_V2]

    out: list[float] = []
    for ue in state.get("ues", []):
        for name in UE_FEATURES_V2:
            out.append(float(ue.get(name, 0.0)))
    while len(out) < NUM_UES * UE_DIM_V2:
        out.append(0.0)
    out = out[: NUM_UES * UE_DIM_V2]

    ap_part: list[float] = []
    for ap in state.get("aps", []):
        for name in AP_FEATURES_V2:
            ap_part.append(float(ap.get(name, 0.0)))
    while len(ap_part) < NUM_APS * AP_DIM_V2:
        ap_part.append(0.0)
    out.extend(ap_part[: NUM_APS * AP_DIM_V2])

    global_state = state.get("global", state)
    for name in GLOBAL_FEATURES_V2:
        out.append(float(global_state.get(name, 0.0)))
    return out[:STATE_DIM_V2]
