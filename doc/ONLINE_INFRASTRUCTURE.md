# MinkowskiKart Online Infrastructure

## Implemented Architecture

Online play continues to use the game's ENet peer/server gameplay protocol.
The new service is the control plane: it signs invited players in, lists
player-hosted rooms, and securely passes short-lived AES connection details
from a joining player to a host.

```mermaid
flowchart LR
    C["Desktop or Android client"] -->|"HTTPS form/XML API"| A["FastAPI control plane"]
    H["Player-hosted game"] -->|"publish and poll"| A
    A --> P["PostgreSQL accounts and sessions"]
    A --> R["Redis listings and join keys"]
    C -->|"STUN lookup"| S["SuperTuxKart public STUN pool"]
    C <-->|"ENet UDP gameplay"| H
    C <-->|"fallback ENet UDP gameplay"| O["Official headless server"]
```

Every invited account may host one active public listing. The official server
exists as a reliable public fallback for players whose home networks cannot
accept the existing UDP hole-punch path. TURN relay is not included in this
first version.

## Test With Two Android Devices

Do not use an APK produced before the protocol-v7 changes. Build a fresh APK,
then install the same build on both devices.

For the first smoke test, put both devices on the same Wi-Fi network. On the
first device choose `Online` -> `Local Network` -> `Create Server`; on the
second choose `Online` -> `Local Network` -> `Find Server` and join the room.
This verifies Android packaging, lobby protocol v7, gameplay traffic, and
server-authoritative relativity rules without requiring the API or STUN.

For the complete owned-infrastructure test, first deploy the HTTPS API below,
replace its `minkowskikart.example` placeholders, and rebuild the APK. The
default build retains SuperTuxKart's public STUN discovery service. Create two
invited accounts. Sign device A in with the first account and select `Global
Networking` -> `Create Server`; sign device B in with the second account,
select `Global Networking` -> `Find Server`, and join A's listing. Run this
once on the same Wi-Fi and once with device B on mobile data to exercise the
WAN rendezvous and NAT path.

For API-only local development, a USB-connected phone can access a PC-hosted
API configured as `http://127.0.0.1:8000/api/` by running:

```powershell
$adb = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
& $adb reverse tcp:8000 tcp:8000
```

Repeat the reversal once for each connected device. This does not replace
STUN and therefore is not by itself a complete Global Networking test.

## Game Changes

- The wire protocol is version `7` and requires `minkowski_rules_v1`, keeping
  modified clients and servers separate from SuperTuxKart public servers.
- Hosts publish authoritative `relativity-normal-c-light` and
  `relativity-max-beta` values; clients apply those values during online play.
- Physics speed limits now use each kart's active time-dilation or warp-bubble
  target. The smooth local `c` transition remains a rendering effect only.
- HTTPS is accepted for online requests; unencrypted HTTP is permitted only
  for `localhost` and `127.0.0.1` development.
- The SuperTuxKart add-on catalog and STUN pool remain enabled. Add-ons are
  compatible content downloads and STUN is protocol-independent; neither
  requires using SuperTuxKart's multiplayer directory.
- News-controlled endpoint redirects are disabled so the add-on feed cannot
  replace the version-7 multiplayer API URL.
- Friends, public achievements tabs, and email changing are hidden for v1.
  Login, saved sessions, password changes, hosting, listing, and joining are
  backed by the new API.

## SuperTuxKart Service Boundary

Keep `AddonServer` on `https://online.supertuxkart.net/dl/xml` so community
tracks, karts, and arenas remain visible. Keep STUN on SuperTuxKart's public
SRV records unless owning that small operational dependency becomes important.

Do not point `OnlineServer` back at SuperTuxKart's public multiplayer API for
distributed builds. This fork requires gameplay protocol `7` and capability
`minkowski_rules_v1`; normal SuperTuxKart clients and servers do not share
that handshake. Use this fork's API for global rooms, or use Local Network
play while testing without an API deployment.

## Deploy On One VPS

Requirements: a Linux VPS with Docker Compose, a domain you control, TCP
ports `80` and `443`, and UDP port `2759` open in its firewall. Ports for
coturn are only required if you choose to replace the upstream STUN pool.

1. Choose a hostname, for example `online.yourdomain.com`.
2. Create DNS `A`/`AAAA` records for that hostname pointing at the VPS.
3. Replace the `online.minkowskikart.example` API-related placeholders in
   `data/stk_config.xml` with your domain before producing desktop or Android
   builds.
4. On the VPS, copy `deploy/online/.env.example` to `deploy/online/.env`,
   set `MK_ONLINE_DOMAIN`, generate a strong PostgreSQL password, and fill in
   the official host credentials after creating that account.
5. Start the database and API first:

```bash
cd deploy/online
docker compose up -d postgres redis api caddy
docker compose exec api python -m app.admin create-user --username official-host --official-host
docker compose exec api python -m app.admin create-user --username your-player-name
```

6. Set the official account password in `.env`, then start the public fallback:

```bash
docker compose up -d --build official-server
docker compose ps
curl "https://online.yourdomain.com/healthz"
```

7. Build and distribute protocol-v7 desktop and Android clients only after the
   DNS and HTTPS endpoint are live.

## Invite And Hosting Operations

Create one account per invited player:

```bash
docker compose exec api python -m app.admin create-user --username alice
```

The command securely prompts for a password. Provide that username and
password to the invited player through your chosen private channel. A signed
in invited player can choose Global Networking and host from the game UI; the
listing automatically expires within 20 seconds when their host stops polling.

To tune the official fallback server, edit
`deploy/online/official_server_config.xml`. These two settings are sent to all
connected clients and are authoritative for online physics:

```xml
<relativity-normal-c-light value="35" />
<relativity-max-beta value="0.98" />
```

## Security And Limits

- Keep `.env` off source control and back up the PostgreSQL volume.
- Caddy obtains and renews TLS certificates automatically once DNS resolves.
- The API never logs passwords, session tokens, or AES rendezvous material.
- The default client uses SuperTuxKart's public STUN discovery pool. You can
  deploy the included coturn service and change the STUN configuration later
  if you need complete operational ownership.
- STUN helps most home-hosted games traverse NAT; users behind restrictive or
  symmetric NAT should use the official fallback server or configure UDP port
  forwarding. A later phase can add TURN relay or dedicated match instances.
- The public SuperTuxKart add-on/news feed is intentionally retained.
  Rankings, email recovery, and public self-registration are not part of this
  initial owned multiplayer infrastructure.
