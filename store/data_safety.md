# Google Play Data Safety Questionnaire — Minkowski Kart

Use the answers below when completing the **Data Safety** section in the
Google Play Console.

---

## Does your app collect or share any of the required user data types?

**Answer: No**

The app does not collect or share any personal or sensitive user data.

---

## Data types checklist

| Category | Data Type | Collected? | Shared? | Notes |
|---|---|---|---|---|
| Location | Approximate / Precise | ❌ No | ❌ No | — |
| Personal info | Name, email, etc. | ❌ No | ❌ No | — |
| Financial info | Purchases, credit card | ❌ No | ❌ No | No IAP |
| Health & Fitness | — | ❌ No | ❌ No | — |
| Messages | — | ❌ No | ❌ No | — |
| Photos & Videos | — | ❌ No | ❌ No | — |
| Audio files | — | ❌ No | ❌ No | — |
| Files & Docs | — | ❌ No | ❌ No | — |
| Calendar | — | ❌ No | ❌ No | — |
| Contacts | — | ❌ No | ❌ No | — |
| App activity | App interactions | ❌ No | ❌ No | — |
| App info & performance | Crash logs | ❌ No | ❌ No | No remote crash reporting |
| Device or other IDs | Device ID, ad ID | ❌ No | ❌ No | — |

---

## Is data encrypted in transit?

N/A — no data is transmitted.

## Can users request that data is deleted?

N/A — no data is collected.

---

## Play Console UI flow

1. Open Play Console → Select app → **Policy** → **App content**
2. Click **Data safety** → **Start**
3. **"Does your app collect or share any of the required user data types?"**
   → Select **No**
4. **"Does your app use encryption when transmitting data?"**
   → Select **Not applicable** (or Yes if using STK online servers)
5. Review & submit

---

## Notes on INTERNET / BLUETOOTH / WRITE_EXTERNAL_STORAGE permissions

These permissions appear in the manifest but are used as follows:

- **INTERNET**: Optional STK online multiplayer only. No data sent to
  developer servers.
- **BLUETOOTH**: Controller pairing only, no data read or collected.
- **WRITE_EXTERNAL_STORAGE**: Save files local to the device only.
- **VIBRATE**: Haptic feedback only.
- **READ_EXTERNAL_STORAGE**: Read user-placed add-on files only.

None of these permissions result in collection or transmission of personal data.
