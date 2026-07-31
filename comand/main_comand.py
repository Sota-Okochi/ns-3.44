#!/usr/bin/env python3
"""Run online_dqn ns-3 simulations sequentially for seeds 21-24.

このスクリプトを実行すると、各 seed について以下を自動で行います。

1. data/setting.json の rngSeed を更新する
2. online DQN 用 Python サーバを起動する
3. ./ns3 run "master --method=online_dqn ..." を実行する
4. シミュレーション終了後に Python サーバを停止する

デフォルトでは seed21, seed22, seed23, seed24 の順に実行します。
"""

from __future__ import annotations

import argparse
import json
import socket
import subprocess
import sys
import time
from pathlib import Path


DEFAULT_SEEDS = [21, 22, 23, 24]
DEFAULT_METHOD = "online_dqn"
DEFAULT_MAX_SWITCHES = 15
DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 50051
DEFAULT_TIMEOUT_MS = 3000


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


def start_online_dqn_server(root: Path, seed: int, host: str, port: int) -> subprocess.Popen:
    checkpoint_out = root / "models" / f"online_dqn_seed{seed}.pt"
    cmd = [
        sys.executable,
        str(root / "rl" / "server.py"),
        "--host",
        host,
        "--port",
        str(port),
        "--seed",
        str(seed),
        "--checkpoint-out",
        str(checkpoint_out),
    ]

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


def ns3_program(method: str, max_switches: int, host: str, port: int, timeout_ms: int) -> str:
    # seed は data/setting.json の rngSeed で切り替えるため、ns-3 コマンド自体は
    # seed20 実行時と同じ形式のままでよい。
    return (
        f"master --method={method} "
        f"--drlServerHost={host} "
        f"--drlServerPort={port} "
        f"--drlTimeoutMs={timeout_ms} "
        f"--maxSwitches={max_switches}"
    )


def ns3_command(method: str, max_switches: int, host: str, port: int, timeout_ms: int) -> list[str]:
    return ["./ns3", "run", ns3_program(method, max_switches, host, port, timeout_ms)]


def printable_command(seed: int, method: str, max_switches: int, host: str, port: int, timeout_ms: int) -> str:
    return f"seed{seed}: ./ns3 run \"{ns3_program(method, max_switches, host, port, timeout_ms)}\""


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run online_dqn simulations sequentially for seed21-24 by default."
    )
    parser.add_argument(
        "--seeds",
        type=int,
        nargs="+",
        default=DEFAULT_SEEDS,
        help="実行する seed のリスト。default: 21 22 23 24",
    )
    parser.add_argument("--method", default=DEFAULT_METHOD, help="実行手法。default: online_dqn")
    parser.add_argument(
        "--maxSwitches",
        type=int,
        default=DEFAULT_MAX_SWITCHES,
        help="1 cycle あたりの最大切り替え数。default: 15",
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
        "--no-server",
        action="store_true",
        help="Python online DQN server を起動せず、既存サーバを使う",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = repo_root()
    setting_path = root / "data" / "setting.json"

    if not setting_path.exists():
        print(f"ERROR: setting.json が見つかりません: {setting_path}", file=sys.stderr)
        return 1

    print("これから実行するコマンド一覧:")
    for i, seed in enumerate(args.seeds, start=1):
        print(f"  {i}. {printable_command(seed, args.method, args.maxSwitches, args.host, args.port, args.drlTimeoutMs)}")
    print("", flush=True)

    for i, seed in enumerate(args.seeds, start=1):
        server_proc: subprocess.Popen | None = None
        try:
            print("=" * 80, flush=True)
            print(f"[{i}/{len(args.seeds)}] rngSeed を {seed} に設定します: {setting_path}", flush=True)
            update_rng_seed(setting_path, seed)

            if args.method == "online_dqn" and not args.no_server:
                server_proc = start_online_dqn_server(root, seed, args.host, args.port)

            cmd = ns3_command(args.method, args.maxSwitches, args.host, args.port, args.drlTimeoutMs)
            print(
                f"[{i}/{len(args.seeds)}] 実行開始: "
                f"{printable_command(seed, args.method, args.maxSwitches, args.host, args.port, args.drlTimeoutMs)}",
                flush=True,
            )
            result = subprocess.run(cmd, cwd=root)
            if result.returncode != 0:
                print(
                    f"ERROR: コマンドが失敗しました (returncode={result.returncode}): {' '.join(cmd)}",
                    file=sys.stderr,
                )
                return result.returncode

            print(f"[{i}/{len(args.seeds)}] seed{seed} 実行完了", flush=True)
        except Exception as exc:
            print(f"ERROR: seed{seed} の実行中に失敗しました: {exc}", file=sys.stderr)
            return 1
        finally:
            stop_online_dqn_server(server_proc)

    print("=" * 80, flush=True)
    print("seed21, seed22, seed23, seed24 のシミュレーションが正常終了しました。", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
