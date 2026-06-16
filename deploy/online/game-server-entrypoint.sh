#!/bin/sh
set -eu

require_env() {
    eval "value=\${$1:-}"
    if [ -z "$value" ]; then
        echo "$1 is required." >&2
        exit 1
    fi
}

require_env MK_ONLINE_DOMAIN
require_env MK_OFFICIAL_USERNAME
require_env MK_OFFICIAL_PASSWORD

api_health_url="https://${MK_ONLINE_DOMAIN}/healthz"
echo "Waiting for ${api_health_url}..."
until curl --fail --silent --show-error --max-time 5 "$api_health_url" >/dev/null; do
    sleep 5
done

mkdir -p "$HOME/logs"

# Self-healing: a stale saved session left in the data volume from a previous
# run hijacks sign-in - the server keeps retrying an invalid saved-session
# token, never reaches OS_SIGNED_IN, times out after 20s and exits, which makes
# the container crash-loop. Drop any saved profile so --init-user always does a
# fresh password login (and writes a fresh, valid session).
rm -rf "$HOME"/.config/supertuxkart/config-* 2>/dev/null || true

echo "Initializing the official host profile..."
/opt/minkowskikart/bin/MinkowskiKart \
    --init-user \
    --login="${MK_OFFICIAL_USERNAME}" \
    --password="${MK_OFFICIAL_PASSWORD}"

exec /opt/minkowskikart/bin/MinkowskiKart \
    --server-config=/config/server_config.xml \
    --login="${MK_OFFICIAL_USERNAME}" \
    --password="${MK_OFFICIAL_PASSWORD}" \
    --port=2759 \
    --track="${MK_OFFICIAL_TRACK:-mobius_track}" \
    --server-ai="${MK_OFFICIAL_BOTS:-3}" \
    --min-players="${MK_OFFICIAL_MIN_PLAYERS:-1}" \
    --no-firewalled-server
