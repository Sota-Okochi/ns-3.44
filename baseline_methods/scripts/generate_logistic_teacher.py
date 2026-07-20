#!/usr/bin/env python3
"""Generate teacher data for logistic AP assignment imitation learning.

The label is the AP selected by the current Hungarian solver.
Features are intentionally restricted to values that can be reproduced in ns-3:
application type, current AP, AP user counts, AP RTTs, estimated AP TP, and
estimated satisfaction for each AP.
"""

from __future__ import annotations

import argparse
import csv
import random
from pathlib import Path
import sys
from typing import Dict, List

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from simulation.config import load_app_config, load_sim_config
from simulation.services import cal, create, rand
from simulation.algorithms import hungarian_kai as hung


FEATURE_COLUMNS = [
    "app_type",
    "current_ap",
    "num_users_ap0",
    "num_users_ap1",
    "num_users_ap2",
    "rtt_ap0",
    "rtt_ap1",
    "rtt_ap2",
    "estimated_tp_ap0",
    "estimated_tp_ap1",
    "estimated_tp_ap2",
]

METADATA_COLUMNS = ["run_id", "seed", "ue_id"]
LABEL_COLUMN = "assigned_ap"



def _build_feature_rows(run_id: int, seed: int, terms, aps) -> List[Dict]:
    num_users = [float(ap.termNum) for ap in aps]
    rtts = [float(ap.rtt) for ap in aps]

    # In this baseline, TP is still AP-level. We store it as estimated_tp_ap{j}
    # so that ns-3 can later provide AP-level predicted/estimated TP with the
    # same feature name and model interface.
    estimated_tps = [float(ap.tp) for ap in aps]

    rows = []
    for term in terms:
        app_type = int(term.appNum)
        row = {
            "run_id": run_id,
            "seed": seed,
            "ue_id": int(term.id),
            "app_type": app_type,
            "current_ap": int(term.apBssid),
        }

        for ap_idx in range(3):
            row[f"num_users_ap{ap_idx}"] = num_users[ap_idx]
            row[f"rtt_ap{ap_idx}"] = rtts[ap_idx]
            row[f"estimated_tp_ap{ap_idx}"] = estimated_tps[ap_idx]

        rows.append(row)

    return rows


def generate_dataset(num_runs: int, base_seed: int, output: Path, term_num: int | None = None) -> None:
    conf_sim = load_sim_config()
    if int(conf_sim["apNumMax"]) != 3:
        raise ValueError("This logistic dataset generator assumes exactly 3 APs.")

    output.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = METADATA_COLUMNS + FEATURE_COLUMNS + [LABEL_COLUMN]

    with output.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()

        for run_id in range(num_runs):
            seed = base_seed + run_id
            random.seed(seed)
            np.random.seed(seed)

            aps = create.createAp(conf_sim["apNumMax"])
            terms = create.createTerm(term_num if term_num is not None else conf_sim["termNum"])

            rand.randAp(terms, aps)
            rand.randApp(terms, aps)
            cal.sumTermAp(terms, aps)
            cal.calLink(terms, aps, conf_sim["appUseSec"])

            rows = _build_feature_rows(run_id, seed, terms, aps)

            # Hungarian solver updates terms[i].apBssid to the teacher assignment.
            hung.call_hungarian(terms, aps)

            for row, term in zip(rows, terms):
                row[LABEL_COLUMN] = int(term.apBssid)
                writer.writerow(row)

            print(f"generated run {run_id + 1}/{num_runs}")

    print(f"saved: {output}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--num-runs", type=int, default=100)
    parser.add_argument("--base-seed", type=int, default=1)
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("data/raw/logistic_teacher.csv"),
    )
    parser.add_argument(
        "--term-num",
        type=int,
        default=None,
        help="Optional debug override. If omitted, config/sim.json termNum is used.",
    )
    args = parser.parse_args()

    generate_dataset(args.num_runs, args.base_seed, args.output, args.term_num)


if __name__ == "__main__":
    main()
