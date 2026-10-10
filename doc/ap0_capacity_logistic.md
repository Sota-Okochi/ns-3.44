# AP0 capacity variation with logistic

ns-3.44; 80 UEs, seed 1001, 5 cycles, fixed positions (`--mob=1`).
PGW-CER transmit rates change in both directions: 80 -> 40 -> 80 Mbps.
Cycle 2 starts at 13.5 s; cycle 4 at 32.5 s. Queues/in-flight packets are not reset.
AP1/AP2 remain 40/20 Mbps. This abstracts available backhaul capacity, not RF degradation.
The logistic policy is unchanged; unlike no_switch it can reassign terminals.

```bash
./ns3 build master -j 4
./ns3 run 'master --settingPath=data/scenarios/ap0_capacity_80_seed1001.json --method=logistic --rngSeed=1001 --mob=1 --pgwCerRate=80Mbps --ap0CapacityVariation=1 --ap0LowRate=40Mbps --ap0DropCycle=2 --ap0RecoveryCycle=4 --ap1CapacityVariation=0 --ap2CapacityVariation=0 --outputRoot=results/ap0_capacity_logistic/variable' --no-build
```

Output: `results/ap0_capacity_logistic/variable/80/logistic/`.
Capacity metadata, original settings, CLI, initial assignment, bidirectional events,
queue samples and completion marker are under `ap0_capacity/<run_id>/`.
Completion marker indicates Simulator::Run returned, not process exit status.
Validate a specific run with:
`python3 scripts/check_ap0_capacity.py <run_directory> <master_log.csv>`.
The checker allows logistic switching; no_switch retains fixed-assignment checks.

For a matched constant-capacity logistic baseline use the same command with
`--ap0CapacityVariation=0 --ap0CapacityTrace=1` and outputRoot
`results/ap0_capacity_logistic/constant`.
Compare same-cycle global H, unsatisfied count, switch count, and per-app QoE;
check non-switched users too. Historical no_switch results are a policy comparison,
not a constant-capacity logistic control. One seed is a functional check, not general evidence.

Validation commands:
- `python3 -m unittest discover -s tests -p test_ap0_capacity.py`: 5 passed.
- `python3 -m unittest discover -s tests -p test_ap2_capacity.py`: 4 passed.
- Combined AP tests: existing AP1 scenario test failed because its scenario currently
  has 100 terminals rather than the expected 80; that file was not modified here.
- Initial sandbox build failed on ccache temporary-directory permissions; retried with approval.
- Approved `./ns3 build master -j 4`: passed (all affected master objects rebuilt).
- 3-UE smoke: copied the scenario to `/tmp/ap0-logistic-smoke.json`, changed only
  terminals to 3; ran the documented command with that settingPath and outputRoot
  `/tmp/ap0-logistic-smoke`. Exit 0; all 5 cycles completed.
- `python3 scripts/check_ap0_capacity.py /tmp/ap0-logistic-smoke/3/logistic/ap0_capacity/* /tmp/ap0-logistic-smoke/3/logistic/master_log_*.csv`: passed.
- 80-UE production experiment not run by assistant. Smoke is not evidence of QoE improvement.
