# Vulkan-on-Android Investigation (Pixel 8 / Mali-G715)

Goal: get the desktop Vulkan graphics pipeline (relativistic warping, PBR
deferred lighting, PCSS shadows, glow, IBL, adaptive tessellation) running on
Android without the driver aborting.

## Status

Working:
- Asset compression (`android/compress_assets.py`): 813 MB -> 330 MB APK.
- Vulkan compiles and links (`android/Android.mk`, `ge_vulkan_features.cpp`).
- `--battle-ai-stats --log=0` runs a full profiling battle at a stable
  33.89 FPS. That run stayed on the **fixed-pipeline / non-PBR** path (glow,
  PCSS, IBL, adaptive tessellation are all gated behind
  `UserConfigParams::m_dynamic_lights`, see below), so it did not exercise the
  advanced shader tree at all.

Broken:
- A real race (`--track=gran_paradiso_island --numkarts=4 --race-now`) with
  the advanced pipeline active renders exactly one frame, then:
  ```
  E MinkowskiKart: [error] main: Exception caught : vkQueueSubmit failed.
  E MinkowskiKart: [error] main: Aborting MinkowskiKart.
  ```
  thrown from `GEVulkanDriver::endScene()` in
  [ge_vulkan_driver.cpp:1872](lib/graphics_engine/src/ge_vulkan_driver.cpp:1872).

## Fix landed this session: the VkResult was never visible

`ge_vulkan_driver.cpp` logged the failing `VkResult` via plain `printf()`.
On Android, STK's `Log::` class writes to logcat through
`__android_log_print(..., "MinkowskiKart", ...)`
([log.cpp:265](src/utils/log.cpp:265)), but raw `printf()`/stdout is **not**
wired to logcat at all in this app (no `dup2`/pipe redirection exists
anywhere in `src/` or `android/`). That's why the handoff's log excerpt had
the "Exception caught" line (which *does* go through `Log::error`) but no
VkResult.

Fixed in [ge_vulkan_driver.cpp](lib/graphics_engine/src/ge_vulkan_driver.cpp):
the `vkQueueSubmit`/`vkQueuePresentKHR`/fence-timeout `printf`s now go through
`__android_log_print(ANDROID_LOG_ERROR, "MinkowskiKart", ...)` on Android, and
the `vkQueueSubmit` failure additionally folds the `VkResult` into the thrown
exception's message, so it shows up in the *existing* "Exception caught : ..."
line without needing a second logcat filter. **Rebuild and rerun** — the next
crash log will read something like
`Exception caught : vkQueueSubmit failed with VkResult -4.` (VK_ERROR_DEVICE_LOST)
or `-2` (VK_ERROR_OUT_OF_DEVICE_MEMORY), etc. That number is the single most
useful next data point and doesn't require any bisection.

## Important correction to the "disable render features" isolation plan

The originally suggested flags
(`--disable-glow --disable-bloom --disable-pcss --shadows=0 --disable-ibl`)
**will not fully isolate this**, for two reasons found while reading the
config plumbing in
[irr_driver.cpp:567-618](src/graphics/irr_driver.cpp:567):

1. **`--disable-bloom` is a no-op on Vulkan.** Bloom only exists in the
   OpenGL/SP `PostProcessing` pipeline; `GE::GEConfig`
   ([ge_main.hpp:45-87](lib/graphics_engine/include/ge_main.hpp:45)) has no
   bloom field at all. Drop it from the test matrix.

2. **Relativity, and everything it forces on, cannot be turned off by any
   existing flag.** `Relativity::isEnabled()`
   ([relativity_math.cpp:350](src/relativity/relativity_math.cpp:350))
   unconditionally `return true;` — MinkowskiKart always runs with
   relativistic warping active. That means, regardless of
   `--disable-*`/`--shadows=0`, `irr_driver.cpp` always sets:
   - `m_force_displace_compose = true`
   - `m_disable_frustum_culling = true`
   - `m_adaptive_tessellation = true` (on every platform except Apple/MoltenVK,
     which is explicitly carved out — see below)

   So a test run with all five original flags set will still build the
   displace-compose pass and the adaptive-tessellation "_tess" pipeline
   variants ([ge_vulkan_draw_call.cpp:941](lib/graphics_engine/src/ge_vulkan_draw_call.cpp:941)),
   and can still crash for the exact same reason.

### Leading hypothesis: adaptive tessellation on a TBDR mobile GPU

