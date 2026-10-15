#!/bin/sh
exec /home/sota/workspace_ns-3.44/ns-allinone-3.44/ns-3.44/build/marl-internal-report-v1/master "$@" --perfDetailed=1 --perfWifiSampleEvery=1024 --perfOutputDir=/home/sota/workspace_ns-3.44/ns-allinone-3.44/ns-3.44/results/perf/marl80_internal_seed384_20261015/timing
