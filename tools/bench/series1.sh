#!/usr/bin/env bash
# A/B series for roadmap 2.3: Bloodborne-specific costs.
B=$HOME/kr-port/tools/bench/bench.sh
$B base_a
$B base_b
$B motion_off BB_OBJECT_MOTION=0
$B upscaler_off BB_UPSCALER=none
$B traps_off BB_IMAGE_TRAPS=0
$B watch_off BB_LAYER_WATCH=0
$B hooks_off BB_GUEST_HOOKS=0
echo SERIES_DONE