The existing `#if defined(__APPLE__)` carve-out in `irr_driver.cpp` says,
verbatim: *"MoltenVK (EDT_VULKAN) emulates tessellation with an expensive
per-draw compute pre-pass... crawled at ~0.3 fps"* — i.e. the previous session
already found tessellation to be risky on Apple's TBDR (tile-based deferred
rendering) GPUs and special-cased it. **Mali-G715 is also a TBDR
architecture.** No equivalent carve-out exists for Android. If Mali's driver
reports `tessellationShader = VK_TRUE` (common on modern Mali/Valhall), the
game will build and dispatch the same tessellation control/evaluation shaders
that were only ever validated on desktop GPUs and (separately) ported to MSL
for Metal — never tuned or driver-tested for Mali. A `vkQueueSubmit` failure
(as opposed to a slow-but-working frame) is consistent with a Mali tessellation
driver bug or a limit violation (e.g. `maxTessellationGenerationLevel`,
patch-control-point count) rather than a performance issue.

This is a hypothesis, not a confirmed root cause — there's no way to query the
device's actual `VkPhysicalDeviceFeatures`/limits or watch `adb logcat` from
this environment (no `adb` in `PATH` here, no attached device). It needs to be
tested on-device.

### New debug flag added this session to make this testable

There was previously no way to disable adaptive tessellation independently of
everything else. Added:
- `UserConfigParams::m_vk_debug_no_tess` (hidden bool config,
  [user_config.hpp](src/config/user_config.hpp)), mirroring the existing
  `m_vk_debug_ao` pattern.
- CLI flag `--disable-vulkan-tess` (parsed in
  [main.cpp](src/main.cpp) next to `--enable-vulkan-ao`).
- Wired into `GE::getGEConfig()->m_adaptive_tessellation` in
  [irr_driver.cpp](src/graphics/irr_driver.cpp:600).

## Recommended next steps (in priority order)

1. **Rebuild, rerun the crashing race, capture logcat.** The exception
   message now contains the VkResult — this alone may point straight at the
   cause (e.g. `VK_ERROR_DEVICE_LOST` implicates a shader/driver bug;
   `VK_ERROR_OUT_OF_DEVICE_MEMORY` implicates the compressed-but-still-large
   render targets/shadow maps).

2. **Single biggest lever — try `--disable-vulkan-tess` alone first**
   (new flag, see above). This isolates the one hypothesis with the strongest
   prior (existing MoltenVK carve-out for the same GPU class).
   ```
   adb shell am start -n org.supertuxkart.stk_dbg/org.supertuxkart.stk_dbg.SuperTuxKartActivity \
     -e args "--track=gran_paradiso_island --numkarts=4 --race-now --disable-vulkan-tess"
   ```
   (Package/activity above read from the current debug manifest at
   `android/build/.../AndroidManifest.xml`; adjust if signing a different
   variant. Note `am start` needs the args bundled into the single `-e args
   "..."` extra — `SuperTuxKartActivity.getArguments()`
   ([SuperTuxKartActivity.java:304-330](android/src/main/java/SuperTuxKartActivity.java:304))
   reads that one string extra and splits it on spaces/quotes; it does not
   read bare shell argv.)

3. **If step 2 doesn't fix it**, use `--disable-dynamic-lights` as the next,
   broader cut — it forces `m_pbr`, `m_shadow_map_size` (so PCSS too), and
   `m_ssao` all off in one shot
   ([irr_driver.cpp:572-617](src/graphics/irr_driver.cpp:572)), leaving only
   the relativity-driven displace-compose/frustum-culling pieces on. Combine
   with `--disable-vulkan-tess` to also rule out tessellation. If *that*
   combination still crashes, the displace-compose pass itself
   (`m_force_displace_compose`, always on) becomes the prime suspect instead.

4. **Corrected feature matrix** if finer bisection is still needed (dropping
   the no-op `--disable-bloom`):
   `--disable-glow`, `--disable-pcss`, `--shadows=0`, `--disable-ibl`,
   `--disable-dynamic-lights`, `--disable-vulkan-tess` — toggle one at a time
   from the fully-disabled state, re-enabling each individually to find which
   single feature reintroduces the crash.

5. Once the trigger feature is identified, the fix is almost certainly
   inside that feature's Vulkan pipeline/shader path specifically for Mali —
   not a general Vulkan setup bug, since desktop Vulkan and the fixed-pipeline
   Android path both work.
