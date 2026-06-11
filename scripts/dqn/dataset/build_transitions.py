#!/usr/bin/env python3
"""Build DQN transition CSVs from master_log CSVs.

A transition is one learning sample: (s_t, a_t, r_t, s_{t+1}, done).

Default target mode is `flag_only`, which uses only rows whose
`target_ue_flag == 1` as action targets.  `all_users` can be used later to
create one transition per UE per cycle.

For both modes, the next state is taken from the same UE in the next cycle.
This makes it possible to inspect how the acted UE changed after the action.
"""

from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path
from typing import Dict, Iterable, List, Sequence, Tuple

REQUIRED_COLUMNS = {
    "seed",
    "cycle_id",
    "ue_id",
    "current_bs_id",
    "app_type",
    "tp_mbps",
    "rtt_ms",
    "satisfaction",
    "satisfaction_class",
    "num_users_on_current_bs",
    "harmonic_mean",
    "num_unsatisfied_users",
    "target_ue_flag",
    "action_selected_bs_id",
    "switch_flag",
    "measurement_valid",
}

OUTPUT_COLUMNS = [
    # identifiers
    "episode_id",
    "source_file",
    "seed",
    "cycle_id",
    "target_ue_id",
    "target_mode",
    # state s_t
    "current_bs_id",
    "app_type",
    "tp_mbps",
    "rtt_ms",
    "satisfaction",
    "satisfaction_class",
    "num_users_on_current_bs",
    "harmonic_mean",
    "num_unsatisfied_users",
    "target_ue_flag",
    # action a_t
    "action_selected_bs_id",
    "switch_flag",
    # reward r_t, recomputed as H_{t+1} - H_t
    "reward",
    # next state s_{t+1}: same UE in next cycle
    "next_cycle_id",
    "next_current_bs_id",
    "next_app_type",
    "next_tp_mbps",
    "next_rtt_ms",
    "next_satisfaction",
    "next_satisfaction_class",
    "next_num_users_on_current_bs",
    "next_harmonic_mean",
    "next_num_unsatisfied_users",
    "next_target_ue_flag",
    # acted UE outcome aliases for analysis/readability
    "acted_ue_bs_after",
    "acted_ue_tp_after",
    "acted_ue_rtt_after",
    "acted_ue_satisfaction_after",
    # terminal flag
    "done",
    # validity
    "measurement_valid",
    "next_measurement_valid",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build DQN transition CSVs from master_log CSVs."
    )
    parser.add_argument(
        "--input",
        "-i",
        nargs="+",
        required=True,
        help="Input master_log CSV file(s) or glob pattern(s).",
    )
    parser.add_argument(
        "--output-dir",
        default="episodes/dqn/transitions",
        help="Output directory for transition CSVs.",
    )
    parser.add_argument(
        "--output",
        help="Output CSV path. Use only when exactly one input file is resolved.",
    )
    parser.add_argument(
        "--target-mode",
        choices=["flag_only", "all_users"],
        default="flag_only",
        help="flag_only: use target_ue_flag==1 rows only; all_users: use every UE row.",
    )
    parser.add_argument(
        "--strict-one-flag",
        action="store_true",
        help="In flag_only mode, fail if a cycle does not have exactly one target_ue_flag==1 row.",
    )
    return parser.parse_args()


def resolve_inputs(patterns: Sequence[str]) -> List[Path]:
    paths: List[Path] = []
    for pattern in patterns:
        p = Path(pattern)
        if p.exists():
            paths.append(p)
            continue
        matches = sorted(Path().glob(pattern))
        paths.extend(matches)
    # keep order but remove duplicates
    seen = set()
    unique: List[Path] = []
    for p in paths:
        resolved = p.resolve()
        if resolved not in seen:
            seen.add(resolved)
            unique.append(p)
    return unique


def read_rows(path: Path) -> Tuple[List[Dict[str, str]], List[str]]:
    with path.open(newline="") as f:
        reader = csv.DictReader(f)
        fieldnames = reader.fieldnames or []
        rows = list(reader)
    missing = sorted(REQUIRED_COLUMNS - set(fieldnames))
    if missing:
        raise ValueError(f"{path}: missing required columns: {missing}")
    return rows, fieldnames


def as_int(row: Dict[str, str], key: str) -> int:
    return int(float(row[key]))


def as_float(row: Dict[str, str], key: str) -> float:
    return float(row[key])


def group_by_cycle(rows: Iterable[Dict[str, str]]) -> Dict[int, List[Dict[str, str]]]:
    grouped: Dict[int, List[Dict[str, str]]] = {}
    for row in rows:
        grouped.setdefault(as_int(row, "cycle_id"), []).append(row)
    return grouped


def index_cycle_by_ue(rows: Iterable[Dict[str, str]]) -> Dict[int, Dict[str, str]]:
    indexed: Dict[int, Dict[str, str]] = {}
    for row in rows:
        indexed[as_int(row, "ue_id")] = row
    return indexed


def select_current_rows(
    cycle_rows: List[Dict[str, str]], target_mode: str
) -> List[Dict[str, str]]:
    if target_mode == "all_users":
        return list(cycle_rows)
    return [row for row in cycle_rows if as_int(row, "target_ue_flag") == 1]


