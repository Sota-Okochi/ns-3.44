from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

import pandas as pd

from centralized_protocol import ACTION_DIM, AP_FEATURES, GLOBAL_FEATURES, NUM_APS, NUM_UES, STATE_DIM, UE_FEATURES
from centralized_protocol_v2 import (
    AP_FEATURES_V2,
    GLOBAL_FEATURES_V2,
    SCHEMA_VERSION_V2,
    STATE_DIM_V2,
    UE_FEATURES_V2,
    ap_static_features,
    app_features,
    current_ap_onehot,
    feature_schema as feature_schema_v2,
)

MASTER_PREFIX = "master_log_"
TEACHER_PREFIX = "centralized_teacher_log_"


def parse_seed_timestamp(path: Path, prefix: str) -> tuple[int, str]:
    m = re.match(rf"{re.escape(prefix)}(\d+)_(\d{{8}}_\d{{6}})\.csv$", path.name)
    if not m:
        raise ValueError(f"unexpected file name: {path.name}")
    return int(m.group(1)), m.group(2)


def collect_pairs(inputs: list[str], recursive: bool = True) -> list[tuple[Path, Path]]:
    files: list[Path] = []
    for raw in inputs:
        p = Path(raw)
        if p.is_dir():
            pattern = "**/*.csv" if recursive else "*.csv"
            files.extend(sorted(p.glob(pattern)))
        elif p.is_file():
            files.append(p)
        else:
            raise FileNotFoundError(raw)

    masters: dict[tuple[int, str], Path] = {}
    teachers: dict[tuple[int, str], Path] = {}
    for f in files:
        if f.name.startswith(MASTER_PREFIX):
            masters[parse_seed_timestamp(f, MASTER_PREFIX)] = f
        elif f.name.startswith(TEACHER_PREFIX):
            teachers[parse_seed_timestamp(f, TEACHER_PREFIX)] = f

    pairs = [(masters[k], teachers[k]) for k in sorted(masters.keys() & teachers.keys())]
    if not pairs:
        raise ValueError("no master_log / centralized_teacher_log pairs found")
    return pairs


def ap_value(values: dict[int, float], idx: int, default: float = 0.0) -> float:
    return float(values.get(idx, default))


def build_state_vector(cycle_df: pd.DataFrame, step_assignment: dict[int, int], teacher_row: pd.Series) -> list[float]:
    rows = {int(r.ue_id): r for r in cycle_df.itertuples(index=False)}
    features: list[float] = []

    monitor_rtt = {
        0: float(cycle_df["monitor_rtt_ap0"].iloc[0]) if "monitor_rtt_ap0" in cycle_df else 0.0,
        1: float(cycle_df["monitor_rtt_ap1"].iloc[0]) if "monitor_rtt_ap1" in cycle_df else 0.0,
        2: float(cycle_df["monitor_rtt_ap2"].iloc[0]) if "monitor_rtt_ap2" in cycle_df else 0.0,
    }

    users_per_ap = {0: 0, 1: 0, 2: 0}
    tp_sum = {0: 0.0, 1: 0.0, 2: 0.0}
    tp_count = {0: 0, 1: 0, 2: 0}
    sat_sum = {0: 0.0, 1: 0.0, 2: 0.0}
    sat_count = {0: 0, 1: 0, 2: 0}
    unsat = {0: 0, 1: 0, 2: 0}

    # First pass for AP aggregate features under the step assignment.
    for ue_id, row in rows.items():
        bs = int(step_assignment.get(ue_id, int(row.current_bs_id)))
        if bs not in users_per_ap:
            continue
        users_per_ap[bs] += 1
        tp = float(getattr(row, "tp_mbps", 0.0))
        if tp > 0.0:
            tp_sum[bs] += tp
            tp_count[bs] += 1
        sat = float(getattr(row, f"estimated_satisfaction_if_ap{bs}", getattr(row, "satisfaction", 0.0)))
        sat_sum[bs] += sat
        sat_count[bs] += 1
        if sat < 0.5:
            unsat[bs] += 1

    for idx in range(NUM_UES):
        ue_id = idx + 1
        row = rows.get(ue_id)
        exists = row is not None
        current_bs = int(step_assignment.get(ue_id, int(row.current_bs_id) if exists else -1))
        app_type = int(row.app_type) if exists else 0
        tp_mbps = float(row.tp_mbps) if exists else 0.0
        rtt_ms = ap_value(monitor_rtt, current_bs, 0.0) if exists else 0.0
        satisfaction = (
            float(getattr(row, f"estimated_satisfaction_if_ap{current_bs}", row.satisfaction))
            if exists and 0 <= current_bs < NUM_APS
            else 0.0
        )
        measurement_valid = float(row.measurement_valid) if exists and hasattr(row, "measurement_valid") else 0.0
        # Offline logs do not contain exact cooldown state for arbitrary step assignment.
        handover_cooldown_flag = 0.0
        last_switch_age = 0.0
        est_s = [0.0, 0.0, 0.0]
        est_d = [0.0, 0.0, 0.0]
        if exists:
            for ap in range(NUM_APS):
                est_s[ap] = float(getattr(row, f"estimated_satisfaction_if_ap{ap}", 0.0))
                est_d[ap] = float(getattr(row, f"estimated_h_delta_if_ap{ap}", 0.0))
        best_d = max([est_d[ap] for ap in range(NUM_APS) if ap != current_bs] or [0.0])

        features.extend(
            [
                float(idx) / float(max(NUM_UES - 1, 1)),
                float(current_bs),
                float(app_type),
                tp_mbps,
                rtt_ms,
                satisfaction,
                measurement_valid,
                handover_cooldown_flag,
                last_switch_age,
                est_s[0],
                est_s[1],
                est_s[2],
                est_d[0],
                est_d[1],
                est_d[2],
                best_d,
            ]
        )

    for ap in range(NUM_APS):
        features.extend(
            [
                float(users_per_ap[ap]),
                float(monitor_rtt[ap]),
                tp_sum[ap] / float(tp_count[ap]) if tp_count[ap] else 0.0,
                sat_sum[ap] / float(sat_count[ap]) if sat_count[ap] else 0.0,
                float(unsat[ap]),
            ]
        )

    features.extend(
        [
            float(teacher_row.cycle_id),
            float(teacher_row.h_before_step_estimated),
            float((cycle_df["num_unsatisfied_users"].iloc[0] if "num_unsatisfied_users" in cycle_df else 0.0)),
            float((cycle_df["switch_count"].iloc[0] if "switch_count" in cycle_df else 0.0)),
            float((cycle_df["num_degraded_users"].iloc[0] if "num_degraded_users" in cycle_df else 0.0)),
            float((cycle_df["measured_reward"].iloc[0] if "measured_reward" in cycle_df else 0.0)),
        ]
    )

    if len(features) != STATE_DIM:
        raise ValueError(f"state_dim mismatch: got {len(features)}, expected {STATE_DIM}")
    return features


