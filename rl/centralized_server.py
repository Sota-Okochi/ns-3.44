from __future__ import annotations

import argparse
import json
import socketserver
from pathlib import Path

import torch

from centralized_agent import make_agent
from centralized_protocol import ACTION_DIM, NUM_APS, STATE_DIM, make_error, state_to_vector


class CentralizedDqnService:
    def __init__(self, args):
        self.agent = make_agent(
            state_dim=args.state_dim,
            action_dim=args.action_dim,
            seed=args.seed,
            hidden_dim=args.hidden_dim,
            lr=args.lr,
            gamma=args.gamma,
            device=args.device,
        )
        self.normalization_enabled = False
        self.feature_mean: list[float] | None = None
        self.feature_std: list[float] | None = None
        if args.checkpoint and Path(args.checkpoint).exists():
            # load_checkpoint() は重みだけを読むため，normalization統計量はここで別途読む。
            ckpt = torch.load(args.checkpoint, map_location=args.device)
            extra = ckpt.get("extra", {}) if isinstance(ckpt, dict) else {}
            norm = extra.get("normalization", {}) if isinstance(extra, dict) else {}
            self.normalization_enabled = bool(norm.get("enabled", False))
            self.feature_mean = norm.get("feature_mean")
            self.feature_std = norm.get("feature_std")
            self.agent.load_checkpoint(args.checkpoint)
        self.args = args
        self.pending: dict[tuple[int, int], tuple[list[float], int, float]] = {}
        self.last_cycle: int | None = None
        self.steps = 0

    def handle(self, msg: dict) -> dict:
        if msg.get("type", "act") != "act":
            return make_error("unsupported message type")

        state = state_to_vector(msg.get("state", {}))
        if self.normalization_enabled and self.feature_mean is not None and self.feature_std is not None:
            if len(self.feature_mean) != len(state) or len(self.feature_std) != len(state):
                return make_error("normalization_state_dim_mismatch")
            state = [
                (x - mean) / (std if abs(std) >= 1e-12 else 1.0)
                for x, mean, std in zip(state, self.feature_mean, self.feature_std)
            ]
        cycle_id = int(msg.get("cycle_id", msg.get("state", {}).get("cycle_id", 0)))
        step_id = int(msg.get("step_id", 0))
        h_now = float(msg.get("harmonic_mean", 0.0))
        prev_cycle_reward = msg.get("prev_cycle_reward")
        prev_cycle_measured_reward = float(msg.get("prev_cycle_measured_reward", 0.0))
        prev_cycle_switch_count = float(msg.get("prev_cycle_switch_count", 0.0))
        prev_cycle_num_degraded_users = float(msg.get("prev_cycle_num_degraded_users", 0.0))
        done = bool(msg.get("done", False))

        loss = None
        if not self.args.eval_only:
            if self.last_cycle is not None and cycle_id != self.last_cycle:
                for _key, (prev_s, prev_a, prev_h) in list(self.pending.items()):
                    if prev_cycle_reward is not None:
                        reward = float(prev_cycle_reward)
                    else:
                        reward = (
                            prev_cycle_measured_reward
                            - self.args.reward_switch_penalty_alpha * prev_cycle_switch_count
                            - self.args.reward_degraded_penalty_beta * prev_cycle_num_degraded_users
                        )
                        if prev_cycle_measured_reward == 0.0 and prev_cycle_switch_count == 0.0 and prev_cycle_num_degraded_users == 0.0:
                            reward = h_now - prev_h
                    self.agent.remember(prev_s, prev_a, reward, state, done)
                    maybe_loss = self.agent.update(self.args.batch_size)
                    if maybe_loss is not None:
                        loss = maybe_loss
                self.pending.clear()
        self.last_cycle = cycle_id

        valid_action_ids = [int(x) for x in msg.get("valid_action_ids", [])]
        epsilon = 0.0 if self.args.eval_only else float(msg.get("epsilon", self.args.epsilon))
        action_id, q_values = self.agent.select_action_masked(state, valid_action_ids, epsilon)
        self.steps += 1

        if action_id < 0:
            return {
                "type": "action",
                "action_id": -1,
                "target_ue_id": -1,
                "selected_bs_id": -1,
                "q_value": 0.0,
                "q_values": [],
                "stop": True,
                "loss": loss,
                "steps": self.steps,
                "eval_only": self.args.eval_only,
            }

        target_ue_id = action_id // NUM_APS + 1
        selected_bs_id = action_id % NUM_APS
        q_value = q_values[action_id] if 0 <= action_id < len(q_values) else 0.0
        if not self.args.eval_only:
            self.pending[(cycle_id, step_id)] = (state, action_id, h_now)
            if self.steps % self.args.target_sync_interval == 0:
                self.agent.sync_target()
            if self.args.checkpoint_out and self.steps % self.args.checkpoint_interval == 0:
                self.agent.save_checkpoint(
                    self.args.checkpoint_out,
                    {"steps": self.steps, "state_dim": self.args.state_dim, "action_dim": self.args.action_dim, "normalization": {"enabled": self.normalization_enabled, "feature_mean": self.feature_mean, "feature_std": self.feature_std}},
                )
        return {
            "type": "action",
            "action_id": action_id,
            "target_ue_id": target_ue_id,
            "selected_bs_id": selected_bs_id,
            "q_value": q_value,
            # Full q_values are large and not currently needed by C++ logs.
            "q_values": [],
            "loss": loss,
            "steps": self.steps,
            "eval_only": self.args.eval_only,
        }


class Handler(socketserver.StreamRequestHandler):
    def handle(self):
        line = self.rfile.readline().decode("utf-8").strip()
        try:
            msg = json.loads(line)
            resp = self.server.service.handle(msg)  # type: ignore[attr-defined]
        except Exception as exc:
            resp = make_error(str(exc))
        self.wfile.write((json.dumps(resp) + "\n").encode("utf-8"))


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=50051)
    p.add_argument("--state-dim", type=int, default=STATE_DIM)
    p.add_argument("--action-dim", type=int, default=ACTION_DIM)
    p.add_argument("--hidden-dim", type=int, default=512)
    p.add_argument("--epsilon", type=float, default=0.1)
    p.add_argument("--batch-size", type=int, default=64)
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--lr", type=float, default=1e-3)
    p.add_argument("--gamma", type=float, default=0.99)
    p.add_argument("--device", default="cpu")
    p.add_argument("--checkpoint", default="")
    p.add_argument("--checkpoint-out", default="models/centralized_dqn.pt")
    p.add_argument("--checkpoint-interval", type=int, default=10)
    p.add_argument("--target-sync-interval", type=int, default=100)
    p.add_argument("--reward-switch-penalty-alpha", type=float, default=0.001)
    p.add_argument("--reward-degraded-penalty-beta", type=float, default=0.001)
    p.add_argument("--eval-only", "--no-update", action="store_true", dest="eval_only")
    args = p.parse_args()

    class Server(socketserver.ThreadingTCPServer):
        allow_reuse_address = True

    with Server((args.host, args.port), Handler) as srv:
        srv.service = CentralizedDqnService(args)  # type: ignore[attr-defined]
        mode = "eval-only" if args.eval_only else "online-learning"
        print(
            f"[CentralizedDQN] listening on {args.host}:{args.port} mode={mode} state_dim={args.state_dim} action_dim={args.action_dim} normalization={srv.service.normalization_enabled}",
            flush=True,
        )
        srv.serve_forever()


if __name__ == "__main__":
    main()
