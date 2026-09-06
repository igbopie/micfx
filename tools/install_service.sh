#!/bin/bash
# tools/install_service.sh — install micfx (+ Bluetooth music) on the Pi.
# Run from the repo dir, with sudo (type the password):
#   sudo ./tools/install_service.sh
# Idempotent: safe to re-run after config changes or updates.
# What it does:
#   micfx voice processor as an always-on system service (autostart on boot,
#     restart on failure), plus Bluetooth A2DP-sink bring-up ("karaoke"):
#     unblocks the radio persistently, sets name/class, infinite
#     pairable timeout, enables + starts bluealsa.
set -eu
cd "$(dirname "$0")/.."

echo "=== packages ==="
apt-get install -y rfkill bluez-alsa-utils alsa-utils

echo "=== micfx service ==="
pkill -x micfx || true
install -m 755 src/micfx /usr/local/bin/micfx
install -d -m 755 /etc/micfx
install -m 644 config/micfx.conf /etc/micfx/micfx.conf
install -m 644 systemd/micfx.service /etc/systemd/system/micfx.service
systemctl daemon-reload
systemctl enable --now micfx.service

echo "=== bluetooth ==="
rfkill unblock bluetooth
mkdir -p /var/lib/systemd/rfkill
echo 0 > /var/lib/systemd/rfkill/platform-3f201000.serial:bluetooth 2>/dev/null || true

cp -n /etc/bluetooth/main.conf /etc/bluetooth/main.conf.bak || true
python3 - <<'EOF'
import re
p = '/etc/bluetooth/main.conf'
s = open(p).read()
def setv(k, v):
    global s
    s2 = re.sub(rf'^#?{k}\s*=.*$', f'{k} = {v}', s, flags=re.M)
    s = s2 if s2 != s else s + f'\n{k} = {v}\n'
setv('Name', 'karaoke')
setv('Class', '0x20041C')
setv('DiscoverableTimeout', '0')
setv('PairableTimeout', '0')
setv('FastConnectable', 'true')
open(p, 'w').write(s)
EOF

systemctl enable --now bluetooth.service bluealsa.service
systemctl restart bluetooth.service
sleep 2

echo "=== status ==="
systemctl status micfx.service --no-pager | head -n 6
rfkill list bluetooth || true
hciconfig hci0 | head -n 3 || true
