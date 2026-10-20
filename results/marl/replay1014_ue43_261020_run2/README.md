# UE43 passive diagnosis replay

Launch: 2026-10-20 JST. No training; checkpoint256 frozen, model_seed=1, environment_seed=1014.
Original sources, checkpoint and binary are unchanged. Copied diagnostic sources are in
`build/marl-ue43-trace-v1/`; compile/link commands and hashes are recorded there and in
`build_provenance.json`. Original runtime libraries are reused unchanged.

Command:
```
python3 scripts/diagnose_marl_rtt.py \
 --source-episode results/marl/train80_b8_episodes256_261019/checkpoint_000256/validation_000003_seed1014 \
 --checkpoint results/marl/train80_b8_episodes256_261019/checkpoint_000256.pt \
 --binary build/marl-ue43-trace-v1/run-master \
 --output results/marl/replay1014_ue43_261020_run2 --trace
```

The first attempt `replay1014_ue43_261020` failed before ns-3 launch because the sandbox
blocked local TCP. This second run was launched with the required permission.

Outputs under `validation_000003_seed1014/simulation/80/marl_ppo/`:
- ue43_path.csv: target-IP IPv4 Tx/Rx/drop, ARP packets seen at Wi-Fi MAC,
  ARP protocol drop, MAC/MPDU drop, UE association/deassociation.
  UE PHY receive drops include all observed frames, not just target data;
  source/destination columns are MAC addresses for those rows.
- ue43_nodes.csv: IP/interface/device/MAC mapping; ue43 role is explicit.
- ue43_sink.csv: actual PacketSink Rx: time,uid,bytes,source_address (no header).
- flow_per_cycle.csv: all terminal flows, including tx/rx/drop counts.
  IMPORTANT: legacy cycle column can differ from MARL cycle; use time_s.
  term_idx is 1-based (UE43 is term_idx=44).

Caveats: passive tracing does not guarantee every loss layer is visible. In particular,
A-MSDU aggregate payloads may not be decoded by the IP-filtered MAC callback. Cross-check
IPv4, sink and flow records rather than treating missing MAC rows as conclusive loss.
No RNG draws, network parameters or simulated-time events were added by these traces.

Completion criteria:
- episode status.json valid=true
- comparison.json exact_equal true for observations.jsonl, transitions.jsonl, qoe.csv
The runner performs this byte comparison automatically and fails on differences.
Until comparison.json exists, full replay equivalence has not been verified.
Runner timeout is 7200 wall seconds. See status.json for any failure.
