# Security findings remediation

The 21 entries in `security-findings.csv` were reviewed against revision
`fb723b717e8614384e0d19c8c2471453533daaa2`. The changes below address the
findings; deployment and compatibility requirements are listed separately.

| Finding (occurrence suffix) | Patch |
| --- | --- |
| `20ba72e8c103d29525da3ea7` Handshake memcpy | Check declared encrypted length against remaining packet bytes before copying. |
| `ff77b33df781fefe24ed9335` Packet format strings | Log packet content through a literal `%s` format. |
| `0591e99ec08dcacb9658b80e` Chat URL shell execution | Pass validated HTTP(S) URLs as a single process argument with no shell. |
| `367efd019c1dbdc76c1d460d` Android PR signing secrets | PR/feature builds use debug signing; production secrets are gated to trusted refs and protected environment selection. Verify signing commits belong to main/master history. |
| `5731d896d2285b8c4b9b13f3` Client-origin authoritative events | Server accepts only the client startup-boost opcode; authoritative events are rejected. |
| `591676040e3140925735ce44` ZIP traversal | Validate retained archive paths before output, reject absolute/traversal/device paths, and reject existing links/reparse points in destination components. |
| `5d3231395ff8b5f29fee18ae` Unbounded Redis joins | Bound each server queue to 64 entries, deduplicate users, cap serialized entries at 4 KiB and request bodies at 8 KiB. |
| `68a7c9971bb2c89a6a89afdc` TLS verification disabled | Enable certificate and hostname verification with the configured CA bundle; reject HTTPS downgrade redirects and do not redirect credential-bearing API forms. |
| `7fd373b18105a2e0b9571a6e` Short CryptoKit packets | Reject packets without a complete counter, authentication tag, and protocol byte in all crypto backends. |
| `96066e45021e04431ba783b8` Unbounded HTTP responses | Enforce 8 MiB memory, 1 GiB ordinary download, and 4 GiB mobile bundle budgets, including chunked responses; discard failed partial results. |
| `c570ab0b7b99b113ca257284` Public registration | Disable public account creation and retain operator-managed provisioning. |
| `ccd1f208161cc2a9f15b646c` Admin HTML injection | Escape request/account fields, remove account interpolation from JavaScript, and validate email syntax. |
| `d6604fa8ab66971e7a14ba67` Add-on path identifiers | Require bounded ASCII identifiers and refuse invalid entries at loading, installation, and removal. |
| `efc012d21f10ed44b1af1fb2` Game tokens accepted by admin | Separate game/admin session types and require the corresponding scope on each authentication path. |
| `43126c98b764f18773617ec3` Arbitrary rendezvous UDP targets | Derive IP from the observed client address; preserve the decimal IPv4 wire format and validate port. Trust proxy headers only through configured Uvicorn proxy peers. |
| `4e2ef7c8a009bfddff6a0a6a` Password changes retain sessions | Revoke all sessions when the password changes. |
| `bf0096cedbcadf5c9c217311` Mutable release inputs | Pin actions by commit and downloaded build inputs by checked-in SHA-256; isolate write permissions to trusted publishing jobs and include the input lock in cache keys. |
| `cc59dacbec8bbae75752887d` Unauthenticated discovery redirects | Echo a fresh 128-bit client nonce in direct discovery; authenticate Aloha with a separate AES-GCM key derived from rendezvous credentials. |
| `e2d2f0142bbabfacf8c417b5` Redis cleanup races | Filter rendezvous lists in atomic Lua scripts while preserving remaining entries and TTL. |
| `f022714390e8b1d5a30dadee` ZIP expansion/count limits | Add-on limits: 10,000 entries, 1 GiB expanded total, 256 MiB/file. Mobile limits: 50,000 entries, 8 GiB total, 1 GiB/file. Preflight before writing and bound file copying. |
| `f57641f02427b7edc50f9d2d` Insecure admin cookie | Mark the browser session cookie Secure, HttpOnly, and SameSite=Lax. |

## Deployment and compatibility

- Run `alembic upgrade head` before deploying the online API. Existing sessions
  become game sessions; administrators must sign in again in the browser.
  The new migration tolerates an already-reconciled session column.
- The API uses the validated client address supplied by Uvicorn. The Compose
  configuration gives Caddy a fixed address on a dedicated API network; keep
  `MK_TRUSTED_PROXY_IPS` restricted to the actual proxy and keep the API port
  private. Caddy overwrites incoming `X-Forwarded-For`.
- Create the `android-signing` GitHub environment with branch/tag restrictions
  and required review. Move the four `ANDROID_UPLOAD_*` secrets from repository
  scope into that environment, and remove the repository-scoped copies.
  `android-debug` must contain no production signing secrets. Environment
  protection is configured in GitHub, not by these local files.
- Updated clients reject legacy unauthenticated discovery/Aloha replies.
  Updated servers retain replies for old clients. New clients can fall back
  to DNS port discovery on old servers; old servers without that mechanism,
  and legacy direct-only Aloha redirection, require a server update.
- Build input hashes deliberately fail closed if upstream replaces an archive.
  Review upstream changes before updating `.github/build-inputs.json`.
  `tools/ci/lock_build_inputs.py` is a maintainer helper, never a CI step.

## Verification

- Windows development build succeeded with LLVM/MinGW and the bundled dependencies.
- Game unit testing exited 0 and reported success, including runtime checks for
  handshake bounds, opcode direction, crypto truncation, nonce discovery, and
  authenticated Aloha (also active when `NDEBUG` is set).
- Online API: 14 tests passed, including actual Redis Lua execution through
  `fakeredis[lua]`/Lupa, concurrency/TTL checks, request-size limits, forged proxy
  headers, scoped sessions, password revocation, HTML escaping, and migration.
- Native HTTP response-buffer and archive/path/identifier harnesses passed.
- Five verified-download tests passed (valid data, tampering, truncation,
  oversize, and insecure redirect).
- `actionlint` passed for the modified Android, Apple, and Windows workflows;
  `git diff --check` passed.
- POSIX symlink and URL process tests are wired into the Ubuntu Windows-build
  matrix leg. This Windows host did not execute those branches, Apple code,
  Android builds, a live Redis instance, or Docker Compose deployment.
