#!/usr/bin/env python3
"""Run ./ns3 run master -- --method=all5g for multiple rngSeed values.

このスクリプトは data/setting.json の rngSeed を 1,2,3 に書き換えながら、
以下の ns-3 実行を順番に行います。

    ./ns3 run master -- --method=all5g

実行例:
    python3 command/method_comand.py

コマンドだけ確認:
    python3 command/method_comand.py --dry-run
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path


def repo_root() -> Path:
    # ns-3.44/command/method_comand.py -> ns-3.44
    return Path(__file__).resolve().parents[1]


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def write_json(path: Path, data: dict) -> None:
    with path.open("w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
        f.write("\n")


def update_rng_seed(setting_path: Path, seed: int) -> None:
    setting = load_json(setting_path)
    setting["rngSeed"] = seed
    write_json(setting_path, setting)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run all5g method automatically for seed values 1, 2, and 3."
    )
    parser.add_argument(
        "--seeds",
        type=int,
        nargs="+",
        default=[1032, 1033, 1034, 1035],
        help="実行する rngSeed のリスト。default: 1 2 3",
    )
    parser.add_argument(
        "--method",
        default="all5g",
        help="実行する method。default: all5g",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="実行せず、更新予定の seed とコマンドだけ表示する",
    )
    parser.add_argument(
        "--no-restore-setting",
        action="store_true",
        help="終了後に data/setting.json を実行前の内容へ戻さない",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = repo_root()
    setting_path = root / "data" / "setting.json"

    if not setting_path.exists():
        print(f"ERROR: setting.json が見つかりません: {setting_path}", file=sys.stderr)
        return 1

    original_setting = load_json(setting_path)
    command = ["./ns3", "run", "master", "--", f"--method={args.method}"]

    print("これから実行する内容:")
    for index, seed in enumerate(args.seeds, start=1):
        print(f"  {index}. rngSeed={seed}: {' '.join(command)}")

    if args.dry_run:
        print("dry-run のため実行しません。")
        return 0

    try:
        for index, seed in enumerate(args.seeds, start=1):
            print("=" * 80, flush=True)
            print(f"[{index}/{len(args.seeds)}] rngSeed={seed} に設定します", flush=True)
            update_rng_seed(setting_path, seed)

            print(f"[{index}/{len(args.seeds)}] 実行開始: {' '.join(command)}", flush=True)
            result = subprocess.run(command, cwd=root)
            if result.returncode != 0:
                print(
                    f"ERROR: rngSeed={seed} の実行が失敗しました "
                    f"(returncode={result.returncode})",
                    file=sys.stderr,
                )
                return result.returncode

            print(f"[{index}/{len(args.seeds)}] 完了: rngSeed={seed}", flush=True)

        print("全シミュレーションが正常終了しました。")
        return 0
    finally:
        if not args.no_restore_setting:
            write_json(setting_path, original_setting)
            print(f"data/setting.json を実行前の内容に復元しました: {setting_path}")


if __name__ == "__main__":
    raise SystemExit(main())
