# MinkowskiKart Online API

This service replaces the SuperTuxKart online directory for the v7
MinkowskiKart protocol. Account creation is invite-only and operator-managed.
It implements authenticated server publication, server listing, and the AES
rendezvous handoff used by the existing ENet lobby connection flow.

## Local Run

```powershell
py -3.12 -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -e ".[test]"
python -m app.admin create-user --username host
uvicorn app.main:app --reload
```

The production Docker image runs `alembic upgrade head` before starting the
API. Use the same command after changing migrations in another deployment.

For a local desktop client test, temporarily set `OnlineServer` in
`data/stk_config.xml` to `http://127.0.0.1:8000/api/`. An Android device can
reach that address only while USB-connected with
`adb reverse tcp:8000 tcp:8000`; a complete Global Networking Android test
also requires a reachable STUN service. Remote HTTP endpoints are
intentionally rejected by the game; production must use HTTPS.

## Environment

| Variable | Default | Purpose |
|---|---|---|
| `MK_DATABASE_URL` | `sqlite:///./minkowski_online.db` | Account/session storage; production Compose uses PostgreSQL. |
| `MK_REDIS_URL` | empty | Live server and join-key storage; empty uses memory for local development/tests. |
| `MK_SESSION_DAYS` | `30` | Session token lifetime. |
| `MK_LISTING_TTL_SECONDS` | `20` | Time before an unpolled host listing expires. |
| `MK_JOIN_TTL_SECONDS` | `45` | Time before an unused rendezvous key expires. |
| `MK_JOIN_MAX_ENTRIES` | `64` | Maximum pending join requests for one server. |
| `MK_JOIN_MAX_PAYLOAD_BYTES` | `4096` | Maximum serialized size of one rendezvous request. |
| `MK_JOIN_MAX_REQUEST_BYTES` | `8192` | Maximum raw rendezvous form size read before parsing. |
| `MK_ALLOWED_GAME_VERSION` | `7` | Published game protocol version. |

Passwords are Argon2id-hashed and session tokens are stored only as SHA-256
hashes. AES join keys are kept only in the short-lived live directory.

Admin browser sessions use a separate session type from game client tokens. The
session schema migration labels existing tokens as game sessions. Changing a
password revokes every current session for that account.

Uvicorn uses forwarded address headers only from the configured
`MK_TRUSTED_PROXY_IPS` (default `172.30.0.2`, Caddy's address on a dedicated
Docker network). Caddy overwrites `X-Forwarded-For`; the API has no published
host port. Keep this trust list limited to actual proxies. The online hostname
must remain DNS-only behind Cloudflare so Caddy observes the connecting
player's address directly. Direct local runs default to trusting only loopback,
as Uvicorn configures by default.
