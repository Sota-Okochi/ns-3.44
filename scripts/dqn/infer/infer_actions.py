#!/usr/bin/env python3
"""Run DQN inference and write action CSV for ns-3 evaluation.

Input is a master_log CSV.  The script selects rows according to target mode
(default: target_ue_flag == 1), feeds state features to a trained DQN, and
writes selected_bs_id for each target row.
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path
from typing import Dict, List, Sequence

import pandas as pd
import torch
from torch import nn


DEFAULT_FEATURE_COLUMNS = [
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
    "target_ue_id",
    "selected_bs_id",
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
    parser = argparse.ArgumentParser(description="Infer DQN selected_bs_id actions from master_log CSV.")
    parser.add_argument("--input", "-i", required=True, help="Input master_log CSV path.")
    parser.add_argument("--checkpoint", "-c", required=True, help="DQN checkpoint .pt path.")
    parser.add_argument(
        "--output",
        "-o",
        help="Output action CSV path. If omitted, a default path under episodes/dqn/actions is used.",
    )
    parser.add_argument(
        "--target-mode",
        choices=["flag_only", "all_users"],
        default="flag_only",
        help="flag_only: infer only target_ue_flag==1 rows; all_users: infer every UE row.",
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
    return df


def select_rows(df: pd.DataFrame, target_mode: str) -> pd.DataFrame:
    if target_mode == "all_users":
        return df.copy()
    selected = df[pd.to_numeric(df["target_ue_flag"]) == 1].copy()
    if selected.empty:
        raise ValueError("No rows found with target_ue_flag == 1.")
    return selected


def numeric_matrix(df: pd.DataFrame, columns: Sequence[str]) -> torch.Tensor:
    values = df.loc[:, columns].apply(pd.to_numeric, errors="raise").astype("float32")
    return torch.tensor(values.to_numpy(), dtype=torch.float32)


def default_output_path(input_path: Path, checkpoint_path: Path, target_mode: str) -> Path:
    return Path("episodes/dqn/actions") / f"actions_{target_mode}_{checkpoint_path.stem}_{input_path.stem}.csv"


def write_actions(path: Path, rows: List[Dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=OUTPUT_COLUMNS)
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    args = parse_args()
    input_path = Path(args.input)
    checkpoint_path = Path(args.checkpoint)
    device = get_device(args.device)

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
    target_df = select_rows(df, args.target_mode).sort_values(["cycle_id", "ue_id"])

    model = QNetwork(len(feature_columns), action_dim, hidden_dims).to(device)
    model.load_state_dict(checkpoint["model_state_dict"])
    model.eval()

    states = numeric_matrix(target_df, feature_columns).to(device)
    states = (states - state_mean) / state_std
    with torch.no_grad():
        q_values = model(states).cpu()
        actions = q_values.argmax(dim=1).tolist()

    rows: List[Dict[str, str]] = []
    for (_, row), action, q in zip(target_df.iterrows(), actions, q_values):
        q_list = q.tolist()
        rows.append({
            "seed": str(int(float(row["seed"]))),
            "cycle_id": str(int(float(row["cycle_id"]))),
            "target_ue_id": str(int(float(row["ue_id"]))),
            "selected_bs_id": str(int(action)),
            "q_bs0": f"{q_list[0]:.6f}" if len(q_list) > 0 else "",
            "q_bs1": f"{q_list[1]:.6f}" if len(q_list) > 1 else "",
            "q_bs2": f"{q_list[2]:.6f}" if len(q_list) > 2 else "",
        })

    output_path = Path(args.output) if args.output else default_output_path(input_path, checkpoint_path, args.target_mode)
    write_actions(output_path, rows)
    print(f"wrote {len(rows)} actions: {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
