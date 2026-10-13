"""Pure observation/QoE, deterministic seeds, shared policy and PPO update.

AP identifiers and UE array indices are zero-based. Missing values are never rewards.
"""
from dataclasses import asdict, dataclass
import hashlib
import json
import math
import random

import torch
from torch import nn

SCHEMA = 'marl_v1'
UE_DIM, AP_DIM, GLOBAL_DIM = 13, 8, 3


def derive(tag, *args):
    wire = json.dumps(['marl_seed_v1', tag, *args], separators=(',', ':'), ensure_ascii=True).encode()
    return int.from_bytes(hashlib.sha256(wire).digest()[:8], 'big') & ((1 << 63) - 1)


def environment_seed(model_seed, episode):
    seeds = list(range(1, 1001))
    random.Random(derive('env_order', model_seed, episode // 1000)).shuffle(seeds)
    return seeds[episode % 1000]


@dataclass
class Config:
    actor_lr: float = 1e-4
    critic_lr: float = 3e-4
    clip: float = .1
    epochs: int = 4
    minibatch: int = 16
    rollout_episodes: int = 16
    entropy: float = .01
    grad_norm: float = .5
    epsilon: float = 1e-6
    browser_mbps: float = 3.
    video_mbps: float = 8.
    voice_ms: float = 100.
    game_ms: float = 40.
    gamma: float = 1.
    gae_lambda: float = 1.
    width: int = 64
    head_width: int = 128

    def validate(self):
        for name in ('epochs', 'minibatch', 'rollout_episodes', 'width', 'head_width'):
            if type(getattr(self, name)) is not int:
                raise ValueError(f'{name} must be an integer')
        if self.gamma != 1 or self.gae_lambda != 1:
            raise ValueError('marl_v1 uses complete finite-horizon returns, gamma=lambda=1')
        for k, v in asdict(self).items():
            if not math.isfinite(v) or v <= 0:
                raise ValueError(f'Invalid configuration: {k}')
        if self.clip >= 1 or self.epsilon >= 1:
            raise ValueError('Invalid clip/epsilon')


def number(x):
    return not isinstance(x, bool) and isinstance(x, (int, float)) and math.isfinite(x)


def scale(x, ref=1.):
    return math.log1p(x / ref) / math.log(2)


def observation(msg, config, previous=None):
    """Return tensors plus raw evaluable QoE. None remains None in raw logs."""
    if msg.get('schema') != SCHEMA or msg.get('cycle') not in range(1, 6):
        raise ValueError('schema/cycle mismatch')
    if msg.get('done') is not (msg['cycle'] == 5):
        raise ValueError('terminal flag mismatch')
    users, aps = msg['users'], msg['aps']
    n = len(users)
    if n < 1 or len(aps) != 3:
        raise ValueError('invalid N/AP count')
    counts, targets, values = [0]*3, [0]*3, [[], [], []]
    raw, u = [], []
    for i, row in enumerate(users):
        if row['ue_id'] != i or type(row['bs']) is not int or row['bs'] not in range(3) or type(row['app']) is not int or row['app'] not in range(4):
            raise ValueError('UE ID/AP/app mismatch')
        ap, app = row['bs'], row['app']
        counts[ap] += 1
        tp = row['tp_mbps']
        if tp is not None and (not number(tp) or tp < 0):
            raise ValueError('invalid TP')
        if app < 2:
            targets[ap] += 1
            if tp is not None:
                values[ap].append(tp)
        status, rtt = aps[ap]['status'], aps[ap]['rtt_ms']
        s_raw = s_eff = None
        if app < 2 and tp is not None:
            s_raw = tp / (config.browser_mbps if app == 0 else config.video_mbps)
        elif app >= 2 and status == 'ok':
            if not number(rtt) or rtt <= 0:
                raise ValueError('invalid RTT')
            s_raw = (config.voice_ms if app == 2 else config.game_ms) / rtt
        elif app >= 2 and status == 'timeout':
            s_eff = config.epsilon
        if s_raw is not None:
            s_eff = max(s_raw, config.epsilon)
        old = previous[i]['s_eff'] if previous is not None else None
        delta = s_eff-old if s_eff is not None and old is not None else None
        u.append([float(app == j) for j in range(4)] + [float(ap == j) for j in range(3)] +
                 [scale(tp, 8) if tp is not None else 0., float(tp is not None),
                  scale(s_eff) if s_eff is not None else 0., float(s_eff is not None),
                  math.copysign(scale(abs(delta)), delta) if delta is not None else 0., float(delta is not None)])
        raw.append(dict(ue_id=i, bs=ap, app=app, tp_mbps=tp, tp_status=row.get('tp_status'),
                        rtt_ms=rtt if app >= 2 else None, rtt_status=status,
                        s_raw=s_raw, s_eff=s_eff, delta_satisfaction=delta))
    a, aggregates = [], []
    for i, row in enumerate(aps):
        if row['ap_id'] != i or row['status'] not in ('ok', 'timeout', 'report_missing', 'probe_send_error', 'measurement_error'):
            raise ValueError('invalid AP report')
        rtt, status = row['rtt_ms'], row['status']
        if status == 'ok' and (not number(rtt) or rtt <= 0):
            raise ValueError('invalid AP RTT')
        if status != 'ok' and rtt is not None:
            raise ValueError('invalid AP RTT/status pairing')
        mean = sum(values[i])/len(values[i]) if values[i] else None
        a.append([counts[i]/n, scale(rtt, 100) if status == 'ok' else 0., float(status == 'ok'),
                  float(status == 'timeout'), scale(mean, 8) if mean is not None else 0.,
                  targets[i]/n, len(values[i])/n, float(mean is not None)])
        aggregates.append(dict(ap_id=i, tp_target_count=targets[i], tp_observed_count=len(values[i]),
                               mean_tp_mbps=mean, num_users=counts[i]))
    h = n / sum(1/x['s_eff'] for x in raw) if all(x['s_eff'] is not None for x in raw) else None
    g = [scale(h) if h is not None else 0., float(h is not None), n/30.]
    obs = dict(ue=torch.tensor(u, dtype=torch.float32), ap=torch.tensor(a, dtype=torch.float32),
               glob=torch.tensor(g, dtype=torch.float32), remaining=torch.tensor([(5-msg['cycle'])/4.]))
    if not all(torch.isfinite(v).all() for v in obs.values()):
        raise ValueError('non-finite model input')
    return obs, dict(h=h, users=raw, aps=aggregates)


class Network(nn.Module):
    def __init__(self, actor, config, seed):
        super().__init__()
        self.actor = actor
        w = config.width
        with torch.random.fork_rng(devices=[]):
            torch.manual_seed(seed)
            self.encoder = nn.Sequential(nn.Linear(UE_DIM, w), nn.ReLU(), nn.Linear(w, w), nn.ReLU())
            dim = w*(2 if actor else 1) + 3*AP_DIM + GLOBAL_DIM + (0 if actor else 1)
            self.head = nn.Sequential(nn.Linear(dim, config.head_width), nn.ReLU(),
                                      nn.Linear(config.head_width, w), nn.ReLU(), nn.Linear(w, 3 if actor else 1))
            for layer in self.modules():
                if isinstance(layer, nn.Linear):
                    nn.init.kaiming_uniform_(layer.weight, nonlinearity='relu')
                    nn.init.zeros_(layer.bias)
            nn.init.orthogonal_(self.head[-1].weight, gain=.01 if actor else 1.)

    def forward(self, obs):
        # [batch,N,features]. No UE padding is used by the initial homogeneous-N runner.
        h = self.encoder(obs['ue'])
        context = torch.cat((h.mean(-2), obs['ap'].flatten(-2), obs['glob']), -1)
        if self.actor:
            context = context.unsqueeze(-2).expand(*h.shape[:-1], context.shape[-1])
            return self.head(torch.cat((h, context), -1))
        return self.head(torch.cat((context, obs['remaining']), -1)).squeeze(-1)


def stack(observations):
    return {k: torch.stack([o[k] for o in observations]) for k in observations[0]}


class Learner:
    def __init__(self, config=None, seed=1):
        self.config = config or Config()
        self.config.validate()
        self.seed, self.updates = seed, 0
        self.actor = Network(True, self.config, derive('actor_init', seed))
        self.critic = Network(False, self.config, derive('critic_init', seed))
        self.actor_opt = torch.optim.Adam(self.actor.parameters(), lr=self.config.actor_lr, eps=1e-8)
        self.critic_opt = torch.optim.Adam(self.critic.parameters(), lr=self.config.critic_lr, eps=1e-8)

    @torch.no_grad()
    def act(self, obs, generator):
        log_probs = self.actor(obs).log_softmax(-1)
        action = torch.multinomial(log_probs.exp(), 1, generator=generator).squeeze(-1)
        return action, log_probs, self.critic(obs)

    def update(self, episodes):
        c = self.config
        if len(episodes) != c.rollout_episodes or any(len(e) != 4 for e in episodes):
            raise ValueError('PPO requires complete 16-episode batches, four transitions each')
        rows, returns = [], []
        for ep in episodes:
            ret = 0.
            rr = []
            for tr in reversed(ep):
                ret += tr['reward']
                rr.append(ret)
            rows.extend(ep)
            returns.extend(reversed(rr))
        obs = stack([r['obs'] for r in rows])
        actions = torch.stack([r['action'] for r in rows])
        old_lp = torch.stack([r['log_probs'] for r in rows])
        old_selected = old_lp.gather(-1, actions.unsqueeze(-1)).squeeze(-1)
        target = torch.tensor(returns, dtype=torch.float32)
        advantage = target-torch.stack([r['value'] for r in rows])
        advantage = (advantage-advantage.mean())/advantage.std(unbiased=False).clamp_min(1e-8)
        metrics = []
        for epoch in range(c.epochs):
            order = list(range(len(rows)))
            random.Random(derive('ppo_order', self.seed, self.updates, epoch)).shuffle(order)
            for begin in range(0, len(order), c.minibatch):
                ids = order[begin:begin+c.minibatch]
                batch = {k: v[ids] for k, v in obs.items()}
                lp = self.actor(batch).log_softmax(-1)
                ratio = (lp.gather(-1, actions[ids].unsqueeze(-1)).squeeze(-1)-old_selected[ids]).exp()
                adv = advantage[ids, None]
                objective = torch.minimum(ratio*adv, ratio.clamp(1-c.clip, 1+c.clip)*adv).mean()
                entropy = -(lp.exp()*lp).sum(-1).mean()
                actor_loss = -objective-c.entropy*entropy
                critic_loss = (self.critic(batch)-target[ids]).square().mean()
                if not torch.isfinite(actor_loss+critic_loss):
                    raise ValueError('Nonfinite PPO loss')
                self.actor_opt.zero_grad(); actor_loss.backward()
                ag = nn.utils.clip_grad_norm_(self.actor.parameters(), c.grad_norm, error_if_nonfinite=True)
                self.actor_opt.step()
                self.critic_opt.zero_grad(); critic_loss.backward()
                cg = nn.utils.clip_grad_norm_(self.critic.parameters(), c.grad_norm, error_if_nonfinite=True)
                self.critic_opt.step()
                metrics.append(dict(actor_loss=float(actor_loss.detach()), critic_loss=float(critic_loss.detach()),
                                    entropy=float(entropy.detach()), clip_fraction=float(((ratio-1).abs()>c.clip).float().mean()),
                                    actor_grad_norm=float(ag), critic_grad_norm=float(cg)))
        with torch.no_grad():
            new_lp = self.actor(obs).log_softmax(-1)
            kl = (old_lp.exp()*(old_lp-new_lp)).sum(-1).mean().item()
        self.updates += 1
        return dict(update=self.updates, kl=kl, **{k: sum(m[k] for m in metrics)/len(metrics) for k in metrics[0]})

    def save(self, path, **metadata):
        state = dict(schema=SCHEMA, config=asdict(self.config), seed=self.seed, updates=self.updates,
                     actor=self.actor.state_dict(), critic=self.critic.state_dict(),
                     actor_opt=self.actor_opt.state_dict(), critic_opt=self.critic_opt.state_dict(),
                     torch_rng=torch.get_rng_state(), **metadata)
        temp = path.with_suffix('.tmp')
        torch.save(state, temp); temp.replace(path)

    def load(self, path):
        ck = torch.load(path, map_location='cpu', weights_only=False)
        if ck['schema'] != SCHEMA or ck['config'] != asdict(self.config) or ck['seed'] != self.seed:
            raise ValueError('Checkpoint schema/config/model seed mismatch')
        self.actor.load_state_dict(ck['actor']); self.critic.load_state_dict(ck['critic'])
        self.actor_opt.load_state_dict(ck['actor_opt']); self.critic_opt.load_state_dict(ck['critic_opt'])
        self.updates = ck['updates']; torch.set_rng_state(ck['torch_rng'])
        return ck
