#!/bin/sh
exec /home/sota/workspace_ns-3.44/ns-allinone-3.44/ns-3.44/build/wifi-rx-detail-v1/run-master "$@" --perfDetailed=1 --perfWifiSampleEvery=1024 --perfOutputDir=/home/sota/workspace_ns-3.44/ns-allinone-3.44/ns-3.44/results/perf/wifi_rx_detail_20261015/timing
