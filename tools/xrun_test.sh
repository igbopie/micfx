#!/bin/bash
# tools/xrun_test.sh — baseline playback test on the Pi.
# Usage (on the Pi, from the dir with tone built):
#   ./xrun_test.sh [device] [seconds]
# Measures xruns + %CPU with the test sine. Stage 1 gate:
#   0 xruns in 60 s at 48 kHz / period 128 before moving on.
set -u
DEV="${1:-hw:0,0}"
SECS="${2:-60}"
./tone -D "$DEV" -t "$SECS" -r 48000 -p 128
