from __future__ import annotations

import argparse
from pathlib import Path
import random

import pandas as pd
import torch
from torch import nn
from torch.utils.data import DataLoader, TensorDataset, random_split

from agent import AgentConfig, OnlineDQNAgent
from protocol import STATE_FEATURES


REQUIRED_COLUMNS = set(
    STATE_FEATURES
    + [
        "seed",
        "cycle_id",
        "ue_id",
        "current_bs_id",
        "action_selected_bs_id",
        "best_estimated_h_delta",
        "harmonic_mean",
    ]
)


def collect_csv_files(inputs: list[str]) -> list[Path]:
    files: list[Path] = []
    for raw in inputs:
        p = Path(raw)
        if p.is_dir():
            files.extend(sorted(p.glob("master_log*.csv")))
        elif p.is_file():
            files.append(p)
        else:
            raise FileNotFoundError(raw)

    seen = set()
    unique: list[Path] = []
    for f in files:
        key = str(f.resolve())
        if key not in seen:
            seen.add(key)
            unique.append(f)
    return unique


def build_best_assignment_dataframe(
    files: list[Path],
    early_cycles: set[int],
    keep_no_switch_ratio: float,
    weight_lambda: float,
    seed: int,
) -> pd.DataFrame:
    rng = random.Random(seed)
    frames: list[pd.DataFrame] = []

    for f in files:
        df = pd.read_csv(f)
        missing = REQUIRED_COLUMNS - set(df.columns)
        if missing:
            raise ValueError(f"{f} missing columns: {sorted(missing)}")
        if df.empty:
            continue

        df = df.copy()
        df["source_file"] = str(f)

        # best cycle は実測 harmonic_mean が最大の cycle とする。
        cycle_h = df.groupby("cycle_id")["harmonic_mean"].first()
        best_cycle = int(cycle_h.idxmax())
        best = df[df["cycle_id"] == best_cycle][["ue_id", "current_bs_id"]].copy()
        best = best.rename(columns={"current_bs_id": "expert_bs_id"})

        early = df[df["cycle_id"].astype(int).isin(early_cycles)].copy()
        if early.empty:
            continue
        early = early.merge(best, on="ue_id", how="inner")
        early["expert_bs_id"] = early["expert_bs_id"].astype(int)
        early = early[(early["expert_bs_id"] >= 0) & (early["expert_bs_id"] <= 2)]
        early["best_cycle"] = best_cycle
        early["is_noop_to_best"] = (
            early["current_bs_id"].astype(int) == early["expert_bs_id"].astype(int)
        ).astype(int)
        early["sample_weight"] = 1.0 + weight_lambda * early[
            "best_estimated_h_delta"
        ].clip(lower=0.0).astype(float)
        frames.append(early)

    if not frames:
        raise ValueError("no usable rows found")

    data = pd.concat(frames, ignore_index=True)
    data = data.dropna(subset=STATE_FEATURES + ["expert_bs_id", "sample_weight"])

    switch_like = data[data["is_noop_to_best"] == 0]
    noop = data[data["is_noop_to_best"] == 1]
    if keep_no_switch_ratio < 1.0 and not noop.empty:
        keep_n = int(len(noop) * keep_no_switch_ratio)
        noop = noop.sample(n=keep_n, random_state=seed) if keep_n > 0 else noop.iloc[0:0]

    sampled = pd.concat([switch_like, noop], ignore_index=True)
    sampled = sampled.sample(frac=1.0, random_state=seed).reset_index(drop=True)
    return sampled


