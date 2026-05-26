#!/bin/sh
set -eu

if [ -z "${MK_OFFICIAL_USERNAME:-}" ] || [ -z "${MK_OFFICIAL_PASSWORD:-}" ]; then
    echo "MK_OFFICIAL_USERNAME and MK_OFFICIAL_PASSWORD are required." >&2
    exit 1
fi

exec /opt/minkowskikart/bin/MinkowskiKart \
    --server-config=/config/server_config.xml \
    --login="${MK_OFFICIAL_USERNAME}" \
    --password="${MK_OFFICIAL_PASSWORD}" \
    --port=2759 \
    --no-firewalled-server