def make_episode_id(path: Path, row: Dict[str, str]) -> str:
    if row.get("episode_id"):
        return row["episode_id"]
    return path.stem


def build_transitions_for_file(
    path: Path, target_mode: str, strict_one_flag: bool = False
) -> List[Dict[str, str]]:
    rows, _ = read_rows(path)
    if not rows:
        return []

    by_cycle = group_by_cycle(rows)
    cycles = sorted(by_cycle)
    max_transition_start = len(cycles) - 1
    transitions: List[Dict[str, str]] = []

    for idx, cycle in enumerate(cycles[:-1]):
        next_cycle = cycles[idx + 1]
        current_rows = by_cycle[cycle]
        next_by_ue = index_cycle_by_ue(by_cycle[next_cycle])

        selected_rows = select_current_rows(current_rows, target_mode)
        if target_mode == "flag_only":
            if strict_one_flag and len(selected_rows) != 1:
                raise ValueError(
                    f"{path}: cycle {cycle} has {len(selected_rows)} target rows; expected 1"
                )
            if len(selected_rows) != 1:
                print(
                    f"[WARN] {path}: cycle {cycle} has {len(selected_rows)} target rows; "
                    "using available target rows.",
                    file=sys.stderr,
                )

        done = 1 if idx == max_transition_start - 1 else 0
        for cur in selected_rows:
            ue_id = as_int(cur, "ue_id")
            nxt = next_by_ue.get(ue_id)
            if nxt is None:
                print(
                    f"[WARN] {path}: missing next row for cycle={cycle}, ue_id={ue_id}; skipped.",
                    file=sys.stderr,
                )
                continue

            reward = as_float(nxt, "harmonic_mean") - as_float(cur, "harmonic_mean")
            transition = {
                "episode_id": make_episode_id(path, cur),
                "source_file": path.name,
                "seed": cur["seed"],
                "cycle_id": cur["cycle_id"],
                "target_ue_id": cur["ue_id"],
                "target_mode": target_mode,
                "current_bs_id": cur["current_bs_id"],
                "app_type": cur["app_type"],
                "tp_mbps": cur["tp_mbps"],
                "rtt_ms": cur["rtt_ms"],
                "satisfaction": cur["satisfaction"],
                "satisfaction_class": cur["satisfaction_class"],
                "num_users_on_current_bs": cur["num_users_on_current_bs"],
                "harmonic_mean": cur["harmonic_mean"],
                "num_unsatisfied_users": cur["num_unsatisfied_users"],
                "target_ue_flag": cur["target_ue_flag"],
                "action_selected_bs_id": cur["action_selected_bs_id"],
                "switch_flag": cur["switch_flag"],
                "reward": f"{reward:.6f}",
                "next_cycle_id": nxt["cycle_id"],
                "next_current_bs_id": nxt["current_bs_id"],
                "next_app_type": nxt["app_type"],
                "next_tp_mbps": nxt["tp_mbps"],
                "next_rtt_ms": nxt["rtt_ms"],
                "next_satisfaction": nxt["satisfaction"],
                "next_satisfaction_class": nxt["satisfaction_class"],
                "next_num_users_on_current_bs": nxt["num_users_on_current_bs"],
                "next_harmonic_mean": nxt["harmonic_mean"],
                "next_num_unsatisfied_users": nxt["num_unsatisfied_users"],
                "next_target_ue_flag": nxt["target_ue_flag"],
                "acted_ue_bs_after": nxt["current_bs_id"],
                "acted_ue_tp_after": nxt["tp_mbps"],
                "acted_ue_rtt_after": nxt["rtt_ms"],
                "acted_ue_satisfaction_after": nxt["satisfaction"],
                "done": str(done),
                "measurement_valid": cur["measurement_valid"],
                "next_measurement_valid": nxt["measurement_valid"],
            }
            transitions.append(transition)

    return transitions


def write_transitions(path: Path, rows: List[Dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=OUTPUT_COLUMNS)
        writer.writeheader()
        writer.writerows(rows)


def default_output_path(input_path: Path, output_dir: Path, target_mode: str) -> Path:
    return output_dir / f"transitions_{target_mode}_{input_path.stem}.csv"


def main() -> int:
    args = parse_args()
    inputs = resolve_inputs(args.input)
    if not inputs:
        print("No input files resolved.", file=sys.stderr)
        return 2
    if args.output and len(inputs) != 1:
        print("--output can be used only with exactly one resolved input file.", file=sys.stderr)
        return 2

    output_dir = Path(args.output_dir)
    total = 0
    for input_path in inputs:
        transitions = build_transitions_for_file(
            input_path,
            target_mode=args.target_mode,
            strict_one_flag=args.strict_one_flag,
        )
        out_path = Path(args.output) if args.output else default_output_path(
            input_path, output_dir, args.target_mode
        )
        write_transitions(out_path, transitions)
        total += len(transitions)
        print(f"wrote {len(transitions)} transitions: {out_path}")
    print(f"total transitions: {total}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
