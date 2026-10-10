# Wi-Fi AP2 backhaul capacity change (ns-3.44)

AP2 means master_log BS ID 2 (1-based ID 3). Only the two transmitters of the AP2–router PointToPoint link change rate; AP0 (5G PGW–CER) and Wi-Fi AP1 remain constant. AP1's separate opt-in variation must remain disabled. This is an abstraction of available backhaul service capacity, not an RF or background-traffic model.

Settings: `data/scenarios/ap2_capacity_80_seed1001.json` (80 UEs, seed 1001, five 9.5-second cycles; cycle 1 begins at 4.0s). AP2 starts at 20 Mbps; it drops to 10 Mbps at cycle 2 (13.5s), recovers to 20 Mbps at cycle 4 (32.5s). Both P2P transmitters are updated. Queues, in-flight transmissions and other links are not reset/changed. Initial UE/application mix remains seed-selected rather than forced.

## Run from repository root

```bash
./ns3 build master -j 4

./ns3 run 'master --settingPath=data/scenarios/ap2_capacity_80_seed1001.json --method=no_switch --rngSeed=1001 --mob=1 --ap1CapacityVariation=0 --ap2CapacityVariation=1 --ap2LowRate=10Mbps --ap2DropCycle=2 --ap2RecoveryCycle=4 --outputRoot=results/ap2_capacity/variable' --no-build
```

Only the changed condition is needed for the requested run. Save the full `results/ap2_capacity/variable/80/no_switch/` directory after normal process exit. `master_log_*.csv` is accompanied by `ap2_capacity/<run_id>/` containing event/rate/queue samples, `setting.json`, effective CLI metadata, initial assignment/application table and `simulation_completed.txt`. Wait for `=====Simulator::End()=====` and the shell prompt before treating diagnostics as complete. The completion file denotes `Simulator::Run()` returned/log files closed, not shell process exit.

Check one run using matching files:

```bash
python3 scripts/check_ap2_capacity.py \
  results/ap2_capacity/variable/80/no_switch/ap2_capacity/<run_id> \
  results/ap2_capacity/variable/80/no_switch/master_log_<filename>.csv
python3 -m unittest discover -s tests -p 'test_ap2_capacity.py'
```

The checker verifies cycle event times and rates in both directions, all sampled rates, diagnostic completion, seed, all UE-cycle rows and no handovers. `queue_samples.csv` contains NetDevice and QueueDisc backlog/drop counters, not application goodput or exact link utilization. Master-log RTT is the per-base-station monitor-terminal proxy, not individual application RTT. Compare app-specific TP/satisfaction and overall harmonic mean; do not conclude on mean TP or unsatisfied count alone.

The AP2 10 Mbps reduction is a 50% reduction from 20 Mbps. Because AP2 already serves a relatively large video-heavy group, monitor queue drops/measurement validity and the satisfaction floor as well as unsatisfied-user count. The 80-UE run is not performed during implementation; user-provided output should be analyzed only after normal completion.
