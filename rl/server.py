from __future__ import annotations

import argparse
import json
import socketserver
from dataclasses import asdict
from pathlib import Path

from agent import AgentConfig, OnlineDQNAgent
from protocol import STATE_FEATURES, make_error, state_to_vector


class OnlineDqnService:
    def __init__(self, args):
        cfg = AgentConfig(state_dim=len(STATE_FEATURES), action_dim=args.action_dim, seed=args.seed, lr=args.lr, gamma=args.gamma, device=args.device)
        self.agent = OnlineDQNAgent(cfg)
        if args.checkpoint and Path(args.checkpoint).exists():
            self.agent.load_checkpoint(args.checkpoint)
        self.args = args
        self.pending: dict[tuple[int, int, int], tuple[list[float], int, float]] = {}
        self.last_cycle: int | None = None
        self.steps = 0

    def handle(self, msg: dict) -> dict:
        if msg.get("type", "act") != "act":
            return make_error("unsupported message type")
        state = state_to_vector(msg.get("state", {}))
        ue_id = int(msg.get("target_ue_id", -1))
        cycle_id = int(msg.get("cycle_id", msg.get("state", {}).get("cycle_id", 0)))
        step_id = int(msg.get("step_id", 0))
        h_now = float(msg.get("harmonic_mean", msg.get("state", {}).get("harmonic_mean", 0.0)))
        done = bool(msg.get("done", False))

        # Delayed reward: when the next cycle's measurements arrive, all
        # pending actions from the previous cycle get reward = H_current - H_t.
        loss = None
        if self.last_cycle is not None and cycle_id != self.last_cycle:
            for _key, (prev_s, prev_a, prev_h) in list(self.pending.items()):
                self.agent.remember(prev_s, prev_a, h_now - prev_h, state, done)
                maybe_loss = self.agent.update(self.args.batch_size)
                if maybe_loss is not None:
                    loss = maybe_loss
            self.pending.clear()
        self.last_cycle = cycle_id

        epsilon = float(msg.get("epsilon", self.args.epsilon))
        action, q_values = self.agent.select_action(state, epsilon)
        self.pending[(cycle_id, step_id, ue_id)] = (state, action, h_now)
        self.steps += 1
        if self.steps % self.args.target_sync_interval == 0:
            self.agent.sync_target()
        if self.args.checkpoint_out and self.steps % self.args.checkpoint_interval == 0:
            self.agent.save_checkpoint(self.args.checkpoint_out, {"steps": self.steps, "features": STATE_FEATURES})
        return {"type": "action", "selected_bs_id": action, "q_values": q_values, "loss": loss, "steps": self.steps}


class Handler(socketserver.StreamRequestHandler):
    def handle(self):
        line = self.rfile.readline().decode("utf-8").strip()
        try:
            msg = json.loads(line)
            resp = self.server.service.handle(msg)  # type: ignore[attr-defined]
        except Exception as exc:  # keep ns-3 alive; it will skip/keep current on error
            resp = make_error(str(exc))
        self.wfile.write((json.dumps(resp) + "\n").encode("utf-8"))


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=50051)
    p.add_argument("--action-dim", type=int, default=3)
    p.add_argument("--epsilon", type=float, default=0.1)
    p.add_argument("--batch-size", type=int, default=64)
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--lr", type=float, default=1e-3)
    p.add_argument("--gamma", type=float, default=0.99)
    p.add_argument("--device", default="cpu")
    p.add_argument("--checkpoint", default="")
    p.add_argument("--checkpoint-out", default="models/online_dqn.pt")
    p.add_argument("--checkpoint-interval", type=int, default=10)
    p.add_argument("--target-sync-interval", type=int, default=100)
    args = p.parse_args()

    class Server(socketserver.ThreadingTCPServer):
        allow_reuse_address = True

    with Server((args.host, args.port), Handler) as srv:
        srv.service = OnlineDqnService(args)  # type: ignore[attr-defined]
        print(f"[OnlineDQN] listening on {args.host}:{args.port} features={STATE_FEATURES}", flush=True)
        srv.serve_forever()

if __name__ == "__main__":
    main()
