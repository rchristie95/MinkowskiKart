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
