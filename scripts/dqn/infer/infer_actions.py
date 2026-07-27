#!/usr/bin/env python3
"""Run DQN inference and write action CSV for ns-3 evaluation.

Multi-DQN extends the original one-UE DQN pipeline: for each cycle, select
multiple UE candidates, run the same UE-wise Q network, and write up to
``max_switches`` actions in the new multi-action CSV format.
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path
from typing import Dict, List, Sequence, Set

import pandas as pd
import torch
from torch import nn


DEFAULT_FEATURE_COLUMNS = [
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

REQUIRED_MASTER_COLUMNS = set(DEFAULT_FEATURE_COLUMNS + [
    "seed",
    "cycle_id",
    "ue_id",
    "target_ue_flag",
])

OUTPUT_COLUMNS = [
    "seed",
    "cycle_id",
    "step_id",
    "target_ue_id",
    "current_bs_id",
    "selected_bs_id",
    "advantage",
    "q_bs0",
    "q_bs1",
    "q_bs2",
]


class QNetwork(nn.Module):
    def __init__(self, input_dim: int, output_dim: int, hidden_dims: Sequence[int]):
        super().__init__()
        layers: List[nn.Module] = []
        prev_dim = input_dim
        for hidden_dim in hidden_dims:
            layers.append(nn.Linear(prev_dim, hidden_dim))
            layers.append(nn.ReLU())
            prev_dim = hidden_dim
        layers.append(nn.Linear(prev_dim, output_dim))
        self.net = nn.Sequential(*layers)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return self.net(x)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Infer Multi-DQN selected_bs_id actions from master_log CSV.")
    parser.add_argument("--input", "-i", required=True, help="Input master_log CSV path.")
    parser.add_argument("--checkpoint", "-c", required=True, help="DQN checkpoint .pt path.")
    parser.add_argument(
        "--output",
        "-o",
        help="Output action CSV path. If omitted, a default path under episodes/dqn/actions is used.",
    )
    parser.add_argument(
        "--candidate-mode",
        "--target-mode",
        dest="candidate_mode",
        choices=["flag_only", "unsatisfied", "top_k_low", "all_users", "switched_only"],
        default="top_k_low",
        help=(
            "Candidate UE selection mode. --target-mode is kept as a deprecated alias. "
            "Default: top_k_low."
        ),
    )
    parser.add_argument("--satisfaction-threshold", type=float, default=0.5)
    parser.add_argument("--max-switches", type=int, default=8)
    parser.add_argument("--min-advantage", type=float, default=0.0)
    parser.add_argument(
        "--exclude-current-bs-action",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Exclude candidates whose argmax action is the current BS. Default: true.",
    )
    parser.add_argument(
        "--sequential-inference",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Update current_bs_id and num_users_on_current_bs after each selected action. Default: true.",
    )
    parser.add_argument("--device", default="auto", choices=["auto", "cpu", "cuda"])
    return parser.parse_args()


def get_device(name: str) -> torch.device:
    if name == "cpu":
        return torch.device("cpu")
    if name == "cuda":
        if not torch.cuda.is_available():
            raise RuntimeError("CUDA was requested but is not available.")
        return torch.device("cuda")
    return torch.device("cuda" if torch.cuda.is_available() else "cpu")


def load_checkpoint(path: Path, device: torch.device) -> Dict:
    # weights_only=False is intentional: this checkpoint stores metadata in addition to tensors.
    return torch.load(path, map_location=device, weights_only=False)


def load_master_log(path: Path) -> pd.DataFrame:
    df = pd.read_csv(path)
    missing = sorted(REQUIRED_MASTER_COLUMNS - set(df.columns))
    if missing:
        raise ValueError(f"{path}: missing required columns: {missing}")
    if "switch_flag" not in df.columns:
        df["switch_flag"] = 0
    return df


def numeric_matrix(df: pd.DataFrame, columns: Sequence[str]) -> torch.Tensor:
    values = df.loc[:, columns].apply(pd.to_numeric, errors="raise").astype("float32")
    return torch.tensor(values.to_numpy(), dtype=torch.float32)


def default_output_path(input_path: Path, checkpoint_path: Path, candidate_mode: str) -> Path:
    return Path("episodes/dqn/actions") / f"actions_multi_dqn_{candidate_mode}_{checkpoint_path.stem}_{input_path.stem}.csv"


def write_actions(path: Path, rows: List[Dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=OUTPUT_COLUMNS)
        writer.writeheader()
        writer.writerows(rows)


def select_candidates(cycle_df: pd.DataFrame, mode: str, threshold: float, top_k: int) -> pd.DataFrame:
    if mode == "all_users":
        candidates = cycle_df.copy()
    elif mode == "flag_only":
        candidates = cycle_df[pd.to_numeric(cycle_df["target_ue_flag"]) == 1].copy()
    elif mode == "unsatisfied":
        candidates = cycle_df[pd.to_numeric(cycle_df["satisfaction"]) < threshold].copy()
    elif mode == "top_k_low":
        candidates = cycle_df.copy()
    elif mode == "switched_only":
        candidates = cycle_df[pd.to_numeric(cycle_df["switch_flag"]) == 1].copy()
    else:  # pragma: no cover
        raise ValueError(f"unsupported candidate mode: {mode}")

    candidates = candidates.sort_values(["satisfaction", "ue_id"], ascending=[True, True])
    if mode == "top_k_low":
        candidates = candidates.head(top_k)
    return candidates.copy()


def recompute_num_users_on_current_bs(working_df: pd.DataFrame) -> None:
    current_bs = pd.to_numeric(working_df["current_bs_id"], errors="raise").astype(int)
    counts = current_bs.value_counts().to_dict()
    working_df.loc[:, "num_users_on_current_bs"] = current_bs.map(counts).astype(int)


def update_working_state(working_df: pd.DataFrame, ue_id: int, selected_bs_id: int) -> None:
    mask = pd.to_numeric(working_df["ue_id"], errors="raise").astype(int) == ue_id
    if not mask.any():
        return
    working_df.loc[mask, "current_bs_id"] = selected_bs_id
    recompute_num_users_on_current_bs(working_df)


def infer_cycle_actions(
    cycle_df: pd.DataFrame,
    candidates: pd.DataFrame,
    model: QNetwork,
    feature_columns: Sequence[str],
    state_mean: torch.Tensor,
    state_std: torch.Tensor,
    device: torch.device,
    action_dim: int,
    max_switches: int,
    min_advantage: float,
    exclude_current_bs_action: bool,
    sequential_inference: bool,
) -> List[Dict[str, str]]:
    if candidates.empty or max_switches <= 0:
        return []

    working_df = cycle_df.copy()
    recompute_num_users_on_current_bs(working_df)
    candidate_ue_ids = [int(v) for v in pd.to_numeric(candidates["ue_id"], errors="raise").tolist()]
    selected_ue_ids: Set[int] = set()
    selected_actions: List[Dict[str, str]] = []
    seed = int(float(cycle_df.iloc[0]["seed"]))
    cycle_id = int(float(cycle_df.iloc[0]["cycle_id"]))

    for step_id in range(max_switches):
        active_ue_ids = [ue_id for ue_id in candidate_ue_ids if ue_id not in selected_ue_ids]
        if not active_ue_ids:
            break

        ue_ids_numeric = pd.to_numeric(working_df["ue_id"], errors="raise").astype(int)
        active_df = working_df[ue_ids_numeric.isin(active_ue_ids)].copy()
        active_df = active_df.set_index(pd.to_numeric(active_df["ue_id"], errors="raise").astype(int)).loc[active_ue_ids].reset_index(drop=True)
        if active_df.empty:
            break

        states = numeric_matrix(active_df, feature_columns).to(device)
        states = (states - state_mean) / state_std
        with torch.no_grad():
            q_values = model(states).cpu()

        best_idx = -1
        best_advantage = float("-inf")
        best_action = -1
        for idx, (_, row) in enumerate(active_df.iterrows()):
            q = q_values[idx]
            current_bs = int(float(row["current_bs_id"]))
            if current_bs < 0 or current_bs >= action_dim:
                continue
            selected_bs = int(torch.argmax(q).item())
            if exclude_current_bs_action and selected_bs == current_bs:
                continue
            advantage = float(q[selected_bs].item() - q[current_bs].item())
            if advantage < min_advantage:
                continue
            if advantage > best_advantage:
                best_idx = idx
                best_advantage = advantage
                best_action = selected_bs

        if best_idx < 0:
            break

        row = active_df.iloc[best_idx]
        q_list = q_values[best_idx].tolist()
        current_bs = int(float(row["current_bs_id"]))
        ue_id = int(float(row["ue_id"]))
        selected_actions.append({
            "seed": str(seed),
            "cycle_id": str(cycle_id),
            "step_id": str(step_id),
            "target_ue_id": str(ue_id),
            "current_bs_id": str(current_bs),
            "selected_bs_id": str(best_action),
            "advantage": f"{best_advantage:.6f}",
            "q_bs0": f"{q_list[0]:.6f}" if len(q_list) > 0 else "",
            "q_bs1": f"{q_list[1]:.6f}" if len(q_list) > 1 else "",
            "q_bs2": f"{q_list[2]:.6f}" if len(q_list) > 2 else "",
        })
        selected_ue_ids.add(ue_id)

        if sequential_inference:
            update_working_state(working_df, ue_id, best_action)

    return selected_actions


def main() -> int:
    args = parse_args()
    input_path = Path(args.input)
    checkpoint_path = Path(args.checkpoint)
    device = get_device(args.device)

    if args.max_switches < 0:
        raise ValueError("--max-switches must be non-negative.")

    checkpoint = load_checkpoint(checkpoint_path, device)
    feature_columns = checkpoint.get("feature_columns", DEFAULT_FEATURE_COLUMNS)
    hidden_dims = checkpoint.get("hidden_dims", [128, 128])
    action_dim = int(checkpoint.get("action_dim", 3))
    state_mean = torch.tensor(checkpoint["state_mean"], dtype=torch.float32, device=device)
    state_std = torch.tensor(checkpoint["state_std"], dtype=torch.float32, device=device)

    df = load_master_log(input_path)
    missing_features = sorted(set(feature_columns) - set(df.columns))
    if missing_features:
        raise ValueError(f"{input_path}: missing model feature columns: {missing_features}")
    df = df.sort_values(["cycle_id", "ue_id"]).reset_index(drop=True)

    model = QNetwork(len(feature_columns), action_dim, hidden_dims).to(device)
    model.load_state_dict(checkpoint["model_state_dict"])
    model.eval()

    rows: List[Dict[str, str]] = []
    for _, cycle_df in df.groupby("cycle_id", sort=True):
        candidates = select_candidates(
            cycle_df,
            mode=args.candidate_mode,
            threshold=args.satisfaction_threshold,
            top_k=args.max_switches,
        )
        rows.extend(infer_cycle_actions(
            cycle_df=cycle_df,
            candidates=candidates,
            model=model,
            feature_columns=feature_columns,
            state_mean=state_mean,
            state_std=state_std,
            device=device,
            action_dim=action_dim,
            max_switches=args.max_switches,
            min_advantage=args.min_advantage,
            exclude_current_bs_action=args.exclude_current_bs_action,
            sequential_inference=args.sequential_inference,
        ))

    output_path = Path(args.output) if args.output else default_output_path(input_path, checkpoint_path, args.candidate_mode)
    write_actions(output_path, rows)
    print(f"wrote {len(rows)} actions: {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
