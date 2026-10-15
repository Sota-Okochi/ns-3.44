#!/bin/sh
exec /home/sota/workspace_ns-3.44/ns-allinone-3.44/ns-3.44/build/wifi-band-cache-v1/run-master "$@" --perfDetailed=1 --perfWifiSampleEvery=1024 --perfOutputDir=/home/sota/workspace_ns-3.44/ns-allinone-3.44/ns-3.44/results/perf/wifi_band_cache_50_20261015/timing
