#!/usr/bin/env bash
set -euo pipefail

SUDO=()
if [[ "${EUID}" -ne 0 ]]; then
    if ! command -v sudo >/dev/null 2>&1; then
        echo "This script needs root privileges. Install sudo or run it as root." >&2
        exit 1
    fi
    SUDO=(sudo)
fi

if [[ ! -r /etc/os-release ]]; then
    echo "Cannot read /etc/os-release; this script expects Debian." >&2
    exit 1
fi

# shellcheck disable=SC1091
. /etc/os-release

if [[ "${ID:-}" != "debian" ]]; then
    echo "Warning: expected Debian, detected ${PRETTY_NAME:-unknown OS}." >&2
fi

if [[ -z "${VERSION_CODENAME:-}" ]]; then
    echo "Cannot determine Debian codename from /etc/os-release." >&2
    exit 1
fi

TARGET_USER="${SUDO_USER:-${USER:-root}}"

"${SUDO[@]}" apt-get update
"${SUDO[@]}" apt-get upgrade -y
"${SUDO[@]}" apt-get install -y ca-certificates curl git gnupg ufw fail2ban

"${SUDO[@]}" install -m 0755 -d /etc/apt/keyrings
curl -fsSL https://download.docker.com/linux/debian/gpg \
    | "${SUDO[@]}" gpg --batch --yes --dearmor -o /etc/apt/keyrings/docker.gpg
"${SUDO[@]}" chmod a+r /etc/apt/keyrings/docker.gpg

ARCH="$(dpkg --print-architecture)"
echo "deb [arch=${ARCH} signed-by=/etc/apt/keyrings/docker.gpg] https://download.docker.com/linux/debian ${VERSION_CODENAME} stable" \
    | "${SUDO[@]}" tee /etc/apt/sources.list.d/docker.list >/dev/null

"${SUDO[@]}" apt-get update
"${SUDO[@]}" apt-get install -y \
    docker-ce \
    docker-ce-cli \
    containerd.io \
    docker-buildx-plugin \
    docker-compose-plugin

"${SUDO[@]}" systemctl enable --now docker fail2ban

if id "${TARGET_USER}" >/dev/null 2>&1; then
    "${SUDO[@]}" usermod -aG docker "${TARGET_USER}"
fi

"${SUDO[@]}" ufw default deny incoming
"${SUDO[@]}" ufw default allow outgoing
"${SUDO[@]}" ufw allow 22/tcp
"${SUDO[@]}" ufw allow 80/tcp
"${SUDO[@]}" ufw allow 443/tcp
"${SUDO[@]}" ufw allow 2759/udp
"${SUDO[@]}" ufw --force enable

echo
echo "Base Debian VPS setup complete."
echo "If you ran this as a non-root user, log out and reconnect so Docker group membership takes effect."
