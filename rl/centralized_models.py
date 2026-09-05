from __future__ import annotations

import torch
from torch import nn

from centralized_protocol import ACTION_DIM as ACTION_DIM_V1, NUM_APS as NUM_APS_V1, NUM_UES as NUM_UES_V1, STATE_DIM as STATE_DIM_V1
from centralized_protocol_v2 import AP_DIM_V2, GLOBAL_DIM_V2, NUM_APS, NUM_UES, STATE_DIM_V2, UE_DIM_V2


class CentralizedMlpQNetwork(nn.Module):
    def __init__(self, state_dim: int, action_dim: int, hidden_dim: int = 512):
        super().__init__()
        self.net = nn.Sequential(
            nn.Linear(state_dim, hidden_dim), nn.ReLU(),
            nn.Linear(hidden_dim, hidden_dim), nn.ReLU(),
            nn.Linear(hidden_dim, action_dim),
        )

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return self.net(x)


class MLP(nn.Module):
    def __init__(self, input_dim: int, output_dim: int, hidden_dim: int):
        super().__init__()
        self.net = nn.Sequential(
            nn.Linear(input_dim, hidden_dim), nn.ReLU(),
            nn.Linear(hidden_dim, output_dim), nn.ReLU(),
        )

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return self.net(x)


class FactorizedQNetwork(nn.Module):
    """Shared UE/AP encoders with a shared pair scorer.

    Input remains a flat v2 state vector; output remains flat Q-values with
    shape [batch, NUM_UES * NUM_APS].  This preserves the existing C++ protocol
    while sharing parameters across UE-AP pairs.
    """

    def __init__(
        self,
        state_dim: int = STATE_DIM_V2,
        action_dim: int = NUM_UES * NUM_APS,
        hidden_dim: int = 256,
        emb_dim: int = 64,
    ):
        super().__init__()
        if state_dim != STATE_DIM_V2:
            raise ValueError(f"factorized_v2 requires state_dim={STATE_DIM_V2}, got {state_dim}")
        if action_dim != NUM_UES * NUM_APS:
            raise ValueError(f"factorized_v2 requires action_dim={NUM_UES * NUM_APS}, got {action_dim}")
        self.state_dim = state_dim
        self.action_dim = action_dim
        self.ue_encoder = MLP(UE_DIM_V2, emb_dim, hidden_dim)
        self.ap_encoder = MLP(AP_DIM_V2, emb_dim, hidden_dim)
        self.global_encoder = MLP(GLOBAL_DIM_V2, emb_dim, hidden_dim)
        # pair features: same_ap, est_sat, est_delta, target users, target rtt.
        self.pair_dim = 5
        self.pair_scorer = nn.Sequential(
            nn.Linear(emb_dim * 3 + self.pair_dim, hidden_dim), nn.ReLU(),
            nn.Linear(hidden_dim, hidden_dim), nn.ReLU(),
            nn.Linear(hidden_dim, 1),
        )

    def _unpack(self, x: torch.Tensor) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor]:
        bsz = x.shape[0]
        ue_end = NUM_UES * UE_DIM_V2
        ap_end = ue_end + NUM_APS * AP_DIM_V2
        ue = x[:, :ue_end].reshape(bsz, NUM_UES, UE_DIM_V2)
        ap = x[:, ue_end:ap_end].reshape(bsz, NUM_APS, AP_DIM_V2)
        glob = x[:, ap_end:ap_end + GLOBAL_DIM_V2]
        return ue, ap, glob

    def _pair_features(self, ue: torch.Tensor, ap: torch.Tensor) -> torch.Tensor:
        bsz = ue.shape[0]
        device = ue.device
        dtype = ue.dtype
        # v2 UE offsets.
        current_onehot = ue[:, :, 1:4]        # [B,U,A]
        est_sat = ue[:, :, 18:21]             # [B,U,A]
        est_delta = ue[:, :, 21:24]           # [B,U,A]
        # v2 AP dynamic offsets.
        target_users = ap[:, :, 5]            # [B,A]
        target_rtt = ap[:, :, 6]              # [B,A]

        same_ap = current_onehot
        users = target_users.unsqueeze(1).expand(bsz, NUM_UES, NUM_APS)
        rtt = target_rtt.unsqueeze(1).expand(bsz, NUM_UES, NUM_APS)
        return torch.stack([same_ap, est_sat, est_delta, users, rtt], dim=-1).to(device=device, dtype=dtype)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        ue, ap, glob = self._unpack(x)
        ue_emb = self.ue_encoder(ue.reshape(-1, UE_DIM_V2)).reshape(x.shape[0], NUM_UES, -1)
        ap_emb = self.ap_encoder(ap.reshape(-1, AP_DIM_V2)).reshape(x.shape[0], NUM_APS, -1)
        g_emb = self.global_encoder(glob).unsqueeze(1).unsqueeze(2).expand(-1, NUM_UES, NUM_APS, -1)
        ue_pair = ue_emb.unsqueeze(2).expand(-1, NUM_UES, NUM_APS, -1)
        ap_pair = ap_emb.unsqueeze(1).expand(-1, NUM_UES, NUM_APS, -1)
        pair_x = self._pair_features(ue, ap)
        scorer_in = torch.cat([ue_pair, ap_pair, g_emb, pair_x], dim=-1)
        q = self.pair_scorer(scorer_in.reshape(-1, scorer_in.shape[-1])).reshape(x.shape[0], NUM_UES * NUM_APS)
        return q


def make_q_network(
    model_type: str,
    state_dim: int,
    action_dim: int,
    hidden_dim: int,
    emb_dim: int = 64,
) -> nn.Module:
    if model_type in {"mlp_v1", "centralized_mlp_v1", "mlp_v2_onehot"}:
        return CentralizedMlpQNetwork(state_dim, action_dim, hidden_dim)
    if model_type in {"factorized_v2", "centralized_factorized_v2"}:
        return FactorizedQNetwork(state_dim, action_dim, hidden_dim=hidden_dim, emb_dim=emb_dim)
    raise ValueError(f"unsupported centralized model_type: {model_type}")


def defaults_for_schema(schema_version: str) -> tuple[int, int]:
    if schema_version in {"centralized_state_v2", "centralized_state_v2_onehot", "v2", "v2_onehot"}:
        return STATE_DIM_V2, NUM_UES * NUM_APS
    return STATE_DIM_V1, ACTION_DIM_V1