def train_weighted_bc(df: pd.DataFrame, args: argparse.Namespace) -> dict:
    cfg = AgentConfig(
        state_dim=len(STATE_FEATURES),
        action_dim=3,
        hidden_dim=args.hidden_dim,
        lr=args.lr,
        seed=args.seed,
        device=args.device,
    )
    agent = OnlineDQNAgent(cfg)
    device = torch.device(args.device)

    x = torch.tensor(df[STATE_FEATURES].astype(float).values, dtype=torch.float32)
    y = torch.tensor(df["expert_bs_id"].astype(int).values, dtype=torch.long)
    w = torch.tensor(df["sample_weight"].astype(float).values, dtype=torch.float32)
    dataset = TensorDataset(x, y, w)

    val_size = max(1, int(len(dataset) * args.val_ratio)) if len(dataset) > 1 else 0
    train_size = len(dataset) - val_size
    gen = torch.Generator().manual_seed(args.seed)
    if val_size > 0:
        train_ds, val_ds = random_split(dataset, [train_size, val_size], generator=gen)
    else:
        train_ds, val_ds = dataset, None

    train_loader = DataLoader(train_ds, batch_size=args.batch_size, shuffle=True, generator=gen)
    val_loader = DataLoader(val_ds, batch_size=args.batch_size) if val_ds is not None else None
    criterion = nn.CrossEntropyLoss(reduction="none")

    history = []
    for epoch in range(1, args.epochs + 1):
        agent.q.train()
        total_loss = 0.0
        correct = 0
        total = 0
        for bx, by, bw in train_loader:
            bx = bx.to(device)
            by = by.to(device)
            bw = bw.to(device)
            logits = agent.q(bx)
            per_sample_loss = criterion(logits, by)
            loss = (per_sample_loss * bw).sum() / bw.sum().clamp_min(1e-12)
            agent.optim.zero_grad()
            loss.backward()
            agent.optim.step()
            total_loss += float(loss.item()) * len(by)
            correct += int((logits.argmax(dim=1) == by).sum().item())
            total += len(by)

        train_loss = total_loss / max(total, 1)
        train_acc = correct / max(total, 1)
        val_loss = None
        val_acc = None
        if val_loader is not None:
            agent.q.eval()
            v_loss = 0.0
            v_correct = 0
            v_total = 0
            with torch.no_grad():
                for bx, by, bw in val_loader:
                    bx = bx.to(device)
                    by = by.to(device)
                    bw = bw.to(device)
                    logits = agent.q(bx)
                    per_sample_loss = criterion(logits, by)
                    loss = (per_sample_loss * bw).sum() / bw.sum().clamp_min(1e-12)
                    v_loss += float(loss.item()) * len(by)
                    v_correct += int((logits.argmax(dim=1) == by).sum().item())
                    v_total += len(by)
            val_loss = v_loss / max(v_total, 1)
            val_acc = v_correct / max(v_total, 1)

        row = {
            "epoch": epoch,
            "train_loss": train_loss,
            "train_acc": train_acc,
            "val_loss": val_loss,
            "val_acc": val_acc,
        }
        history.append(row)
        if epoch == 1 or epoch == args.epochs or epoch % args.log_interval == 0:
            print(
                f"epoch={epoch} train_loss={train_loss:.6f} train_acc={train_acc:.4f} "
                f"val_loss={val_loss if val_loss is not None else 'NA'} "
                f"val_acc={val_acc if val_acc is not None else 'NA'}"
            )

    agent.sync_target()
    extra = {
        "pretrain_type": "best_assignment_weighted_behavior_cloning",
        "features": STATE_FEATURES,
        "num_samples": int(len(df)),
        "early_cycles": args.early_cycles,
        "keep_no_switch_ratio": args.keep_no_switch_ratio,
        "weight_lambda": args.weight_lambda,
        "label_counts": {str(k): int(v) for k, v in df["expert_bs_id"].value_counts().sort_index().items()},
        "noop_counts": {str(k): int(v) for k, v in df["is_noop_to_best"].value_counts().sort_index().items()},
        "history": history,
    }
    agent.save_checkpoint(args.output, extra=extra)
    return extra


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Weighted BC pretraining from best-cycle assignments in master_log CSV files."
    )
    parser.add_argument("--inputs", nargs="+", required=True)
    parser.add_argument("--output", default="models/online_dqn_bc_best_assignment.pt")
    parser.add_argument("--early-cycles", type=int, nargs="+", default=[1, 2])
    parser.add_argument("--keep-no-switch-ratio", type=float, default=0.25)
    parser.add_argument("--weight-lambda", type=float, default=20.0)
    parser.add_argument("--epochs", type=int, default=50)
    parser.add_argument("--batch-size", type=int, default=256)
    parser.add_argument("--hidden-dim", type=int, default=128)
    parser.add_argument("--lr", type=float, default=1e-3)
    parser.add_argument("--val-ratio", type=float, default=0.1)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--device", default="cpu")
    parser.add_argument("--log-interval", type=int, default=10)
    args = parser.parse_args()

    files = collect_csv_files(args.inputs)
    print(f"loaded_files={len(files)}")
    for f in files:
        print(f"  {f}")
    df = build_best_assignment_dataframe(
        files,
        set(args.early_cycles),
        args.keep_no_switch_ratio,
        args.weight_lambda,
        args.seed,
    )
    print(f"samples={len(df)} features={STATE_FEATURES}")
    print("label_counts", df["expert_bs_id"].value_counts().sort_index().to_dict())
    print("noop_counts", df["is_noop_to_best"].value_counts().sort_index().to_dict())
    print(
        "sample_weight",
        {
            "min": float(df["sample_weight"].min()),
            "mean": float(df["sample_weight"].mean()),
            "max": float(df["sample_weight"].max()),
        },
    )
    extra = train_weighted_bc(df, args)
    print(f"saved={args.output}")
    print("extra", {k: v for k, v in extra.items() if k != "history"})


if __name__ == "__main__":
    main()