def build_state_vector_v2(cycle_df: pd.DataFrame, step_assignment: dict[int, int], teacher_row: pd.Series) -> list[float]:
    rows = {int(r.ue_id): r for r in cycle_df.itertuples(index=False)}
    features: list[float] = []

    monitor_rtt = {
        0: float(cycle_df["monitor_rtt_ap0"].iloc[0]) if "monitor_rtt_ap0" in cycle_df else 0.0,
        1: float(cycle_df["monitor_rtt_ap1"].iloc[0]) if "monitor_rtt_ap1" in cycle_df else 0.0,
        2: float(cycle_df["monitor_rtt_ap2"].iloc[0]) if "monitor_rtt_ap2" in cycle_df else 0.0,
    }

    users_per_ap = {0: 0, 1: 0, 2: 0}
    tp_sum = {0: 0.0, 1: 0.0, 2: 0.0}
    tp_count = {0: 0, 1: 0, 2: 0}
    sat_sum = {0: 0.0, 1: 0.0, 2: 0.0}
    sat_count = {0: 0, 1: 0, 2: 0}
    unsat = {0: 0, 1: 0, 2: 0}

    for ue_id, row in rows.items():
        bs = int(step_assignment.get(ue_id, int(row.current_bs_id)))
        if bs not in users_per_ap:
            continue
        users_per_ap[bs] += 1
        tp = float(getattr(row, "tp_mbps", 0.0))
        if tp > 0.0:
            tp_sum[bs] += tp
            tp_count[bs] += 1
        sat = float(getattr(row, f"estimated_satisfaction_if_ap{bs}", getattr(row, "satisfaction", 0.0)))
        sat_sum[bs] += sat
        sat_count[bs] += 1
        if sat < 0.5:
            unsat[bs] += 1

    for idx in range(NUM_UES):
        ue_id = idx + 1
        row = rows.get(ue_id)
        exists = row is not None
        current_bs = int(step_assignment.get(ue_id, int(row.current_bs_id) if exists else -1))
        app_type = int(row.app_type) if exists else 0
        tp_mbps = float(row.tp_mbps) if exists else 0.0
        rtt_ms = monitor_rtt.get(current_bs, 0.0) if exists else 0.0
        satisfaction = (
            float(getattr(row, f"estimated_satisfaction_if_ap{current_bs}", row.satisfaction))
            if exists and 0 <= current_bs < NUM_APS
            else 0.0
        )
        measurement_valid = float(row.measurement_valid) if exists and hasattr(row, "measurement_valid") else 0.0
        est_s = [0.0, 0.0, 0.0]
        est_d = [0.0, 0.0, 0.0]
        if exists:
            for ap in range(NUM_APS):
                est_s[ap] = float(getattr(row, f"estimated_satisfaction_if_ap{ap}", 0.0))
                est_d[ap] = float(getattr(row, f"estimated_h_delta_if_ap{ap}", 0.0))
        best_d = max([est_d[ap] for ap in range(NUM_APS) if ap != current_bs] or [0.0])
        features.extend(
            [
                float(idx) / float(max(NUM_UES - 1, 1)),
                *current_ap_onehot(current_bs),
                *app_features(app_type),
                tp_mbps,
                rtt_ms,
                satisfaction,
                measurement_valid,
                0.0,  # Offline logs do not contain exact cooldown state for arbitrary step assignment.
                0.0,
                est_s[0],
                est_s[1],
                est_s[2],
                est_d[0],
                est_d[1],
                est_d[2],
                best_d,
            ]
        )

    for ap in range(NUM_APS):
        features.extend(
            [
                *ap_static_features(ap),
                float(users_per_ap[ap]),
                float(monitor_rtt[ap]),
                tp_sum[ap] / float(tp_count[ap]) if tp_count[ap] else 0.0,
                sat_sum[ap] / float(sat_count[ap]) if sat_count[ap] else 0.0,
                float(unsat[ap]),
            ]
        )

    features.extend(
        [
            float(teacher_row.cycle_id),
            float(teacher_row.h_before_step_estimated),
            float((cycle_df["num_unsatisfied_users"].iloc[0] if "num_unsatisfied_users" in cycle_df else 0.0)),
            float((cycle_df["switch_count"].iloc[0] if "switch_count" in cycle_df else 0.0)),
            float((cycle_df["num_degraded_users"].iloc[0] if "num_degraded_users" in cycle_df else 0.0)),
            float((cycle_df["measured_reward"].iloc[0] if "measured_reward" in cycle_df else 0.0)),
        ]
    )

    if len(features) != STATE_DIM_V2:
        raise ValueError(f"v2 state_dim mismatch: got {len(features)}, expected {STATE_DIM_V2}")
    return features


