#!/usr/bin/env python3
"""Run ns-3 master simulations sequentially for fixed seeds and maxSwitches.

This script updates data/setting.json's rngSeed before each run, then executes
./ns3 run "master --method=multi_greedy --maxSwitches=<K>" one by one.
"""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path


# Execute in the exact order requested: seed9 K=4, seed9 K=6, seed10 K=4, seed10 K=6.
RUNS = [
    {"seed": 9, "method": "multi_greedy", "max_switches": 4},
    {"seed": 9, "method": "multi_greedy", "max_switches": 6},
    {"seed": 10, "method": "multi_greedy", "max_switches": 4},
    {"seed": 10, "method": "multi_greedy", "max_switches": 6},
]


def repo_root() -> Path:
    # ns-3.44/comand/main_comand.py -> ns-3.44
    return Path(__file__).resolve().parents[1]


def update_rng_seed(setting_path: Path, seed: int) -> None:
    with setting_path.open("r", encoding="utf-8") as f:
        setting = json.load(f)

    setting["rngSeed"] = seed

    with setting_path.open("w", encoding="utf-8") as f:
        json.dump(setting, f, ensure_ascii=False, indent=2)
        f.write("\n")


def ns3_command(run: dict[str, int | str]) -> list[str]:
    program = f"master --method={run['method']} --maxSwitches={run['max_switches']}"
    return ["./ns3", "run", program]


def printable_command(run: dict[str, int | str]) -> str:
    return f"seed{run['seed']}: ./ns3 run \"master --method={run['method']} --maxSwitches={run['max_switches']}\""


def main() -> int:
    root = repo_root()
    setting_path = root / "data" / "setting.json"

    if not setting_path.exists():
        print(f"ERROR: setting.json が見つかりません: {setting_path}", file=sys.stderr)
        return 1

    print("これから実行するコマンド一覧:")
    for i, run in enumerate(RUNS, start=1):
        print(f"  {i}. {printable_command(run)}")
    print("", flush=True)

    for i, run in enumerate(RUNS, start=1):
        seed = int(run["seed"])
        cmd = ns3_command(run)

        print("=" * 80, flush=True)
        print(f"[{i}/{len(RUNS)}] rngSeed を {seed} に設定します: {setting_path}", flush=True)
        update_rng_seed(setting_path, seed)

        print(f"[{i}/{len(RUNS)}] 実行開始: {printable_command(run)}", flush=True)
        result = subprocess.run(cmd, cwd=root)
        if result.returncode != 0:
            print(
                f"ERROR: コマンドが失敗しました (returncode={result.returncode}): {' '.join(cmd)}",
                file=sys.stderr,
            )
            return result.returncode
        print(f"[{i}/{len(RUNS)}] 実行完了", flush=True)

    print("=" * 80, flush=True)
    print("すべてのコマンドが正常終了しました。", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
