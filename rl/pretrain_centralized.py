from __future__ import annotations

import argparse
import json
import random
from pathlib import Path

import numpy as np
import pandas as pd
import torch
from torch import nn
from torch.utils.data import DataLoader, TensorDataset

from centralized_agent import make_agent
from centralized_protocol import ACTION_DIM, STATE_DIM
from centralized_protocol_v2 import STATE_DIM_V2, feature_schema as feature_schema_v2


def state_dim_for_schema(schema_version: str) -> int:
    if schema_version in {"v2", "v2_onehot", "centralized_state_v2", "centralized_state_v2_onehot"}:
        return STATE_DIM_V2
    return STATE_DIM


def normalized_schema_version(schema_version: str) -> str:
    if schema_version in {"v2", "v2_onehot", "centralized_state_v2"}:
        return "centralized_state_v2_onehot"
    return "centralized_state_v1"


def feature_columns(state_dim: int) -> list[str]:
    return [f"f{i}" for i in range(state_dim)]


def load_dataset(path: str | Path, state_dim: int) -> pd.DataFrame:
    df = pd.read_csv(path)
    cols = feature_columns(state_dim)
    required = cols + ["expert_action_id", "sample_weight", "target_ue_id", "selected_bs_id"]
    missing = set(required) - set(df.columns)
    if missing:
        raise ValueError(f"missing columns: {sorted(missing)[:20]}{'...' if len(missing) > 20 else ''}")
    df = df.dropna(subset=required).copy()
    df["expert_action_id"] = df["expert_action_id"].astype(int)
    df = df[(df["expert_action_id"] >= 0) & (df["expert_action_id"] < ACTION_DIM)]
    if df.empty:
        raise ValueError("dataset has no valid samples")
    return df


def split_dataframe(df: pd.DataFrame, args: argparse.Namespace) -> tuple[pd.DataFrame, pd.DataFrame | None, dict]:
    if args.val_ratio <= 0.0 or len(df) <= 1:
        return df.reset_index(drop=True), None, {"split_type": "none"}

    rng = random.Random(args.seed)
    if args.seed_split and "seed" in df.columns:
        seeds = sorted(int(s) for s in df["seed"].dropna().unique())
        rng.shuffle(seeds)
        val_n = max(1, int(round(len(seeds) * args.val_ratio))) if len(seeds) > 1 else 0
        val_seeds = set(seeds[:val_n])
        train = df[~df["seed"].astype(int).isin(val_seeds)].copy()
        val = df[df["seed"].astype(int).isin(val_seeds)].copy()
        # 極端に小さいデータで train/val が空になる事故を避ける。
        if train.empty or val.empty:
            return df.reset_index(drop=True), None, {"split_type": "none_fallback"}
        return (
            train.sample(frac=1.0, random_state=args.seed).reset_index(drop=True),
            val.sample(frac=1.0, random_state=args.seed).reset_index(drop=True),
            {
                "split_type": "seed",
                "train_seeds": sorted(int(s) for s in train["seed"].unique()),
                "val_seeds": sorted(int(s) for s in val["seed"].unique()),
            },
        )

    shuffled = df.sample(frac=1.0, random_state=args.seed).reset_index(drop=True)
    val_size = max(1, int(len(shuffled) * args.val_ratio))
    val = shuffled.iloc[:val_size].copy()
    train = shuffled.iloc[val_size:].copy()
    return train.reset_index(drop=True), val.reset_index(drop=True), {"split_type": "sample"}


def make_tensors(df: pd.DataFrame, state_dim: int, mean: np.ndarray | None, std: np.ndarray | None) -> TensorDataset:
    cols = feature_columns(state_dim)
    x_np = df[cols].astype(float).values.astype("float32")
    if mean is not None and std is not None:
        x_np = (x_np - mean.astype("float32")) / std.astype("float32")
    x = torch.tensor(x_np, dtype=torch.float32)
    y = torch.tensor(df["expert_action_id"].astype(int).values, dtype=torch.long)
    w = torch.tensor(df["sample_weight"].astype(float).values, dtype=torch.float32)
    return TensorDataset(x, y, w)


def topk_counts(logits: torch.Tensor, labels: torch.Tensor, ks: tuple[int, ...]) -> dict[int, int]:
    max_k = min(max(ks), logits.shape[1])
    top = logits.topk(max_k, dim=1).indices
    out: dict[int, int] = {}
    for k in ks:
        kk = min(k, logits.shape[1])
        out[k] = int((top[:, :kk] == labels.unsqueeze(1)).any(dim=1).sum().item())
    return out


def action_decomposed_counts(logits: torch.Tensor, labels: torch.Tensor) -> dict[str, int]:
    pred = torch.argmax(logits, dim=1)
    pred_ue = pred // 3
    pred_ap = pred % 3
    true_ue = labels // 3
    true_ap = labels % 3
    target_correct = pred_ue == true_ue
    out = {
        "target_ue_correct": int(target_correct.sum().item()),
        "selected_ap_correct": int((pred_ap == true_ap).sum().item()),
        "selected_ap_given_target_correct": int(((pred_ap == true_ap) & target_correct).sum().item()),
        "target_correct_total": int(target_correct.sum().item()),
    }

    q = logits.reshape(logits.shape[0], -1, 3)
    row = torch.arange(logits.shape[0], device=logits.device)
    ap_for_true_ue = torch.argmax(q[row, true_ue], dim=1)
    out["ap_for_true_ue_correct"] = int((ap_for_true_ue == true_ap).sum().item())
    return out


