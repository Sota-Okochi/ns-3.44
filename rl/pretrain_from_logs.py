from __future__ import annotations

import argparse
from dataclasses import asdict
from pathlib import Path
import random

import pandas as pd
import torch
from torch import nn
from torch.utils.data import DataLoader, TensorDataset, random_split

from agent import AgentConfig, OnlineDQNAgent
from protocol import STATE_FEATURES

REQUIRED_COLUMNS = set(STATE_FEATURES + ["action_selected_bs_id", "switch_flag"])


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
    # preserve order but remove duplicates
    seen = set()
    unique = []
    for f in files:
        key = str(f.resolve())
        if key not in seen:
            seen.add(key)
            unique.append(f)
    return unique


def load_bc_dataframe(files: list[Path], keep_no_switch_ratio: float, seed: int) -> pd.DataFrame:
    rng = random.Random(seed)
    frames = []
    for f in files:
        df = pd.read_csv(f)
        missing = REQUIRED_COLUMNS - set(df.columns)
        if missing:
            raise ValueError(f"{f} missing columns: {sorted(missing)}")
        df = df.copy()
        df["source_file"] = str(f)
        frames.append(df)
    data = pd.concat(frames, ignore_index=True)

    # Clean invalid labels and NaNs in features.
    data = data.dropna(subset=STATE_FEATURES + ["action_selected_bs_id", "switch_flag"])
    data["action_selected_bs_id"] = data["action_selected_bs_id"].astype(int)
    data = data[(data["action_selected_bs_id"] >= 0) & (data["action_selected_bs_id"] <= 2)]

    switched = data[data["switch_flag"].astype(int) == 1]
    kept = data[data["switch_flag"].astype(int) == 0]
    if keep_no_switch_ratio < 1.0 and not kept.empty:
        keep_n = int(len(kept) * keep_no_switch_ratio)
        keep_n = max(0, keep_n)
        kept = kept.sample(n=keep_n, random_state=seed) if keep_n > 0 else kept.iloc[0:0]
    sampled = pd.concat([switched, kept], ignore_index=True)
    sampled = sampled.sample(frac=1.0, random_state=seed).reset_index(drop=True)
    return sampled


def train_bc(df: pd.DataFrame, args: argparse.Namespace) -> dict:
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
    y = torch.tensor(df["action_selected_bs_id"].astype(int).values, dtype=torch.long)
    dataset = TensorDataset(x, y)
    val_size = max(1, int(len(dataset) * args.val_ratio)) if len(dataset) > 1 else 0
    train_size = len(dataset) - val_size
    gen = torch.Generator().manual_seed(args.seed)
    if val_size > 0:
        train_ds, val_ds = random_split(dataset, [train_size, val_size], generator=gen)
    else:
        train_ds, val_ds = dataset, None

    train_loader = DataLoader(train_ds, batch_size=args.batch_size, shuffle=True, generator=gen)
    val_loader = DataLoader(val_ds, batch_size=args.batch_size) if val_ds is not None else None
    criterion = nn.CrossEntropyLoss()

    history = []
    for epoch in range(1, args.epochs + 1):
        agent.q.train()
        total_loss = 0.0
        correct = 0
        total = 0
        for bx, by in train_loader:
            bx = bx.to(device)
            by = by.to(device)
            logits = agent.q(bx)
            loss = criterion(logits, by)
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
                for bx, by in val_loader:
                    bx = bx.to(device)
                    by = by.to(device)
                    logits = agent.q(bx)
                    loss = criterion(logits, by)
                    v_loss += float(loss.item()) * len(by)
                    v_correct += int((logits.argmax(dim=1) == by).sum().item())
                    v_total += len(by)
            val_loss = v_loss / max(v_total, 1)
            val_acc = v_correct / max(v_total, 1)
        history.append({"epoch": epoch, "train_loss": train_loss, "train_acc": train_acc, "val_loss": val_loss, "val_acc": val_acc})
        if epoch == 1 or epoch == args.epochs or epoch % args.log_interval == 0:
            print(f"epoch={epoch} train_loss={train_loss:.6f} train_acc={train_acc:.4f} val_loss={val_loss if val_loss is not None else 'NA'} val_acc={val_acc if val_acc is not None else 'NA'}")

    agent.sync_target()
    extra = {
        "pretrain_type": "behavior_cloning",
        "features": STATE_FEATURES,
        "num_samples": int(len(df)),
        "action_counts": {str(k): int(v) for k, v in df["action_selected_bs_id"].value_counts().sort_index().items()},
        "switch_counts": {str(k): int(v) for k, v in df["switch_flag"].astype(int).value_counts().sort_index().items()},
        "history": history,
    }
    agent.save_checkpoint(args.output, extra=extra)
    return extra


def main() -> None:
    parser = argparse.ArgumentParser(description="Behavior cloning pretrain from ns-3 master_log CSV files.")
    parser.add_argument("--inputs", nargs="+", required=True, help="master_log CSV files or directories containing master_log*.csv")
    parser.add_argument("--output", default="models/bc_multi_greedy_rulebase_state9.pt")
    parser.add_argument("--keep-no-switch-ratio", type=float, default=0.25, help="Sampling ratio for switch_flag=0 rows")
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
    df = load_bc_dataframe(files, args.keep_no_switch_ratio, args.seed)
    print(f"samples={len(df)} features={STATE_FEATURES}")
    print("action_counts", df["action_selected_bs_id"].value_counts().sort_index().to_dict())
    print("switch_counts", df["switch_flag"].astype(int).value_counts().sort_index().to_dict())
    extra = train_bc(df, args)
    print(f"saved={args.output}")
    print("extra", {k: v for k, v in extra.items() if k != "history"})


if __name__ == "__main__":
    main()
