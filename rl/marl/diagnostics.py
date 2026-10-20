"""Passive PPO update diagnostics; no environment failure-rate aggregation."""
import torch


@torch.no_grad()
def update_statistics(target, old_values, new_values, old_lp, new_lp):
    # Float64 statistics only; the PPO optimization tensors are not modified.
    target = target.detach().double()
    old_values = old_values.detach().double()
    new_values = new_values.detach().double()
    advantage = target - old_values
    variance = target.var(unbiased=False)

    def explained(values):
        if variance.item() <= 1e-12:
            return None
        return (1 - (target - values).var(unbiased=False) / variance).item()

    per_ue_kl = (old_lp.double().exp() * (old_lp.double() - new_lp.double())).sum(-1)
    return dict(
        explained_variance_before=explained(old_values),
        explained_variance_after=explained(new_values),
        advantage_raw_mean=advantage.mean().item(),
        advantage_raw_std=advantage.std(unbiased=False).item(),
        return_mean=target.mean().item(),
        return_std=target.std(unbiased=False).item(),
        joint_kl=per_ue_kl.sum(-1).mean().item(),
    )

