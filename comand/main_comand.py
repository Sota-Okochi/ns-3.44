#!/usr/bin/env python3
"""Run ns-3 simulations sequentially for log collection / online DQN.

主な用途:

1. 事前学習用ログ収集（デフォルト）

   multi_greedy K=1   15 seed

   実行例:

     python3 comand/main_comand.py

2. 任意 method の連続実行

   実行例:

     python3 comand/main_comand.py --preset custom --method multi_greedy --maxSwitches 4 --seeds 1 2 3

3. online_dqn の連続実行

   実行例:

     python3 comand/main_comand.py --preset custom --method online_dqn --maxSwitches 1 --seeds 1 2 3

処理内容:

- data/setting.json の rngSeed を自動更新
- ./ns3 run "master ..." を順番に実行
- online_dqn の場合だけ Python DQN server を起動
- 終了時に data/setting.json を元の内容へ復元
"""

from __future__ import annotations

import argparse
import json
import os
import socket
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path


DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 50051
DEFAULT_TIMEOUT_MS = 3000


@dataclass(frozen=True)
class RunJob:
    method: str
    max_switches: int
    seed: int


def repo_root() -> Path:
    # ns-3.44/comand/main_comand.py -> ns-3.44
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


def wait_for_server(host: str, port: int, proc: subprocess.Popen, timeout_sec: float = 20.0) -> None:
    deadline = time.time() + timeout_sec
    last_error: Exception | None = None

    while time.time() < deadline:
        if proc.poll() is not None:
            raise RuntimeError(f"online DQN server exited early (returncode={proc.returncode})")
        try:
            with socket.create_connection((host, port), timeout=0.5):
                return
        except OSError as exc:
            last_error = exc
            time.sleep(0.2)

    raise TimeoutError(f"online DQN server did not start on {host}:{port}: {last_error}")


def start_online_dqn_server(
    root: Path,
    seed: int,
    host: str,
    port: int,
    checkpoint: str,
    checkpoint_out: str,
    epsilon: float,
    eval_only: bool,
) -> subprocess.Popen:
    if checkpoint_out:
        checkpoint_out_path = Path(checkpoint_out)
    else:
        checkpoint_out_path = root / "models" / f"online_dqn_seed{seed}.pt"

    cmd = [
        sys.executable,
        str(root / "rl" / "server.py"),
        "--host",
        host,
        "--port",
        str(port),
        "--seed",
        str(seed),
        "--epsilon",
        str(epsilon),
        "--checkpoint-out",
        str(checkpoint_out_path),
    ]
    if checkpoint:
        cmd.extend(["--checkpoint", checkpoint])
    if eval_only:
        cmd.append("--eval-only")

    print(f"online DQN server 起動: {' '.join(cmd)}", flush=True)
    proc = subprocess.Popen(cmd, cwd=root)
    wait_for_server(host, port, proc)
    return proc


def stop_online_dqn_server(proc: subprocess.Popen | None) -> None:
    if proc is None or proc.poll() is not None:
        return

    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=5)


def ns3_program(method: str, max_switches: int, host: str, port: int, timeout_ms: int, safety_threshold: float) -> str:
    parts = [
        f"master --method={method}",
        f"--maxSwitches={max_switches}",
    ]
    if method == "online_dqn":
        parts.extend(
            [
                f"--drlServerHost={host}",
                f"--drlServerPort={port}",
                f"--drlTimeoutMs={timeout_ms}",
                f"--onlineDqnSafetyThreshold={safety_threshold}",
            ]
        )
    return " ".join(parts)


def ns3_command(method: str, max_switches: int, host: str, port: int, timeout_ms: int, safety_threshold: float) -> list[str]:
    return ["./ns3", "run", ns3_program(method, max_switches, host, port, timeout_ms, safety_threshold)]


def printable_command(job: RunJob, host: str, port: int, timeout_ms: int, safety_threshold: float) -> str:
    return (
        f"seed{job.seed}: ./ns3 run \""
        f"{ns3_program(job.method, job.max_switches, host, port, timeout_ms, safety_threshold)}"
        f"\""
    )


def build_pretrain_k1_jobs() -> list[RunJob]:
    # 一旦、事前学習用ログのうち multi_greedy K=1 の seed1-15 だけを収集する。
    return [RunJob("multi_greedy", 1, seed) for seed in range(13, 16)]


