from __future__ import annotations

from collections import deque
from dataclasses import dataclass
import random
from typing import Deque, Iterable


@dataclass
class Transition:
    state: list[float]
    action: int
    reward: float
    next_state: list[float]
    done: bool


class ReplayBuffer:
    def __init__(self, capacity: int = 100_000, seed: int = 1):
        self._buf: Deque[Transition] = deque(maxlen=capacity)
        self._rng = random.Random(seed)

    def __len__(self) -> int:
        return len(self._buf)

    def push(self, state, action: int, reward: float, next_state, done: bool = False) -> None:
        self._buf.append(Transition(list(state), int(action), float(reward), list(next_state), bool(done)))

    def sample(self, batch_size: int) -> list[Transition]:
        return self._rng.sample(list(self._buf), min(batch_size, len(self._buf)))