def add_decomposed_metrics(metrics: dict, counts: dict[str, int], total: int) -> None:
    metrics["target_ue_acc"] = counts["target_ue_correct"] / max(total, 1)
    metrics["selected_ap_acc"] = counts["selected_ap_correct"] / max(total, 1)
    metrics["selected_ap_acc_given_target_ue_correct"] = (
        counts["selected_ap_given_target_correct"] / max(counts["target_correct_total"], 1)
    )
    metrics["ap_acc_for_true_ue"] = counts["ap_for_true_ue_correct"] / max(total, 1)


def evaluate(agent, loader: DataLoader | None, criterion, device: torch.device, topk: tuple[int, ...]) -> dict | None:
    if loader is None:
        return None
    agent.q.eval()
    loss_sum = 0.0
    weight_sum = 0.0
    total = 0
    correct = {k: 0 for k in topk}
    decomposed = {
        "target_ue_correct": 0,
        "selected_ap_correct": 0,
        "selected_ap_given_target_correct": 0,
        "target_correct_total": 0,
        "ap_for_true_ue_correct": 0,
    }
    with torch.no_grad():
        for bx, by, bw in loader:
            bx = bx.to(device)
            by = by.to(device)
            bw = bw.to(device)
            logits = agent.q(bx)
            per_sample_loss = criterion(logits, by)
            loss_sum += float((per_sample_loss * bw).sum().item())
            weight_sum += float(bw.sum().item())
            counts = topk_counts(logits, by, topk)
            for k, v in counts.items():
                correct[k] += v
            dc = action_decomposed_counts(logits, by)
            for k, v in dc.items():
                decomposed[k] += v
            total += len(by)
    metrics = {"loss": loss_sum / max(weight_sum, 1e-12)}
    for k in topk:
        metrics[f"top{k}"] = correct[k] / max(total, 1)
    add_decomposed_metrics(metrics, decomposed, total)
    return metrics