def build_custom_jobs(args: argparse.Namespace) -> list[RunJob]:
    return [RunJob(args.method, args.maxSwitches, seed) for seed in args.seeds]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run ns-3 simulations sequentially.")
    parser.add_argument(
        "--preset",
        choices=["pretrain-k1", "custom"],
        default="pretrain-k1",
        help="実行プリセット。default: pretrain-k1",
    )
    parser.add_argument(
        "--seeds",
        type=int,
        nargs="+",
        default=[1],
        help="custom preset で実行する seed のリスト。default: 1",
    )
    parser.add_argument("--method", default="online_dqn", help="custom preset の実行手法。default: online_dqn")
    parser.add_argument(
        "--maxSwitches",
        type=int,
        default=1,
        help="custom preset の 1 cycle あたり最大切り替え数。default: 1",
    )
    parser.add_argument("--host", default=DEFAULT_HOST, help="online DQN server host")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="online DQN server port")
    parser.add_argument(
        "--drlTimeoutMs",
        type=int,
        default=DEFAULT_TIMEOUT_MS,
        help="online DQN request timeout [ms]。default: 3000",
    )
    parser.add_argument(
        "--onlineDqnSafetyThreshold",
        type=float,
        default=0.0,
        help="online_dqn の safety filter threshold。default: 0.0",
    )
    parser.add_argument(
        "--no-server",
        action="store_true",
        help="online_dqn でも Python server を起動せず、既存サーバを使う",
    )
    parser.add_argument("--checkpoint", default="", help="online_dqn server に渡す checkpoint")
    parser.add_argument("--checkpoint-out", default="", help="online_dqn server の checkpoint 保存先")
    parser.add_argument("--epsilon", type=float, default=0.1, help="online_dqn server の epsilon。default: 0.1")
    parser.add_argument("--eval-only", action="store_true", help="online_dqn server を eval-only で起動")
    parser.add_argument(
        "--no-restore-setting",
        action="store_true",
        help="実行後に data/setting.json を復元しない",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="実行せず、コマンド一覧だけ表示する",
    )
    return parser.parse_args()


def run_job(root: Path, setting_path: Path, job: RunJob, index: int, total: int, args: argparse.Namespace) -> int:
    server_proc: subprocess.Popen | None = None
    try:
        print("=" * 80, flush=True)
        print(
            f"[{index}/{total}] method={job.method} maxSwitches={job.max_switches} rngSeed={job.seed} を設定します: {setting_path}",
            flush=True,
        )
        update_rng_seed(setting_path, job.seed)

        if job.method == "online_dqn" and not args.no_server:
            server_proc = start_online_dqn_server(
                root,
                job.seed,
                args.host,
                args.port,
                args.checkpoint,
                args.checkpoint_out,
                args.epsilon,
                args.eval_only,
            )

        cmd = ns3_command(
            job.method,
            job.max_switches,
            args.host,
            args.port,
            args.drlTimeoutMs,
            args.onlineDqnSafetyThreshold,
        )
        print(
            f"[{index}/{total}] 実行開始: "
            f"{printable_command(job, args.host, args.port, args.drlTimeoutMs, args.onlineDqnSafetyThreshold)}",
            flush=True,
        )
        env = os.environ.copy()
        # sandbox / WSL 環境で ccache が read-only な場所を使う事故を避ける。
        env.setdefault("CCACHE_DIR", "/tmp/ccache")
        env.setdefault("CCACHE_TEMPDIR", "/tmp")
        result = subprocess.run(cmd, cwd=root, env=env)
        if result.returncode != 0:
            print(
                f"ERROR: コマンドが失敗しました (returncode={result.returncode}): {' '.join(cmd)}",
                file=sys.stderr,
            )
            return result.returncode

        print(f"[{index}/{total}] 完了: method={job.method} seed={job.seed}", flush=True)
        return 0
    except Exception as exc:
        print(f"ERROR: method={job.method} seed={job.seed} の実行中に失敗しました: {exc}", file=sys.stderr)
        return 1
    finally:
        stop_online_dqn_server(server_proc)


def main() -> int:
    args = parse_args()
    root = repo_root()
    setting_path = root / "data" / "setting.json"

    if not setting_path.exists():
        print(f"ERROR: setting.json が見つかりません: {setting_path}", file=sys.stderr)
        return 1

    original_setting = load_json(setting_path)
    jobs = build_pretrain_k1_jobs() if args.preset == "pretrain-k1" else build_custom_jobs(args)

    print("これから実行するコマンド一覧:")
    for i, job in enumerate(jobs, start=1):
        print(f"  {i}. {printable_command(job, args.host, args.port, args.drlTimeoutMs, args.onlineDqnSafetyThreshold)}")
    print(f"合計 {len(jobs)} runs", flush=True)

    if args.dry_run:
        print("dry-run のため実行しません。", flush=True)
        return 0

    try:
        for i, job in enumerate(jobs, start=1):
            code = run_job(root, setting_path, job, i, len(jobs), args)
            if code != 0:
                return code
    finally:
        if not args.no_restore_setting:
            write_json(setting_path, original_setting)
            print(f"data/setting.json を実行前の内容に復元しました: {setting_path}", flush=True)

    print("=" * 80, flush=True)
    print("全シミュレーションが正常終了しました。", flush=True)
    print("期待される出力先:")
    print("  OUTPUT/80/multi_greedy_K1/")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
