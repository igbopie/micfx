#!/bin/bash
# tools/xrun_test.sh — test base de playback en la Pi.
# Uso (en la Pi, desde el dir con tone compilado):
#   ./xrun_test.sh [device] [segundos]
# Mide xruns + %CPU con el seno de prueba. Gate etapa 1:
#   0 xruns en 60 s a 48 kHz / periodo 128 antes de seguir.
set -u
DEV="${1:-hw:0,0}"
SECS="${2:-60}"
./tone -D "$DEV" -t "$SECS" -r 48000 -p 128
