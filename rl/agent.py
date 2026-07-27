from __future__ import annotations

import os
import random
from dataclasses import dataclass
from pathlib import Path

import torch
from torch import nn
import torch.nn.functional as F

from replay_buffer import ReplayBuffer


class QNetwork(nn.Module):
    def __init__(self, state_dim: int = 9, action_dim: int = 3, hidden_dim: int = 128):
        super().__init__()
        self.net = nn.Sequential(
            nn.Linear(state_dim, hidden_dim), nn.ReLU(),
            nn.Linear(hidden_dim, hidden_dim), nn.ReLU(),
            nn.Linear(hidden_dim, action_dim),
        )

    def forward(self, x):
        return self.net(x)


@dataclass
class AgentConfig:
    state_dim: int = 9
    action_dim: int = 3
    hidden_dim: int = 128
    gamma: float = 0.99
    lr: float = 1e-3
    buffer_size: int = 100_000
    seed: int = 1
    device: str = "cpu"


class OnlineDQNAgent:
    def __init__(self, cfg: AgentConfig):
        self.cfg = cfg
        random.seed(cfg.seed)
        torch.manual_seed(cfg.seed)
        self.device = torch.device(cfg.device)
        self.q = QNetwork(cfg.state_dim, cfg.action_dim, cfg.hidden_dim).to(self.device)
        self.target = QNetwork(cfg.state_dim, cfg.action_dim, cfg.hidden_dim).to(self.device)
        self.target.load_state_dict(self.q.state_dict())
        self.optim = torch.optim.Adam(self.q.parameters(), lr=cfg.lr)
        self.replay = ReplayBuffer(cfg.buffer_size, cfg.seed)

    @torch.no_grad()
    def select_action(self, state: list[float], epsilon: float = 0.1) -> tuple[int, list[float]]:
        if random.random() < epsilon:
            action = random.randrange(self.cfg.action_dim)
            q_values = self.q(torch.tensor([state], dtype=torch.float32, device=self.device))[0].tolist()
            return action, q_values
        q_values_t = self.q(torch.tensor([state], dtype=torch.float32, device=self.device))[0]
        return int(torch.argmax(q_values_t).item()), q_values_t.tolist()

    def remember(self, s, a: int, r: float, s_next, done: bool = False) -> None:
        self.replay.push(s, a, r, s_next, done)

    def update(self, batch_size: int = 64) -> float | None:
        if len(self.replay) < batch_size:
            return None
        batch = self.replay.sample(batch_size)
        s = torch.tensor([b.state for b in batch], dtype=torch.float32, device=self.device)
        a = torch.tensor([b.action for b in batch], dtype=torch.long, device=self.device).unsqueeze(1)
        r = torch.tensor([b.reward for b in batch], dtype=torch.float32, device=self.device).unsqueeze(1)
        ns = torch.tensor([b.next_state for b in batch], dtype=torch.float32, device=self.device)
        done = torch.tensor([b.done for b in batch], dtype=torch.float32, device=self.device).unsqueeze(1)
        q_sa = self.q(s).gather(1, a)
        with torch.no_grad():
            target = r + self.cfg.gamma * (1.0 - done) * self.target(ns).max(dim=1, keepdim=True).values
        loss = F.smooth_l1_loss(q_sa, target)
        self.optim.zero_grad()
        loss.backward()
        self.optim.step()
        return float(loss.item())

    def sync_target(self) -> None:
        self.target.load_state_dict(self.q.state_dict())

    def save_checkpoint(self, path: str | os.PathLike, extra: dict | None = None) -> None:
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        torch.save({"config": self.cfg.__dict__, "q": self.q.state_dict(), "target": self.target.state_dict(), "extra": extra or {}}, path)

    def load_checkpoint(self, path: str | os.PathLike) -> None:
        ckpt = torch.load(path, map_location=self.device)
        self.q.load_state_dict(ckpt["q"])
        self.target.load_state_dict(ckpt.get("target", ckpt["q"]))
