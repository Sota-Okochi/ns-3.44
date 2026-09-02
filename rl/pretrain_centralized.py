from __future__ import annotations

import argparse
import json
from pathlib import Path

import pandas as pd
import torch
from torch import nn
from torch.utils.data import DataLoader, TensorDataset, random_split

from centralized_agent import make_agent
from centralized_protocol import ACTION_DIM, STATE_DIM


def load_dataset(path: str | Path) -> pd.DataFrame:
    df = pd.read_csv(path)
    feature_cols = [f"f{i}" for i in range(STATE_DIM)]
    missing = set(feature_cols + ["expert_action_id", "sample_weight"]) - set(df.columns)
    if missing:
        raise ValueError(f"missing columns: {sorted(missing)[:20]}{'...' if len(missing) > 20 else ''}")
    df = df.dropna(subset=feature_cols + ["expert_action_id", "sample_weight"]).copy()
    df["expert_action_id"] = df["expert_action_id"].astype(int)
    df = df[(df["expert_action_id"] >= 0) & (df["expert_action_id"] < ACTION_DIM)]
    if df.empty:
        raise ValueError("dataset has no valid samples")
    return df


def train_weighted_bc(df: pd.DataFrame, args: argparse.Namespace) -> dict:
    feature_cols = [f"f{i}" for i in range(STATE_DIM)]
    agent = make_agent(
        state_dim=STATE_DIM,
        action_dim=ACTION_DIM,
        seed=args.seed,
        hidden_dim=args.hidden_dim,
        lr=args.lr,
        gamma=args.gamma,
        device=args.device,
    )
    device = torch.device(args.device)

    x = torch.tensor(df[feature_cols].astype(float).values, dtype=torch.float32)
    y = torch.tensor(df["expert_action_id"].astype(int).values, dtype=torch.long)
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
    history: list[dict] = []

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
            torch.nn.utils.clip_grad_norm_(agent.q.parameters(), args.grad_clip)
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
                f"val_acc={val_acc if val_acc is not None else 'NA'}",
                flush=True,
            )

    agent.sync_target()
    label_counts = {str(k): int(v) for k, v in df["expert_action_id"].value_counts().sort_index().items()}
    bs_counts = (
        {str(k): int(v) for k, v in df["selected_bs_id"].value_counts().sort_index().items()}
        if "selected_bs_id" in df.columns
        else {}
    )
    extra = {
        "pretrain_type": "centralized_dqn_logistic_behavior_cloning",
        "state_dim": STATE_DIM,
        "action_dim": ACTION_DIM,
        "num_samples": int(len(df)),
        "hidden_dim": args.hidden_dim,
        "lr": args.lr,
        "gamma": args.gamma,
        "label_counts": label_counts,
        "selected_bs_counts": bs_counts,
        "history": history,
    }
    agent.save_checkpoint(args.output, extra=extra)
    return extra


def main() -> None:
    parser = argparse.ArgumentParser(description="Pretrain centralized DQN by weighted BC from exported centralized dataset.")
    parser.add_argument("--input", required=True, help="CSV generated by rl/export_centralized_bc_dataset.py")
    parser.add_argument("--output", default="models/centralized_dqn_bc.pt")
    parser.add_argument("--epochs", type=int, default=50)
    parser.add_argument("--batch-size", type=int, default=256)
    parser.add_argument("--hidden-dim", type=int, default=512)
    parser.add_argument("--lr", type=float, default=1e-3)
    parser.add_argument("--gamma", type=float, default=0.99)
    parser.add_argument("--val-ratio", type=float, default=0.1)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--device", default="cpu")
    parser.add_argument("--log-interval", type=int, default=10)
    parser.add_argument("--grad-clip", type=float, default=5.0)
    parser.add_argument("--history-output", default=None)
    args = parser.parse_args()

    df = load_dataset(args.input)
    print(f"samples={len(df)} state_dim={STATE_DIM} action_dim={ACTION_DIM}")
    print("unique_actions", int(df["expert_action_id"].nunique()))
    if "selected_bs_id" in df.columns:
        print("selected_bs_counts", df["selected_bs_id"].value_counts().sort_index().to_dict())
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
    print("extra", {k: v for k, v in extra.items() if k not in {"history", "label_counts"}})

    if args.history_output:
        out = Path(args.history_output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(extra["history"], ensure_ascii=False, indent=2) + "\n")
        print(f"history -> {out}")


if __name__ == "__main__":
    main()
