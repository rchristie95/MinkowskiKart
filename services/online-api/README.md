# MinkowskiKart Online API

This service replaces the SuperTuxKart online directory for the v7
MinkowskiKart protocol. It implements invite-only login, authenticated server
publication, server listing, and the AES rendezvous handoff used by the
existing ENet lobby connection flow.

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
| `MK_ALLOWED_GAME_VERSION` | `7` | Published game protocol version. |

Passwords are Argon2id-hashed and session tokens are stored only as SHA-256
hashes. AES join keys are kept only in the short-lived live directory.
