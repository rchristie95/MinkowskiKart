# Keystore — IMPORTANT SECURITY NOTES

## Keystore File

`minkowski-kart-release.keystore`

**Alias:** `minkowskikart`
**Store password:** `minkowski2024!`
**Key password:** `minkowski2024!`
**Validity:** 10,000 days (~27 years)
**Algorithm:** RSA 2048-bit, SHA384withRSA

---

## ⚠️ CRITICAL: Do NOT lose this keystore

Google Play permanently ties your app to this signing key. If you lose the
keystore, you **cannot** update your app on the Play Store. You would need
to publish it as a brand new app.

**Back this file up to at least two separate locations:**
- Cloud storage (e.g. Google Drive, OneDrive — NOT the same as your repo)
- USB drive or offline backup

---

## ⚠️ Do NOT commit to Git

This keystore must **never** be committed to your public GitHub repository.
The `.gitignore` for the `store/keystore/` folder has been configured
to exclude the `.keystore` file.

---

## Using with the build script

See `build-release.ps1` in the repo root. It reads these credentials
automatically from the `KEYSTORE_*` environment variables that are preset
in that script.

---

## Google Play App Signing (optional)

You can enroll in **Google Play App Signing** in the Play Console, which
lets Google manage the final signing key while you sign with an upload key.
In that case, this keystore acts as your **upload key** — losing it only
means you need to contact Google support to reset it, and your users are
unaffected.