def build_dataset(
    pairs: list[tuple[Path, Path]],
    min_measured_delta: float | None,
    min_step_delta: float | None,
    weight_lambda: float,
    schema_version: str,
) -> tuple[pd.DataFrame, pd.DataFrame]:
    samples: list[dict] = []
    summaries: list[dict] = []
    use_v2 = schema_version in {"v2", "v2_onehot", SCHEMA_VERSION_V2, "centralized_state_v2"}
    state_dim = STATE_DIM_V2 if use_v2 else STATE_DIM
    feature_cols = [f"f{i}" for i in range(state_dim)]

    for run_index, (master_path, teacher_path) in enumerate(pairs):
        master = pd.read_csv(master_path)
        teacher = pd.read_csv(teacher_path)
        seed = int(master["seed"].iloc[0]) if not master.empty else parse_seed_timestamp(master_path, MASTER_PREFIX)[0]
        if teacher.empty:
            summaries.append({"seed": seed, "samples": 0, "reason": "empty_teacher", "master_file": str(master_path), "teacher_file": str(teacher_path)})
            continue

        cycle_h = master.groupby("cycle_id")["harmonic_mean"].first()
        measured_delta = float(cycle_h.iloc[-1] - cycle_h.iloc[0]) if len(cycle_h) else 0.0
        if min_measured_delta is not None and measured_delta < min_measured_delta:
            summaries.append({"seed": seed, "samples": 0, "reason": "filtered_measured_delta", "measured_delta": measured_delta, "master_file": str(master_path), "teacher_file": str(teacher_path)})
            continue

        n_before = len(teacher)
        if min_step_delta is not None:
            teacher = teacher[teacher["estimated_marginal_delta"].astype(float) >= min_step_delta].copy()

        run_samples = 0
        for cycle_id, cycle_teacher in teacher.groupby("cycle_id"):
            cycle_df = master[master["cycle_id"].astype(int) == int(cycle_id)].copy()
            if cycle_df.empty:
                continue
            cycle_df = cycle_df.sort_values("ue_id")
            step_assignment = {int(r.ue_id): int(r.current_bs_id) for r in cycle_df.itertuples(index=False)}
            for row in cycle_teacher.sort_values("step_id").itertuples(index=False):
                row_s = pd.Series(row._asdict())
                action_id = int(row_s["action_id"])
                if action_id < 0 or action_id >= ACTION_DIM:
                    continue
                x = build_state_vector_v2(cycle_df, step_assignment, row_s) if use_v2 else build_state_vector(cycle_df, step_assignment, row_s)
                rec = {col: val for col, val in zip(feature_cols, x)}
                rec.update(
                    {
                        "expert_action_id": action_id,
                        "target_ue_id": int(row_s["target_ue_id"]),
                        "selected_bs_id": int(row_s["selected_bs_id"]),
                        "seed": seed,
                        "source_run_index": run_index,
                        "cycle_id": int(row_s["cycle_id"]),
                        "step_id": int(row_s["step_id"]),
                        "estimated_marginal_delta": float(row_s["estimated_marginal_delta"]),
                        "h_before_step_estimated": float(row_s["h_before_step_estimated"]),
                        "h_after_step_estimated": float(row_s["h_after_step_estimated"]),
                        "h_after_final_estimated": float(row_s["h_after_final_estimated"]),
                        "sample_weight": 1.0 + weight_lambda * max(0.0, float(row_s["estimated_marginal_delta"])),
                        "source_master_log": str(master_path),
                        "source_teacher_log": str(teacher_path),
                    }
                )
                samples.append(rec)
                run_samples += 1
                step_assignment[int(row_s["target_ue_id"])] = int(row_s["selected_bs_id"])

        summaries.append(
            {
                "seed": seed,
                "samples": run_samples,
                "teacher_rows_before_filter": n_before,
                "teacher_rows_after_filter": len(teacher),
                "measured_delta": measured_delta,
                "master_file": str(master_path),
                "teacher_file": str(teacher_path),
                "reason": "ok",
            }
        )

    if not samples:
        raise ValueError("no samples exported")
    return pd.DataFrame(samples), pd.DataFrame(summaries)


