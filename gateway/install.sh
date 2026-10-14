#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Erez Yaary
# Set up a Raspberry Pi (or any Debian/Ubuntu/Raspberry Pi OS computer)
# as ECal's gateway: the Atari sends its calendar requests to the Pi
# in plain HTTP on your home network, and ecalgate.py makes the HTTPS
# connections to iCloud and the other calendar servers.
#
#   sudo ./install.sh                         anyone on the home network may use it
#   sudo ./install.sh --allow 192.168.1.50    only the Atari at that address
#   sudo ./install.sh --port 8180             another port (8180 is the default)
#   sudo ./install.sh --allow-local           also reach servers at home (Radicale, Nextcloud)
#
# Run it again to change the settings; `sudo ./install.sh --remove` takes it away.
set -euo pipefail

PORT=8180
ALLOW_IP=""
ARGS=()
while [ $# -gt 0 ]; do
  case "$1" in
    --allow) ALLOW_IP=${2:?--allow needs the address of the Atari}; ARGS+=(--allow "$ALLOW_IP"); shift 2 ;;
    --port) PORT=${2:?--port needs a number}; shift 2 ;;
    --allow-local) ARGS+=(--allow-local); shift ;;
    --remove)
      systemctl disable --now ecalgate.service 2>/dev/null || true
      rm -f /etc/systemd/system/ecalgate.service /usr/local/bin/ecalgate.py
      systemctl daemon-reload
      echo "ecalgate removed."
      exit 0 ;;
    *) echo "usage: install.sh [--allow ATARI_IP] [--port N] [--allow-local] [--remove]" >&2; exit 2 ;;
  esac
done

if [ "$(id -u)" != 0 ]; then
  echo "Please run with sudo." >&2
  exit 1
fi
if ! command -v python3 >/dev/null; then
  apt-get update -qq
  apt-get install -y -qq python3
fi
# the system's root certificates, for checking the servers
[ -f /etc/ssl/certs/ca-certificates.crt ] || apt-get install -y -qq ca-certificates

HERE=$(cd "$(dirname "$0")" && pwd)
install -m 755 "$HERE/ecalgate.py" /usr/local/bin/ecalgate.py
id ecalgate >/dev/null 2>&1 || useradd --system --no-create-home --shell /usr/sbin/nologin ecalgate

cat > /etc/systemd/system/ecalgate.service <<UNIT
[Unit]
Description=ECal gateway for the Atari (HTTPS for CalDAV and subscriptions)
After=network-online.target
Wants=network-online.target

[Service]
User=ecalgate
ExecStart=/usr/bin/python3 /usr/local/bin/ecalgate.py --port $PORT ${ARGS[*]:-}
Restart=always
RestartSec=5
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes

[Install]
WantedBy=multi-user.target
UNIT

systemctl daemon-reload
systemctl enable ecalgate.service >/dev/null
systemctl restart ecalgate.service

if command -v ufw >/dev/null && ufw status | grep -q "Status: active"; then
  if [ -n "$ALLOW_IP" ]; then
    ufw allow from "$ALLOW_IP" to any port "$PORT" proto tcp >/dev/null
  else
    ufw allow from 192.168.0.0/16 to any port "$PORT" proto tcp >/dev/null
    ufw allow from 10.0.0.0/8 to any port "$PORT" proto tcp >/dev/null
  fi
fi

sleep 1
IP=$(hostname -I 2>/dev/null | awk '{print $1}')
IP=${IP:-PI-ADDRESS}
if systemctl is-active --quiet ecalgate.service; then
  echo
  echo "ecalgate is running. In ECal on the Atari, Options > Connection:"
  echo "   Gateway:  $IP"
  echo "   Port:     $PORT"
  echo
  echo "Check it from any browser on your network: http://$IP:$PORT/"
else
  echo "ecalgate didn't start; see: journalctl -u ecalgate" >&2
  exit 1
fi
