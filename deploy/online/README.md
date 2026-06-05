# Production VPS Deployment

This stack runs:

- one persistent public MinkowskiKart server on UDP port 2759;
- the HTTPS matchmaking and rendezvous API;
- a static MinkowskiKart website;
- PostgreSQL, Redis, and Caddy;
- optional owned STUN on port 3478 using the `owned-stun` profile.

## Required Before Deployment

- A Debian 13 VPS with a public IPv4 address.
- DNS records for `online.robsonchristie.com` and
  `minkowskikart.robsonchristie.com` pointing at the VPS.
- TCP ports 22, 80, and 443 plus UDP port 2759 open.
- This repository revision, including `stk-assets`, available on the VPS.

The official server image injects the production HTTPS endpoint at build time.
Desktop and Android clients must also be rebuilt with the same endpoint in
`data/stk_config.xml`.

## 1. Bootstrap The VPS

OVH creates the initial account from the installed OS. For Debian it is often
`debian`, but check the OVH access email or VPS dashboard console if that user
does not work.

```bash
ssh debian@51.195.235.177
cd /path/to/MinkowskiKart
chmod +x deploy/online/bootstrap-debian.sh
./deploy/online/bootstrap-debian.sh
exit
```

Reconnect after the script finishes:

```bash
ssh debian@51.195.235.177
```

If SSH on port 22 times out from your local network, use the OVH web console
to run the same bootstrap commands, or reconnect from a network that permits
outbound SSH.

## 2. Configure DNS And Secrets

Create these records in the OVH DNS zone for `robsonchristie.com`:

| Type | Subdomain | Target |
| --- | --- | --- |
| `A` | `online` | VPS IPv4 |
| `A` | `minkowskikart` | VPS IPv4 |
| `A` | `play` | VPS IPv4 |

Wait until this returns the VPS IPv4:

```bash
getent ahostsv4 online.robsonchristie.com
getent ahostsv4 minkowskikart.robsonchristie.com
```

Then configure the stack:

```bash
cd /path/to/MinkowskiKart/deploy/online
cp .env.example .env
chmod 600 .env
openssl rand -hex 32
nano .env
```

Set `MK_ONLINE_DOMAIN=online.robsonchristie.com` and
`MK_SITE_DOMAIN=minkowskikart.robsonchristie.com`, paste the generated value as
`POSTGRES_PASSWORD`, and choose an official-host password between 8 and 60
characters.

## 3. Start The Matchmaker

```bash
docker compose up -d --build postgres redis api caddy
docker compose ps
curl --fail "https://$(sed -n 's/^MK_ONLINE_DOMAIN=//p' .env)/healthz"
```

Create the official host account. Enter the same official-host password that
you placed in `.env`:

```bash
docker compose exec api python -m app.admin create-user \
  --username official-host --official-host
```

Create additional invited player accounts with:

```bash
docker compose exec api python -m app.admin create-user --username PLAYER_NAME
```

## 4. Start The Persistent Race

Building the server image compiles the headless Linux server and copies the
track and kart assets, so the first build can take several minutes:

```bash
docker compose up -d --build official-server
docker compose ps
docker compose logs --tail=100 official-server
```

The server is ready when its log says it is online. The default room uses
`mobius_track`, three bots, and starts when one human joins. Change those
values in `.env`, then recreate the service:

```bash
docker compose up -d --force-recreate official-server
```

## Operations

```bash
# Service status
docker compose ps

# Follow logs
docker compose logs -f api caddy official-server

# Pull/build a new repository revision
git pull
docker compose up -d --build

# Stop the stack
docker compose down

# Back up the database
docker compose exec -T postgres pg_dump -U minkowski minkowski \
  | gzip > "minkowski-$(date +%F).sql.gz"
```

Do not run `docker compose down -v` in production; `-v` deletes the database
and other persistent volumes.
