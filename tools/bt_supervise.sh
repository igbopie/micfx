#!/bin/bash
# tools/bt_supervise.sh — keep Bluetooth pairing healthy (runs as igbopie).
# Started from cron @reboot; loops forever:
#   - keeps the auto-accept agent (bt-agent) alive; re-registers it after
#     bluetoothd restarts (which silently drop agent registrations and
#     cause "Access denied" on A2DP connect)
#   - keeps the adapter discoverable + pairable
#   - trusts every paired device so reconnects route audio automatically
set -eu
LOG=/tmp/bt_supervise.log
exec >>"$LOG" 2>&1
echo "bt_supervise started $(date)"
while true; do
    pgrep -x bt-agent >/dev/null || {
        echo "$(date) starting bt-agent"
        nohup bt-agent -c NoInputNoOutput >/tmp/bt-agent.log 2>&1 &
    }
    bluetoothctl -- discoverable on >/dev/null 2>&1 || true
    bluetoothctl -- pairable on >/dev/null 2>&1 || true
    bluetoothctl -- paired-devices 2>/dev/null | awk '{print $2}' | while read -r mac; do
        [ -n "$mac" ] && bluetoothctl -- trust "$mac" >/dev/null 2>&1 || true
    done
    sleep 30
done