def main() -> None:
    parser = argparse.ArgumentParser(description="Export centralized DQN BC dataset from logistic master/teacher logs.")
    parser.add_argument("--inputs", nargs="+", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--summary-output", default=None)
    parser.add_argument("--meta-output", default=None)
    parser.add_argument("--min-measured-delta", type=float, default=None, help="Drop runs whose final H - initial H is below this value.")
    parser.add_argument("--min-step-delta", type=float, default=None, help="Drop teacher actions whose estimated_marginal_delta is below this value.")
    parser.add_argument("--weight-lambda", type=float, default=20.0)
    parser.add_argument("--schema-version", choices=["v1", "v2_onehot", "centralized_state_v1", "centralized_state_v2_onehot"], default="v1")
    parser.add_argument("--no-recursive", action="store_true")
    args = parser.parse_args()

    pairs = collect_pairs(args.inputs, recursive=not args.no_recursive)
    data, summary = build_dataset(pairs, args.min_measured_delta, args.min_step_delta, args.weight_lambda, args.schema_version)

    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    summary_out = Path(args.summary_output) if args.summary_output else out.with_suffix(out.suffix + ".summary.csv")
    meta_out = Path(args.meta_output) if args.meta_output else out.with_suffix(out.suffix + ".meta.json")
    summary_out.parent.mkdir(parents=True, exist_ok=True)
    meta_out.parent.mkdir(parents=True, exist_ok=True)

    data.to_csv(out, index=False)
    summary.to_csv(summary_out, index=False)
    use_v2 = args.schema_version in {"v2_onehot", "centralized_state_v2_onehot"}
    state_dim = STATE_DIM_V2 if use_v2 else STATE_DIM
    meta = {
        "inputs": args.inputs,
        "num_pairs": len(pairs),
        "num_samples": int(len(data)),
        "schema_version": SCHEMA_VERSION_V2 if use_v2 else "centralized_state_v1",
        "state_dim": state_dim,
        "action_dim": ACTION_DIM,
        "num_ues": NUM_UES,
        "num_aps": NUM_APS,
        "ue_features": UE_FEATURES_V2 if use_v2 else UE_FEATURES,
        "ap_features": AP_FEATURES_V2 if use_v2 else AP_FEATURES,
        "global_features": GLOBAL_FEATURES_V2 if use_v2 else GLOBAL_FEATURES,
        "feature_schema": feature_schema_v2() if use_v2 else {"schema_version": "centralized_state_v1", "ue_features": UE_FEATURES, "ap_features": AP_FEATURES, "global_features": GLOBAL_FEATURES},
        "feature_columns": [f"f{i}" for i in range(state_dim)],
        "label_column": "expert_action_id",
        "min_measured_delta": args.min_measured_delta,
        "min_step_delta": args.min_step_delta,
        "weight_lambda": args.weight_lambda,
        "label_counts_selected_bs": {str(k): int(v) for k, v in data["selected_bs_id"].value_counts().sort_index().items()},
        "unique_actions": int(data["expert_action_id"].nunique()),
        "summary_output": str(summary_out),
    }
    meta_out.write_text(json.dumps(meta, ensure_ascii=False, indent=2) + "\n")
    print(f"exported samples={len(data)} pairs={len(pairs)} -> {out}")
    print(f"summary -> {summary_out}")
    print(f"meta -> {meta_out}")


if __name__ == "__main__":
    main()
