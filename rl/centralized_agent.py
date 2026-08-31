from __future__ import annotations

import math
import random

import torch

from agent import AgentConfig, OnlineDQNAgent


class CentralizedDQNAgent(OnlineDQNAgent):
    @torch.no_grad()
    def select_action_masked(
        self,
        state: list[float],
        valid_action_ids: list[int],
        epsilon: float = 0.1,
    ) -> tuple[int, list[float]]:
        q_values_t = self.q(torch.tensor([state], dtype=torch.float32, device=self.device))[0]
        q_values = q_values_t.tolist()
        valid = [a for a in valid_action_ids if 0 <= int(a) < self.cfg.action_dim]
        if not valid:
            return -1, q_values
        if random.random() < epsilon:
            return int(random.choice(valid)), q_values
        best_action = max(valid, key=lambda a: (q_values[a], -a))
        return int(best_action), q_values


def make_agent(
    state_dim: int,
    action_dim: int,
    seed: int,
    hidden_dim: int,
    lr: float,
    gamma: float,
    device: str,
) -> CentralizedDQNAgent:
    cfg = AgentConfig(
        state_dim=state_dim,
        action_dim=action_dim,
        hidden_dim=hidden_dim,
        lr=lr,
        gamma=gamma,
        seed=seed,
        device=device,
    )
    return CentralizedDQNAgent(cfg)
