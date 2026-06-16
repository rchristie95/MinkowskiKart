# Putting the websites behind Cloudflare (campus-network fix)

Some networks (e.g. university firewalls) inject TCP resets for HTTPS to this
VPS's raw OVH IP, so `robsonchristie.com` shows `ERR_CONNECTION_RESET` even
though the server is healthy worldwide. Fronting the **static sites** with
Cloudflare makes browsers connect to Cloudflare's IPs instead, which those
firewalls don't block.

## What is and isn't proxied

Cloudflare's free plan only proxies HTTP/HTTPS. The **game is left untouched**:
gameplay is UDP, which Cloudflare free cannot proxy, so it stays direct.

| Hostname | Purpose | Protocol | Cloudflare |
| --- | --- | --- | --- |
| `robsonchristie.com`, `www` | personal homepage | HTTPS | **Proxied** (orange) |
| `minkowskikart.robsonchristie.com` | game website | HTTPS | **Proxied** (orange) |
| `online.robsonchristie.com` | matchmaking API | HTTPS | **DNS only** (grey) |
| `play.robsonchristie.com` | gameplay | UDP 2759 | **DNS only** (grey) |

> Online multiplayer is **unchanged** by this. It is also **not** made reachable
> from a blocking campus network — the gameplay UDP still goes to the blocked
> OVH IP (free Cloudflare can't proxy UDP; that needs paid Cloudflare Spectrum).
> This change only fixes the **websites**.

## Steps

### 1. Add the domain to Cloudflare
1. Create/log in to a Cloudflare account, **Add a site** → `robsonchristie.com`,
   Free plan.
2. Cloudflare imports existing DNS. Verify/set these records (Content =
   `51.195.235.177`):

   | Type | Name | Content | Proxy |
   | --- | --- | --- | --- |
   | A | `@` | `51.195.235.177` | Proxied |
   | A | `www` | `51.195.235.177` | Proxied |
   | A | `minkowskikart` | `51.195.235.177` | Proxied |
   | A | `online` | `51.195.235.177` | **DNS only** |
   | A | `play` | `51.195.235.177` | **DNS only** |

3. **Re-create the MX / email records** (the Zimbra mail). Keep all mail records
   **DNS only**. Do not lose these when switching nameservers.

### 2. Point OVH at Cloudflare
In the OVH Manager, set `robsonchristie.com`'s nameservers to the two Cloudflare
nameservers shown in step 1. Propagation can take up to a few hours.

### 3. SSL/TLS mode + Origin Certificate
1. Cloudflare → **SSL/TLS** → **Overview** → set mode to **Full (strict)**.
2. Cloudflare → **SSL/TLS** → **Origin Server** → **Create Certificate**:
   - Hostnames: `robsonchristie.com`, `*.robsonchristie.com`
   - Type: RSA or ECDSA, format PEM.
   - Copy the **certificate** and the **private key**.
3. On the VPS, save them (NOT in git — the key is secret):
   ```bash
   cd ~/apps/MinkowskiKart/deploy/online
   mkdir -p certs && chmod 700 certs
   nano certs/cf-origin.pem   # paste the Origin Certificate
   nano certs/cf-origin.key   # paste the Private Key
   chmod 600 certs/cf-origin.*
   ```
4. (Recommended) Cloudflare → **SSL/TLS** → **Edge Certificates** → enable
   **Always Use HTTPS** and **Automatic HTTPS Rewrites**.

### 4. Switch Caddy to the Cloudflare variant
The repo ships `Caddyfile.cloudflare` (origin-cert `tls` directives for the
proxied sites; the API keeps automatic Let's Encrypt because it's DNS-only).
Mount the certs and point Caddy at the variant.

`docker-compose.yml` — caddy service, add the certs mount and use the variant:
```yaml
  caddy:
    ...
    volumes:
      - ./Caddyfile.cloudflare:/etc/caddy/Caddyfile:ro   # was ./Caddyfile
      - ./site/robsonchristie:/srv/robsonchristie:ro
      - ./site/minkowskikart:/srv/minkowskikart:ro
      - ./certs:/certs:ro                                 # NEW
      - caddy_data:/data
      - caddy_config:/config
```

Then:
```bash
docker compose up -d --force-recreate caddy
docker compose logs --tail=40 caddy        # no cert/config errors
curl -I https://robsonchristie.com         # 200 via Cloudflare (cf-ray header)
```

## Verify
- `curl -sI https://robsonchristie.com | grep -i cf-ray` → a `cf-ray` header means
  it's served through Cloudflare. From a previously-blocked campus network it now
  loads.
- `curl -sI https://online.robsonchristie.com/healthz` → still direct, 200.
- Online play (UDP 2759 to `play.`/the VPS IP) is unchanged.

## Rollback
Point the caddy volume back to `./Caddyfile`, drop the `./certs` mount, set the
Cloudflare records back to **DNS only**, and `docker compose up -d
--force-recreate caddy`.

## Notes
- `deploy/online/certs/` holds a **private key** — keep it out of git (see the
  `.gitignore` entry added alongside this doc).
- The origin certificate is valid for up to 15 years and does not need renewal,
  unlike the Let's Encrypt certs Caddy still manages for the DNS-only API host.
