#!/usr/bin/env bash
set -euo pipefail

if [[ "${EUID}" -eq 0 ]]; then
    echo "Run this script as the normal Ubuntu user, not root." >&2
    exit 1
fi

sudo apt-get update
sudo apt-get upgrade -y
sudo apt-get install -y ca-certificates curl docker.io docker-compose-v2 fail2ban git ufw

sudo systemctl enable --now docker fail2ban
sudo usermod -aG docker "${USER}"

sudo ufw default deny incoming
sudo ufw default allow outgoing
sudo ufw allow OpenSSH
sudo ufw allow 80/tcp
sudo ufw allow 443/tcp
sudo ufw allow 2759/udp
sudo ufw --force enable

echo
echo "Base VPS setup complete."
echo "Log out and reconnect so your Docker group membership takes effect."
