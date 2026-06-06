# OVH DNS Records

In OVH Control Panel, open:

`Web Cloud` -> `Domain names` -> `robsonchristie.com` -> `DNS zone`

Add these records, replacing `YOUR_VPS_IPV4` with the VPS public IPv4:

| Type | Subdomain | Target |
| --- | --- | --- |
| `A` | `@` | `YOUR_VPS_IPV4` |
| `A` | `www` | `YOUR_VPS_IPV4` |
| `A` | `online` | `YOUR_VPS_IPV4` |
| `A` | `minkowskikart` | `YOUR_VPS_IPV4` |
| `A` | `play` | `YOUR_VPS_IPV4` |

`robsonchristie.com` and `www.robsonchristie.com` are the personal homepage.
`online.robsonchristie.com` is the HTTPS matchmaking API.
`minkowskikart.robsonchristie.com` is the public game website.
`play.robsonchristie.com:2759` is the friendly address for direct UDP gameplay.

Do not delete the existing MX records unless you intentionally want to disable
the included Zimbra email account.
