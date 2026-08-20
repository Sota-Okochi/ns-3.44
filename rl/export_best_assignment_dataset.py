from __future__ import annotations

import argparse
import json
from pathlib import Path
import random

import pandas as pd

from protocol import STATE_FEATURES


REQUIRED_COLUMNS = set(
    STATE_FEATURES
    + [
        "seed",
        "method",
        "max_switches",
        "cycle_id",
        "ue_id",
        "current_bs_id",
        "best_estimated_h_delta",
        "harmonic_mean",
    ]
)


def collect_master_logs(inputs: list[str], recursive: bool = True) -> list[Path]:
    files: list[Path] = []
    for raw in inputs:
        p = Path(raw)
        if p.is_dir():
            pattern = "**/master_log*.csv" if recursive else "master_log*.csv"
            files.extend(sorted(p.glob(pattern)))
        elif p.is_file():
            files.append(p)
        else:
            raise FileNotFoundError(raw)

    seen: set[str] = set()
    unique: list[Path] = []
    for f in files:
        key = str(f.resolve())
        if key not in seen:
            seen.add(key)
            unique.append(f)
    return unique


def build_dataset(
    files: list[Path],
    early_cycles: set[int],
    keep_no_switch_ratio: float,
    weight_lambda: float,
    random_seed: int,
) -> tuple[pd.DataFrame, pd.DataFrame]:
    rng = random.Random(random_seed)
    frames: list[pd.DataFrame] = []
    run_summaries: list[dict] = []

    for run_index, f in enumerate(files):
        df = pd.read_csv(f)
        missing = REQUIRED_COLUMNS - set(df.columns)
        if missing:
            raise ValueError(f"{f} missing columns: {sorted(missing)}")
        if df.empty:
            continue

        df = df.copy()
        df["cycle_id"] = df["cycle_id"].astype(int)
        df["ue_id"] = df["ue_id"].astype(int)

        # 1 cycle は全 UE 行で同じ harmonic_mean を持つ想定。
        # 実測 QoE が最大の cycle を expert assignment とする。
        cycle_h = df.groupby("cycle_id")["harmonic_mean"].first()
        best_cycle = int(cycle_h.idxmax())
        best_h = float(cycle_h.loc[best_cycle])
        best = df[df["cycle_id"] == best_cycle][["ue_id", "current_bs_id"]].copy()
        best = best.rename(columns={"current_bs_id": "expert_bs_id"})

        early = df[df["cycle_id"].isin(early_cycles)].copy()
        if early.empty:
            continue

        early = early.merge(best, on="ue_id", how="inner")
        early["expert_bs_id"] = early["expert_bs_id"].astype(int)
        early = early[(early["expert_bs_id"] >= 0) & (early["expert_bs_id"] <= 2)]

        early["run_index"] = run_index
        early["source_file"] = str(f)
        early["source_condition"] = f.parent.name
        early["best_cycle"] = best_cycle
        early["best_harmonic_mean"] = best_h
        early["is_noop_to_best"] = (
            early["current_bs_id"].astype(int) == early["expert_bs_id"].astype(int)
        ).astype(int)
        early["sample_weight"] = 1.0 + weight_lambda * early[
            "best_estimated_h_delta"
        ].clip(lower=0.0).astype(float)

        before_n = len(early)
        switch_like = early[early["is_noop_to_best"] == 0]
        noop = early[early["is_noop_to_best"] == 1]
        if keep_no_switch_ratio < 1.0 and not noop.empty:
            keep_n = int(len(noop) * keep_no_switch_ratio)
            noop = (
                noop.sample(n=keep_n, random_state=random_seed + run_index)
                if keep_n > 0
                else noop.iloc[0:0]
            )
        sampled = pd.concat([switch_like, noop], ignore_index=True)
        frames.append(sampled)

        run_summaries.append(
            {
                "run_index": run_index,
                "source_file": str(f),
                "source_condition": f.parent.name,
                "seed": int(df["seed"].iloc[0]),
                "method": str(df["method"].iloc[0]),
                "max_switches": int(df["max_switches"].iloc[0]),
                "num_cycles": int(df["cycle_id"].nunique()),
                "num_ues": int(df["ue_id"].nunique()),
                "best_cycle": best_cycle,
                "best_harmonic_mean": best_h,
                "early_rows_before_sampling": int(before_n),
                "samples_after_sampling": int(len(sampled)),
                "switch_like_samples": int(len(switch_like)),
                "noop_samples_kept": int((sampled["is_noop_to_best"] == 1).sum()),
            }
        )

    if not frames:
        raise ValueError("no usable rows found")

    data = pd.concat(frames, ignore_index=True)
    data = data.dropna(subset=STATE_FEATURES + ["expert_bs_id", "sample_weight"])
    data = data.sample(frac=1.0, random_state=random_seed).reset_index(drop=True)
    summaries = pd.DataFrame(run_summaries)
    return data, summaries


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Export best-cycle assignment teacher dataset from master_log CSV files."
    )
    parser.add_argument("--inputs", nargs="+", required=True)
    parser.add_argument("--output", required=True, help="Output teacher dataset CSV path.")
    parser.add_argument(
        "--summary-output",
        default=None,
        help="Optional per-run summary CSV path. Default: <output>.summary.csv",
    )
    parser.add_argument(
        "--meta-output",
        default=None,
        help="Optional metadata JSON path. Default: <output>.meta.json",
    )
    parser.add_argument("--early-cycles", type=int, nargs="+", default=[1, 2])
    parser.add_argument("--keep-no-switch-ratio", type=float, default=0.25)
    parser.add_argument("--weight-lambda", type=float, default=20.0)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--no-recursive", action="store_true")
    args = parser.parse_args()

    if not (0.0 <= args.keep_no_switch_ratio <= 1.0):
        raise ValueError("--keep-no-switch-ratio must be in [0, 1]")

    files = collect_master_logs(args.inputs, recursive=not args.no_recursive)
    if not files:
        raise ValueError("no master_log CSV files found")

    data, summaries = build_dataset(
        files=files,
        early_cycles=set(args.early_cycles),
        keep_no_switch_ratio=args.keep_no_switch_ratio,
        weight_lambda=args.weight_lambda,
        random_seed=args.seed,
    )

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    summary_output = (
        Path(args.summary_output)
        if args.summary_output
        else output.with_suffix(output.suffix + ".summary.csv")
    )
    meta_output = (
        Path(args.meta_output)
        if args.meta_output
        else output.with_suffix(output.suffix + ".meta.json")
    )
    summary_output.parent.mkdir(parents=True, exist_ok=True)
    meta_output.parent.mkdir(parents=True, exist_ok=True)

    data.to_csv(output, index=False)
    summaries.to_csv(summary_output, index=False)

    meta = {
        "inputs": args.inputs,
        "num_files": len(files),
        "num_runs": int(len(summaries)),
        "num_samples": int(len(data)),
        "features": STATE_FEATURES,
        "label_column": "expert_bs_id",
        "early_cycles": args.early_cycles,
        "keep_no_switch_ratio": args.keep_no_switch_ratio,
        "weight_lambda": args.weight_lambda,
        "seed": args.seed,
        "label_counts": {
            str(k): int(v)
            for k, v in data["expert_bs_id"].value_counts().sort_index().items()
        },
        "noop_counts": {
            str(k): int(v)
            for k, v in data["is_noop_to_best"].value_counts().sort_index().items()
        },
        "condition_counts": {
            str(k): int(v)
            for k, v in data["source_condition"].value_counts().sort_index().items()
        },
        "best_cycle_counts": {
            str(k): int(v)
            for k, v in summaries["best_cycle"].value_counts().sort_index().items()
        },
    }
    meta_output.write_text(json.dumps(meta, indent=2, ensure_ascii=False) + "\n")

    print(f"files={len(files)} runs={len(summaries)} samples={len(data)}")
    print(f"saved_dataset={output}")
    print(f"saved_summary={summary_output}")
    print(f"saved_meta={meta_output}")
    print("label_counts", meta["label_counts"])
    print("noop_counts", meta["noop_counts"])
    print("condition_counts", meta["condition_counts"])
    print("best_cycle_counts", meta["best_cycle_counts"])


if __name__ == "__main__":
    main()