def train_weighted_bc(df: pd.DataFrame, args: argparse.Namespace) -> dict:
    train_df, val_df, split_meta = split_dataframe(df, args)
    state_dim = state_dim_for_schema(args.schema_version)
    schema_version = normalized_schema_version(args.schema_version)
    cols = feature_columns(state_dim)

    mean = None
    std = None
    if not args.no_normalize:
        mean = train_df[cols].astype(float).values.mean(axis=0).astype("float32")
        std = train_df[cols].astype(float).values.std(axis=0).astype("float32")
        std = np.where(std < 1e-6, 1.0, std).astype("float32")

    agent = make_agent(
        state_dim=state_dim,
        action_dim=ACTION_DIM,
        seed=args.seed,
        hidden_dim=args.hidden_dim,
        lr=args.lr,
        gamma=args.gamma,
        device=args.device,
        model_type=args.model_type,
        emb_dim=args.emb_dim,
    )
    device = torch.device(args.device)
    gen = torch.Generator().manual_seed(args.seed)
    train_loader = DataLoader(
        make_tensors(train_df, state_dim, mean, std),
        batch_size=args.batch_size,
        shuffle=True,
        generator=gen,
    )
    val_loader = (
        DataLoader(make_tensors(val_df, state_dim, mean, std), batch_size=args.batch_size)
        if val_df is not None
        else None
    )
    criterion = nn.CrossEntropyLoss(reduction="none")
    topk = tuple(args.topk)
    history: list[dict] = []

    for epoch in range(1, args.epochs + 1):
        agent.q.train()
        loss_sum = 0.0
        weight_sum = 0.0
        total = 0
        correct = {k: 0 for k in topk}
        decomposed = {
            "target_ue_correct": 0,
            "selected_ap_correct": 0,
            "selected_ap_given_target_correct": 0,
            "target_correct_total": 0,
            "ap_for_true_ue_correct": 0,
        }
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
            loss_sum += float((per_sample_loss.detach() * bw).sum().item())
            weight_sum += float(bw.sum().item())
            counts = topk_counts(logits.detach(), by, topk)
            for k, v in counts.items():
                correct[k] += v
            dc = action_decomposed_counts(logits.detach(), by)
            for k, v in dc.items():
                decomposed[k] += v
            total += len(by)

        train_metrics = {"loss": loss_sum / max(weight_sum, 1e-12)}
        for k in topk:
            train_metrics[f"top{k}"] = correct[k] / max(total, 1)
        add_decomposed_metrics(train_metrics, decomposed, total)
        val_metrics = evaluate(agent, val_loader, criterion, device, topk)

        row = {"epoch": epoch}
        row.update({f"train_{k}": v for k, v in train_metrics.items()})
        if val_metrics is not None:
            row.update({f"val_{k}": v for k, v in val_metrics.items()})
        history.append(row)

        if epoch == 1 or epoch == args.epochs or epoch % args.log_interval == 0:
            msg = (
                f"epoch={epoch} train_loss={train_metrics['loss']:.6f} "
                + " ".join(f"train_top{k}={train_metrics[f'top{k}']:.4f}" for k in topk)
                + f" train_target_ue_acc={train_metrics['target_ue_acc']:.4f}"
                + f" train_selected_ap_acc={train_metrics['selected_ap_acc']:.4f}"
            )
            if val_metrics is not None:
                msg += (
                    f" val_loss={val_metrics['loss']:.6f} "
                    + " ".join(f"val_top{k}={val_metrics[f'top{k}']:.4f}" for k in topk)
                    + f" val_target_ue_acc={val_metrics['target_ue_acc']:.4f}"
                    + f" val_selected_ap_acc={val_metrics['selected_ap_acc']:.4f}"
                )
            print(msg, flush=True)

    agent.sync_target()
    label_counts = {str(k): int(v) for k, v in df["expert_action_id"].value_counts().sort_index().items()}
    bs_counts = (
        {str(k): int(v) for k, v in df["selected_bs_id"].value_counts().sort_index().items()}
        if "selected_bs_id" in df.columns
        else {}
    )
    normalization = {
        "enabled": not args.no_normalize,
        "feature_mean": mean.tolist() if mean is not None else None,
        "feature_std": std.tolist() if std is not None else None,
    }
    extra = {
        "pretrain_type": "centralized_dqn_logistic_behavior_cloning",
        "model_type": args.model_type,
        "schema_version": schema_version,
        "state_dim": state_dim,
        "action_dim": ACTION_DIM,
        "num_samples": int(len(df)),
        "num_train_samples": int(len(train_df)),
        "num_val_samples": int(len(val_df)) if val_df is not None else 0,
        "hidden_dim": args.hidden_dim,
        "emb_dim": args.emb_dim,
        "lr": args.lr,
        "gamma": args.gamma,
        "topk": list(topk),
        "split": split_meta,
        "normalization": normalization,
        "feature_schema": feature_schema_v2() if schema_version == "centralized_state_v2_onehot" else {
            "schema_version": "centralized_state_v1",
            "state_dim": STATE_DIM,
            "action_dim": ACTION_DIM,
        },
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
    parser.add_argument("--schema-version", choices=["v1", "v2_onehot", "centralized_state_v1", "centralized_state_v2_onehot"], default="v1")
    parser.add_argument("--model-type", choices=["mlp_v1", "mlp_v2_onehot", "factorized_v2"], default="mlp_v1")
    parser.add_argument("--epochs", type=int, default=50)
    parser.add_argument("--batch-size", type=int, default=256)
    parser.add_argument("--hidden-dim", type=int, default=512)
    parser.add_argument("--emb-dim", type=int, default=64, help="Embedding dimension for --model-type=factorized_v2.")
    parser.add_argument("--lr", type=float, default=1e-3)
    parser.add_argument("--gamma", type=float, default=0.99)
    parser.add_argument("--val-ratio", type=float, default=0.1)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--device", default="cpu")
    parser.add_argument("--log-interval", type=int, default=10)
    parser.add_argument("--grad-clip", type=float, default=5.0)
    parser.add_argument("--history-output", default=None)
    parser.add_argument("--topk", type=int, nargs="+", default=[1, 5, 10, 20])
    parser.add_argument("--seed-split", action=argparse.BooleanOptionalAction, default=True,
                        help="validationをseed単位で分離する。default: true")
    parser.add_argument("--no-normalize", action="store_true", help="feature normalizationを無効化")
    args = parser.parse_args()

    state_dim = state_dim_for_schema(args.schema_version)
    if args.model_type in {"mlp_v2_onehot", "factorized_v2"} and state_dim != STATE_DIM_V2:
        raise ValueError(f"{args.model_type} requires --schema-version=v2_onehot")
    if args.model_type == "mlp_v1" and state_dim != STATE_DIM:
        raise ValueError("mlp_v1 requires --schema-version=v1; use --model-type=mlp_v2_onehot for v2 MLP.")

    df = load_dataset(args.input, state_dim)
    print(f"samples={len(df)} state_dim={state_dim} action_dim={ACTION_DIM} model_type={args.model_type} schema={normalized_schema_version(args.schema_version)}")
    print("unique_actions", int(df["expert_action_id"].nunique()))
    if "seed" in df.columns:
        print("unique_seeds", int(df["seed"].nunique()))
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
    print("extra", {k: v for k, v in extra.items() if k not in {"history", "label_counts", "normalization"}})
    if extra.get("split"):
        print("split", extra["split"])
    print("normalization", {"enabled": extra["normalization"]["enabled"]})

    if args.history_output:
        out = Path(args.history_output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(extra["history"], ensure_ascii=False, indent=2) + "\n")
        print(f"history -> {out}")


if __name__ == "__main__":
    main()
