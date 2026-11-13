# Steam Deck Port, Stage 2 (voxel 3D world and 3D battles on Mesa desktop GL) Implementation Plan

**Status: DRAFT, refine after probe.** The probe report `.superpowers/sdd/2026-11-13-deck-stage1-boot/stage2-probe-report.md` did not exist when this draft was written. Every step marked PROBE-DEPENDENT must be filled in from that report before it is executed. Nothing in a PROBE-DEPENDENT step may be guessed.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** On the Steam Deck the 3D voxel overworld (sun, shadows, depth-of-field diorama) and the 3D wild battle render correctly through Mesa desktop OpenGL, with no GL errors and a steady frame time, with 30, 40 and 60 FPS options.

**Architecture:** Stage 1 already compiles the whole voxel renderer and runs it on Mesa through `src/platform/deck/compat/vitaGL.h`, with 3D switched off by `userdata/settings.txt`. Stage 2 turns it on, adds a GL error logger to find what desktop GL rejects, fixes only what the logger and the probe show, makes the off-screen passes and the letterbox agree, and then adds pacing and measurement. Vita code stays behind `PLATFORM_VITA` / `PLATFORM_DECK` exactly as in stage 1 (the Deck build defines both).

**Tech Stack:** C (gnu99), CMake, GCC `-m32 -O3 -march=znver2`, SDL2 2.32.10 static, Mesa desktop GL 2.1 compatibility context, distrobox container `emerald-build`, SSH to the Deck.

**Spec:** `docs/superpowers/specs/2026-11-13-steam-deck-port-design.md` (stage 2 exit test: walk the overworld with sun and shadows, enter a wild battle in 3D, no GL errors, steady frame time; section 4 performance plan).

## Unverified assumptions

Each of these is read from the code or inferred, and not yet proven on the Deck. A step that depends on one says so.

1. `glGenFramebuffers`, `glFramebufferTexture2D`, `glRenderbufferStorage` (`voxel_renderer.c:3629-3653`) resolve and work in the SDL-created 2.1 compatibility context (ARB/EXT framebuffer object in Mesa). They link in stage 1, but nothing has executed them. The probe report is the proof.
2. `SDL_GL_SetSwapInterval(2)` is accepted by Mesa on the Deck's session (X11 or gamescope). The code asks for interval 2 for 30 FPS (`sdl2.c:836`) and never checks the return value (`deck_video.c:DeckVideo_SetSwapInterval`).
3. The Deck panel refreshes at 60 Hz by default and 40 Hz in the SteamOS 40 Hz mode, and the game's 60 Hz clock cannot be locked to 40 Hz without uneven frame spacing. Whether 40 FPS looks acceptable is an experiment (Task 5), not a given.
4. GL texture uploads with `GL_RGBA` + `GL_UNSIGNED_SHORT_5_5_5_1` and `GL_UNSIGNED_SHORT_4_4_4_4` produce the same colours as vitaGL (same packed-component order). Checked only by eye in Task 2.
5. A saved game from stage 1 exists on the Deck and "Continue" reaches the overworld, so the smoke and manual tests can enter 3D without a scripted input path.
6. The unverified Vita-only behaviour "the GPU never reads a texture being written" (rings and spares) is harmless on desktop GL. Not measured.

## Global Constraints

- Target: Linux x86 32-bit (`-m32`), run on the Steam Deck. All work and files on the Deck live under `/run/media/deck/SD512/emerald-deck/`. The Deck is reached with `ssh deck`.
- Code is written in this repo at `F:\emerald-deck` and pushed with git to the bare repo on the Deck: `git push deck deck-port:main`, then `ssh deck 'git -C /run/media/deck/SD512/emerald-deck/src pull'`. Nothing goes to GitHub unless Marcos asks. Do not touch the Deck by any other route than `ssh deck` commands written in a step.
- Keep the Vita build working: every change sits behind `PLATFORM_DECK` or in files under `src/platform/deck/`. Vita-only calls in shared files are not removed.
- Never guess an error cause. For any GL error: read the error code, find the call that raised it (Task 1 bisect), read the code, then the docs in `docs/` (`RENDERING.md`, `VOXEL_ENGINE.md`, `THREADS.md`, `DECK_NOTES.md`). Per the project rule, grep the exact error text in `docs/` first.
- Frame-time claims are measured on the Deck with the telemetry from Task 6, never estimated.
- Agents: Sonnet for Tasks 1 to 5 and 9, Haiku for sub-steps marked "Haiku" (commands only). At most 30 tool calls and 3 retries per agent, report in 40 lines or fewer.

## Commands used throughout

Run a command in the container on the Deck from this PC (same as stage 1, written `DECK_RUN '<command>'`):

```bash
ssh deck "distrobox enter emerald-build -- bash -lc 'cd /run/media/deck/SD512/emerald-deck/src && <command>'"
```

Run the game with a window from this PC (Desktop Mode is needed for `DISPLAY=:0`). Written `DECK_PLAY '<env> <args>'`:

```bash
ssh deck "distrobox enter emerald-build -- bash -lc 'cd /run/media/deck/SD512/emerald-deck/src/build-deck/dist && DISPLAY=:0 XAUTHORITY=\$(ls /run/user/1000/xauth_*) XDG_RUNTIME_DIR=/run/user/1000 <env> ./run.sh <args>'"
```

Build and sync after a change:

```bash
git push deck deck-port:main
ssh deck 'git -C /run/media/deck/SD512/emerald-deck/src pull'
DECK_RUN 'cmake --build build-deck -j8 2>&1 | tail -20'
```

Screenshot on the Deck host (not in the container): `ssh deck 'spectacle -b -n -f -o /run/media/deck/SD512/emerald-deck/shots/<name>.png'`, then `scp deck:/run/media/deck/SD512/emerald-deck/shots/<name>.png .superpowers/sdd/2026-11-13-deck-stage2/`.

---

### Task 1: Turn 3D on and add the GL error logger

**Agent:** Sonnet

**Files:**
- Modify: `tools/deck/settings.txt`
- Modify: `src/platform/deck/deck_video.c` (`DeckVideo_Swap`, new `DeckGl_CheckErrors`)
- Modify: `src/platform/deck/deck_video.h`

**Interfaces:**
- Produces: `void DeckGl_CheckErrors(const char *where)` in `deck_video.c`. When the environment variable `DECK_GL_DEBUG` is set to anything but `0` at `DeckVideo_Init`, it drains `glGetError()` and, for each error, prints `GLERR frame=<n> where=<where> code=0x<hex> (<name>)` to stderr and appends the same line to `userdata/gl_errors.log`. Each distinct `(where, code)` pair is printed at most 20 times, then counted. When the variable is unset it is a single branch and returns.
- Produces: `DeckVideo_Swap` calls `DeckGl_CheckErrors("swap")` before `SDL_GL_SwapWindow`. At exit (`VitaPlatform_Exit`) nothing extra is required.
- Consumes: the settings keys `voxel_3d`, `voxel_battle`, `voxel_blur`, `voxel_fps` read by `VitaExtras_Init` (`src/platform/vita/vita_extras.c:94-98, 342-343`).

- [ ] **Step 1: Read what the settings file accepts**

Run: `grep -n "voxel_" src/platform/vita/vita_extras.c | head -30`
Expected: `voxel_3d`, `voxel_battle`, `voxel_blur` (on/off, default on, line 274-287) and `voxel_fps` (maps to `PLATFORM_SETTING_VOXEL_FPS`: 0 = 30 FPS locked, 1 = up to 60, `include/platform.h:41`).

- [ ] **Step 2: Enable 3D in the dist settings, battle still off**

Edit `tools/deck/settings.txt` to exactly:

```
voxel_3d=1
voxel_battle=0
voxel_fps=1
```

The battle stays off until Task 9 so the overworld is verified alone. `POST_BUILD` copies this file into `build-deck/dist/userdata/settings.txt` only if the file is absent, so on the Deck also run:

```bash
DECK_RUN 'cp tools/deck/settings.txt build-deck/dist/userdata/settings.txt && cat build-deck/dist/userdata/settings.txt'
```

Expected: the three lines above.

- [ ] **Step 3: Write `DeckGl_CheckErrors`**

In `deck_video.c`, add a file-static `sGlDebug` (set in `DeckVideo_Init` from `getenv("DECK_GL_DEBUG")`, true when set and not `"0"`), a frame counter incremented in `DeckVideo_Swap`, and a small table of `(where, code, count)` entries (fixed size 64, plain array). The function loops `GLenum e; while ((e = glGetError()) != GL_NO_ERROR)`, maps `e` to a name with a `switch` over `GL_INVALID_ENUM`, `GL_INVALID_VALUE`, `GL_INVALID_OPERATION`, `GL_STACK_OVERFLOW`, `GL_STACK_UNDERFLOW`, `GL_OUT_OF_MEMORY`, `GL_INVALID_FRAMEBUFFER_OPERATION`, and prints as described. Declare it in `deck_video.h`. `glGetError` must only be called on the thread that owns the context; `DeckVideo_Swap` runs on the main thread (`sdl2.c:849` and `voxel_renderer.c:5834` through the `vglSwapBuffers` shim), so this is safe there.

Also add, in `DeckVideo_Init` after context creation and only when `sGlDebug`, one line to stderr with `glGetString(GL_VENDOR)`, `GL_RENDERER`, `GL_VERSION`, and whether `GL_ARB_framebuffer_object` or `GL_EXT_framebuffer_object` appears in `GL_EXTENSIONS`.

- [ ] **Step 4: Build**

Run: `DECK_RUN 'cmake --build build-deck -j8 2>&1 | tail -20'`
Expected: links with no new warnings from `deck_video.c`.

- [ ] **Step 5: First 3D run with the logger on**

Run: `DECK_PLAY 'DECK_GL_DEBUG=1' 2>&1 | tee /tmp/deck_gl_run1.log` (Marcos loads the stage 1 save with Continue, walks outdoors for about 60 seconds, then quits).
Expected: the first line prints the GL strings; then either no `GLERR` lines, or a list. Copy `/tmp/deck_gl_run1.log` and `userdata/gl_errors.log` to `.superpowers/sdd/2026-11-13-deck-stage2/run1/`.

- [ ] **Step 6 (PROBE-DEPENDENT): Reconcile with the probe**

Read `.superpowers/sdd/2026-11-13-deck-stage1-boot/stage2-probe-report.md`: sections "GL strings", "errors seen", "crash or hang point", "what rendered". Decision rule:
  - If the probe reports a crash or hang before the first swap, do not continue to Step 5. Take the crash location from the report, read the function it names in `src/platform/voxel/voxel_renderer.c`, then `docs/RENDERING.md`, and fix that first in Task 2.
  - If the probe reports GL errors, the codes and call sites it lists replace the candidate list in Task 2 Step 2 as the first things fixed; compare with this run's `GLERR` lines. A mismatch between the probe and run 1 is itself recorded in `docs/DECK_NOTES.md`.
  - If the probe reports a clean run with a correct picture, Task 2 shrinks to the verification steps only.

- [ ] **Step 7: Bisect helper (only if Step 5 shows errors)**

Add to `deck_video.h` `#define DECK_GL_AT(label) DeckGl_CheckErrors(label)` and, as a temporary uncommitted edit, insert `DECK_GL_AT("pass-name")` after each `Pass_*` call inside `VoxelRenderer_RenderFrame` (`src/platform/voxel/voxel_renderer.c`, the `Pass_Ground` ... `Pass_Overlay2D` sequence). Run again; the first label that prints an error is the pass that raised it. Remove the temporary inserts before committing; keep only the macro.

- [ ] **Step 8: Commit**

```bash
git add tools/deck/settings.txt src/platform/deck/deck_video.c src/platform/deck/deck_video.h docs/DECK_NOTES.md
git commit -m "feat(deck): 3D on in dist settings and DECK_GL_DEBUG error logger"
```

---

### Task 2: Fix the GL differences found

**Agent:** Sonnet. Each item below was found by grepping the code; none is yet shown to fail on the Deck. Fix an item only when Task 1 or the probe shows it fails, or the visual check in its step shows wrong output. One commit per item fixed.

**Files:** as listed per item.

**Interfaces:**
- Consumes: Task 1 logger and its `GLERR` lines.
- Produces: no new interfaces; each fix keeps the Vita path unchanged behind `#ifdef PLATFORM_DECK`.

- [ ] **Step 1: Verify the baseline picture, no code change**

With the Task 1 run, check by eye (Marcos, plus a screenshot) that colours are right on grass, water and sprites. Colours wrong means item A. Missing textures means item B.

- [ ] **Step 2: Work through the candidates, in this order, only those that fail**

**A. Packed 16-bit texture formats.** Uploads use internal format `GL_RGBA` (unsized) with type `GL_UNSIGNED_SHORT_5_5_5_1` at `voxel_renderer.c:1049-1050, 1494-1495, 1538-1539, 1592-1593, 1703-1704, 1911-1912, 3519-3520, 3627-3628, 3646-3647` and `ui_sprites.c:372`, `ui_render.c:42`, `vita_stage.c:233`; `GL_UNSIGNED_SHORT_4_4_4_4` at `voxel_renderer.c:3585-3586` (GBA overlay) and `:3617`; `GL_RGB` + `GL_UNSIGNED_SHORT_5_6_5` at `sdl2.c:238, 254, 263, 616, 1292, 1318`. These types are valid in desktop GL 1.2+. Failing symptom: swapped or wrong colours. Fix only if the symptom shows: compare the first `glTexImage2D` call against `docs/RENDERING.md` ("Pass 6", RGBA4444) and check component order with a single test texture before changing any converter.

**B. Texture upload fallback for the Vita direct-write path.** `vglGetTexDataPointer` is called at `voxel_renderer.c:1685` (billboard sprite atlas) and `ui_sprites.c:356` (new UI cell uploads). `compat/vitaGL.h` returns 0, and both callers already take their `glTexSubImage2D` branch (`voxel_renderer.c:1703`, `ui_sprites.c:372`). No code change expected; verify with a billboard (NPC walking) and a Pokemon icon in a menu. If either is missing, check that the `texels == NULL` branch runs by a temporary `fprintf`.

**C. Framebuffer objects.** `voxel_renderer.c:3627-3653` creates the 960x544 diorama FBO (`GL_RGBA`/5551 colour texture plus `GL_DEPTH_COMPONENT16` renderbuffer) and the 240x136 scratch FBO (colour only). Nothing checks completeness. Add, after line 3653's `glBindFramebuffer(GL_FRAMEBUFFER, 0)` and before `sDioramaFboReady` is set (line 3654), for each FBO: bind it, call `glCheckFramebufferStatus(GL_FRAMEBUFFER)`, and treat anything but `GL_FRAMEBUFFER_COMPLETE` as not ready (so the game falls back to direct rendering through `dioramaActive`, line 4741). Print the status under `#ifdef PLATFORM_DECK`. This is a real hardening item whatever the probe says: an incomplete FBO silently draws nothing. Mesa may reject a depth renderbuffer of this format with a colour texture of this type; the check tells which.

**D. Alpha test.** `glEnable(GL_ALPHA_TEST)` + `glAlphaFunc(GL_GREATER, 0.0f)` at `voxel_renderer.c:4807-4808, 4869-4870, 4935-4936`, `vita_stage.c:320-321, 355-356`; `glDisable` at many sites (`:3048, 3206, 3272, 3359, 3407, 4789, 4921`, `sdl2.c:274, 363`, `ui_render.c:77`). Valid in the compatibility profile. The comment at `:4788` about keeping alpha test off is PowerVR-specific and harmless on Zen 2 / RDNA2. No change.

**E. Texture environment combiners.** `glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, ...)` and `GL_SRC1_ALPHA` at `voxel_renderer.c:4989-5004` and `:5059-5074` (the player silhouette and modulation passes). Valid in GL 1.3+ compatibility. Failing symptom would be `GL_INVALID_ENUM` at those lines or a wrong silhouette colour. Fix only if the Task 1 bisect names this pass.

**F. Client arrays and VBOs.** Dynamic batches use `glEnableClientState` with `glVertexPointer`/`glDrawArrays` from CPU memory (`voxel_renderer.c:3053-3055, 3211-3212, 3277-3278, 3363-3364, 3411-3413, 4793-4795, 5127, 5253, 5278-5279, 5306, 5364-5365, 5437, 5537-5539, 5743-5744`, `vita_stage.c:264, 323-324, 358-360`, `sdl2.c:298-299, 343-344, 497-498`, `ui_render.c:83-85`). Chunk geometry uses buffer objects, `glBufferData(GL_ARRAY_BUFFER, ..., NULL, GL_STATIC_DRAW)` at `voxel_renderer.c:1975`. All valid in compatibility GL 2.1. The most likely desktop-only fault here is a client array left enabled while a VBO is bound (or the reverse): `GL_INVALID_OPERATION` from `glDrawArrays`. Fix only if the bisect names a pass; the fix is `glBindBuffer(GL_ARRAY_BUFFER, 0)` before client-array draws, never changing the Vita path.

**G. Vita calls that become no-ops.** `vglSwapResolution` (`sdl2.c:166`), `vglSetup*`/`vglUse*` (`sdl2.c:585-590`), `vglMemFree`/`vglMemTotal` (`vita_perf.c:423-425, 547-549`), `eglSwapInterval` (`sdl2.c:176, 308, 836, 848`). All are shims in `compat/vitaGL.h`. No change in this task; pacing is Task 5, memory reporting is Task 6.

**H. Depth.** The window context asks for a 24-bit depth buffer (`deck_video.c:DeckVideo_Init`); the diorama FBO uses 16-bit. A depth precision difference with `zFar = 62` (`docs/RENDERING.md`, step 4) could show as z-fighting on the FBO path only. Fix only if z-fighting appears in screenshots with blur on and not with `voxel_blur=0`: change `GL_DEPTH_COMPONENT16` at `voxel_renderer.c:3632` to `GL_DEPTH_COMPONENT24` under `PLATFORM_DECK`.

- [ ] **Step 3 (PROBE-DEPENDENT): Add the failures the probe found that are not above**

Read the probe report section "errors seen". For each error code and call site not covered by A to H: add a new lettered item here with `file:line`, the exact `GLERR` text, the cause read from the code (not recalled), and the fix. If the report is empty on errors, record "no further items" and continue.

- [ ] **Step 4: Verify**

Run: `DECK_PLAY 'DECK_GL_DEBUG=1' 2>&1 | tee /tmp/deck_gl_run2.log`, walk 60 seconds outdoors and indoors, enter and leave a building.
Expected: no `GLERR` lines for the whole session.

- [ ] **Step 5: Commit each fixed item separately**

```bash
git add <files of the item> docs/DECK_NOTES.md
git commit -m "fix(deck): <item letter> <one-line cause>"
```

---

### Task 3: Framebuffer and viewport handling inside the letterbox

**Agent:** Sonnet

**Files:**
- Modify: `src/platform/deck/deck_video.c` (`DeckGlViewport`, `DeckVideo_Swap`)
- Possibly modify: `src/platform/voxel/voxel_renderer.c` (only if a pass is shown to break)

**Interfaces:**
- Consumes: Task 2 FBO completeness check.
- Produces: the invariant "scissor is enabled exactly while the default framebuffer is the target and the letterbox rect is active; it is disabled while any FBO is bound". Documented in a comment block above `DeckGlViewport`.

How it works today (read from `deck_video.c` and `compat/vitaGL.h`): the macro `glViewport(x,y,w,h)` becomes `DeckGlViewport`. When the default framebuffer is bound and the call is exactly `(0, 0, 960, 544)`, the viewport and a scissor are set to the letterboxed rectangle (`DeckVideo_GetViewport`) and `GL_SCISSOR_TEST` is turned on. Any other call disables the scissor and passes through. The only `glScissor` in the project is inside `DeckGlViewport`.

Calls the macro sees (all from the grep): `voxel_renderer.c:4751` (start of the passes, FBO or default), `:5229` (before the 2D overlays), `:5334` (240x136 scratch FBO, passes through), `:5375` (back to the default framebuffer for the DoF composite), `:5521` and `:5689` (overlay passes), `sdl2.c:268, 358` (2D path), `ui_render.c:69` (new UI, `viewWidth x viewHeight` supplied by `sdl2.c`).

- [ ] **Step 1: Check the scissor and FBO ordering**

Read `voxel_renderer.c:4741-4752` and `:5325-5380`. The order is: bind the diorama FBO (or 0), then `glViewport(0,0,960,544)`. With the FBO bound the macro passes through and disables the scissor, so the FBO is not clipped. With the default framebuffer it letterboxes. At `:5375` the composite binds 0 and calls `glViewport(0,0,renderW,renderH)`, which re-enables the letterbox scissor. Confirm this by reading, and write the result in `docs/DECK_NOTES.md`.

- [ ] **Step 2: Fix the scissor state after the swap**

`DeckVideo_Swap` disables the scissor to clear the bars and restores the viewport but not the scissor. Any `glClear` issued before the next `DeckGlViewport` call on the default framebuffer would then clear the whole window including the bars. Make `DeckVideo_Swap` also restore the previous `GL_SCISSOR_TEST` enable state and scissor box (`glGetBooleanv(GL_SCISSOR_TEST)`, `glGetIntegerv(GL_SCISSOR_BOX)` before, restore after). Cost: three state queries per frame; with `glGetError` logging off they are cheap, but record the swap time in Task 6 to confirm.

- [ ] **Step 3: Verify the DoF composite lands inside the letterbox**

With 3D on and `voxel_blur=1` outdoors, take a screenshot (Spectacle, host). The picture must fill the 16:9 rect with black bars top and bottom (window 1280x800 gives 1280x720 inside, bars of 40 px) and no bar contamination. Repeat with `voxel_blur=0` (edit `userdata/settings.txt`, relaunch). Both screenshots go to `.superpowers/sdd/2026-11-13-deck-stage2/`.

- [ ] **Step 4 (PROBE-DEPENDENT): Choose the action from what the probe saw**

Read the probe report section "what rendered" and the screenshots it lists. Decision rule:
  - If the scene shows correctly sized but the blurred bands (top and bottom) are wrong or missing while `voxel_blur=0` is right: the fault is in the diorama FBO path, not the letterbox. Go to Task 2 item C and H first.
  - If the picture is drawn in the lower-left 960x544 corner instead of the letterbox: a viewport call with a size other than 960x544 reached the macro with the default framebuffer bound. Find it with `DECK_GL_DEBUG` off by adding one temporary `fprintf` in `DeckGlViewport` printing `x,y,w,h` and the bound FBO, and fix that caller to pass the logical size.
  - If the picture is correct: no code change; record it.

- [ ] **Step 5: Commit**

```bash
git add src/platform/deck/deck_video.c docs/DECK_NOTES.md
git commit -m "fix(deck): scissor and FBO handling inside the letterbox"
```

---

### Task 4: Render resolution and scaling option

**Agent:** Sonnet. Start by measuring; build the option only if the measurement says it is needed.

**Files:**
- Read first: `src/platform/sdl2.c:140-168` (`sRenderWidth`, `sRenderHeight`, `VitaPlatform_ApplyRenderResolution`), `src/platform/voxel/voxel_renderer.c:116-118, 3627-3653, 4757, 5325-5334`.
- Possibly modify: `src/platform/deck/deck_video.c`, `deck_video.h`, `voxel_renderer.c` (under `PLATFORM_DECK`), `tools/deck/settings.txt`.

**Interfaces:**
- Produces (only if Step 3 says so): `void DeckVideo_GetInternalSize(int *w, int *h)` returning the 3D internal resolution, and a settings key `deck_scale` (0 = 960x544 logical, 1 = window letterbox size) read by `VitaExtras_Init`.

What the code does today: the world renders at a fixed logical 960x544. `VitaPlatform_ApplyRenderResolution` (`sdl2.c:155`) picks 960x544 or 720x408 from `voxelRes`, but `ReadConfigFile` ignores `voxelRes` now (`sdl2.c:960-961`), so it is always 960x544. The perspective aspect is hard-coded `960.0f / 544.0f` (`voxel_renderer.c:4757`). The diorama FBO is `VOXEL_DIORAMA_FBO_W/H = 960x544` and the scratch FBO is 240x136 (`:116-118`). The letterbox macro scales that 960x544 picture to the window rect; on the 1280x800 panel the rect is 1280x720, a 1.333 scale.

- [ ] **Step 1: Measure with the fixed size**

Run the stage 2 game (Task 1 settings) and take a screenshot of the overworld with blur off and on. Judge: without blur the 3D is rasterised straight into the 1280x720 letterbox rect (the viewport is mapped, so geometry is sharp at native size). With blur on the scene goes through the 960x544 FBO and is upscaled 1.333 with nearest filtering (`voxel_renderer.c:3623-3624`), so it is softer. Record both in `docs/DECK_NOTES.md`.

- [ ] **Step 2 (PROBE-DEPENDENT): Decide**

Read the probe report section "visual quality" if it has one. Decision rule: if with blur on the picture is acceptably sharp to Marcos in the Step 1 screenshot, stop here and record "fixed 960x544 kept". Otherwise do Step 3.

- [ ] **Step 3: Make the internal size a runtime value (only if Step 2 says so)**

Replace the compile-time `VOXEL_DIORAMA_FBO_W/H` and `VOXEL_SCRATCH_FBO_W/H` with file-static values set once in `VoxelRenderer_Init` from `DeckVideo_GetInternalSize` under `PLATFORM_DECK` (the Vita values stay as the `#define`s). Change the three places that assume 960x544: the perspective aspect at `:4757` becomes `(float)renderW / (float)renderH`, the `uMax/vMax` computation at `:5325-5326`, and the scratch size at `:5327-5328` (`renderW/4`). `VitaPlatform_GetRenderWidth/Height` (`sdl2.c:131-139`) must return the same internal size, because `DeckGlViewport` only letterboxes a call that equals `DECK_LOGICAL_WIDTH x DECK_LOGICAL_HEIGHT` (`deck_video.h`): change that comparison to the internal size, or the macro stops mapping the viewport. Do not change the 2D path (`sdl2.c:268`) or `ui_render.c:69`; they keep 960x544.

Note the overlay passes (`voxel_renderer.c:5521, 5689`) and the GBA overlay (pass 6) map the 288-px frame to 960 px wide; check them after any size change.

- [ ] **Step 4: Verify and commit**

Re-run Task 3 Step 3's two screenshots at the new size. Expected: sharper blurred scene, bars unchanged, no GL errors.

```bash
git add -A src/platform tools/deck docs/DECK_NOTES.md
git commit -m "feat(deck): <chosen resolution outcome>"
```

---

### Task 5: Frame pacing and the 30 / 40 / 60 FPS limiter

**Agent:** Sonnet

**Files:**
- Modify: `src/platform/deck/deck_video.c` (`DeckVideo_SetSwapInterval`), `src/platform/sdl2.c` (only inside `#ifdef PLATFORM_DECK`, near lines 744-760 and 836), `src/platform/vita/vita_extras.c`, `src/platform/sdl2.c:960-964` (config parse accepts `value <= 1` only).

**Interfaces:**
- Consumes: `PLATFORM_SETTING_VOXEL_FPS`: 0 = 30 FPS locked, 1 = 60 (`include/platform.h:41`); `is30FpsLocked` at `sdl2.c:744`.
- Produces: `DeckVideo_SetSwapInterval(int)` that caches the last value, calls `SDL_GL_SetSwapInterval` only on change, and returns or logs its failure.

How pacing works today (read from `sdl2.c:726-860`): in 3D, Core 0 waits on `sFrameReadySem`, posted by the game thread once per game frame at its 60.00 Hz audio-driven clock (`ticksNeeded` = 1 at 60 FPS, 2 at 30 FPS), draws the latest frame, and sets `accumulator = 0.0`. On the Vita `eglSwapInterval(NULL, 2)` is used at 30 FPS (`:836`). On the Deck `eglSwapInterval` is a shim for `DeckVideo_SetSwapInterval`, so the code calls `SDL_GL_SetSwapInterval` on every frame (`:308`, `:836`, `:848`), and ignores its return value. The 3D build budget is 33.3 ms at 30 FPS or 16.7 ms at 60 FPS (`voxel_renderer.c:4504`, `ChunkBuildFits` at `:2674`).

- [ ] **Step 1: Cache the swap interval and report failure**

In `DeckVideo_SetSwapInterval`, keep a `static int sCurrent = -1`; return when equal; otherwise call `SDL_GL_SetSwapInterval(interval)`; on `-1` print `SDL_GetError()` once and remember that the interval is unsupported. Expected: one SDL call per mode change, not one per frame.

- [ ] **Step 2 (PROBE-DEPENDENT): Does interval 2 work?**

Read the probe report section "swap interval" (value of `SDL_GL_GetSwapInterval()` after asking for 2, and the session type: X11 or gamescope). Decision rule:
  - If interval 2 is accepted and the frame time at 30 FPS settles at 33.3 ms in the Task 6 CSV: keep the Vita logic (`is30FpsLocked ? 2 : 1`) unchanged.
  - If interval 2 is refused (returns -1) or the CSV shows frames at 16.7 ms while the target is 30: add a software limiter under `PLATFORM_DECK`: after `VoxelRenderer_SwapBuffers()` at `sdl2.c:838`, sleep with `VitaPlatform_DelayMicroseconds` until `2 x 16666.67 us` have passed since the previous frame start, measured with `SDL_GetPerformanceCounter`; interval stays 1.
  - If the probe did not test it, run it in Step 3 and decide the same way.

- [ ] **Step 3: Measure 30 and 60**

Run `DECK_PLAY 'DECK_GL_DEBUG=1'` twice, with `voxel_fps=0` and `voxel_fps=1` in `userdata/settings.txt` (the Task 1 file; `voxel_fps` is read by `VitaExtras_Init`, `vita_extras.c:343`), 60 seconds outdoors each, quit. Read the new `perf_summary.txt` "FRAME (3D)" line and `perf_frames.csv` (Task 6 explains the columns). Expected: 30 FPS run p50 about 33.3 ms with p99 under 40 ms; 60 FPS run p50 about 16.7 ms.

- [ ] **Step 4: Experiment, 40 FPS (kept only if it passes)**

The Deck's 40 Hz mode (the SteamOS quick-access refresh rate slider, not code) changes the panel to 40 Hz. The game clock stays at 60 Hz, so 40 FPS means rendering after 1.5 game ticks on average: alternate `ticksNeeded` between 2 and 1 in the wait loop at `sdl2.c:754-760`, at swap interval 1 with the panel at 40 Hz. Add a settings value `voxel_fps=2` (extend the parse limit `value <= 1` at `sdl2.c:963` to `<= 2` and the `is30FpsLocked`-style booleans at `:744, :858` and `voxel_renderer.c:4504` to handle 2) only after reading each of those lines and recording what they do with a third value.
Measure: with the panel at 40 Hz and `voxel_fps=2`, run 60 seconds and compute the standard deviation of consecutive game-time camera steps from `perf_frames.csv`. Decision rule (agree with Marcos before adding it to the options): keep 40 FPS only if the p99 frame time is within 2 ms of the median and Marcos judges the walking motion smooth on the panel; otherwise record "40 FPS rejected, uneven game-tick spacing" in `docs/DECK_NOTES.md` and revert the `voxel_fps=2` edits.

- [ ] **Step 5: Commit**

```bash
git add src/platform docs/DECK_NOTES.md
git commit -m "feat(deck): swap interval caching and frame limiter"
```

---

### Task 6: Performance telemetry on the Deck, and the baseline

**Agent:** Sonnet for the edit, Haiku for the runs.

**Files:**
- Modify: `src/platform/vita/vita_perf.c` (lines 9-12, 37-41, 417-426, 539-552: `PLATFORM_VITA`-guarded blocks)
- Modify: `src/platform/vita/vita_perf.h` only if a declaration is needed
- Create: `tools/deck_tests/perf_report.sh`

**Interfaces:**
- Produces: `userdata/perf_summary.txt` and `userdata/perf_frames.csv` (paths `PERF_SUMMARY_PATH`, `PERF_CSV_PATH` at `vita_perf.c:19-20`) with correct Deck content.
- Produces: `tools/deck_tests/perf_report.sh <perf_frames.csv>` printing frames, mean, p50, p95, p99, max frame time and the count over 16.7 ms and 33.3 ms.

What exists: the telemetry already works on the Deck: `PLATFORM_VITA` is defined, `VitaPerf_NowMs` uses `sceKernelGetProcessTimeWide`, which `compat/psp2/` supplies, and the summary prints `FRAME (3D): p50 | p95 | p99 | max` (`vita_perf.c:476`). Stage 1 recorded that the summary still carries Vita labels and zeroes (`docs/DECK_NOTES.md`, "Task 7").

- [ ] **Step 1: Read what the compat layer gives**

Run: `grep -rn "sceKernelGetProcessTimeWide\|mallinfo" src/platform/deck/compat | head`
Expected: where the time function comes from. If it is a stub that returns 0, fix it first, because every number below depends on it; use `clock_gettime(CLOCK_MONOTONIC)` inside the stub.

- [ ] **Step 2: Replace the Vita-only memory lines**

In `vita_perf.c:417-426, 539-552` the `#ifdef PLATFORM_VITA` blocks print `newlib`, `vitaGL VRAM/RAM/PHYCONT`, which are zeros on the Deck. Under `#ifdef PLATFORM_DECK` print instead one line: process resident set from `/proc/self/statm` in KB. The existing `#ifdef PLATFORM_VITA` code stays for the Vita (use `#if defined(PLATFORM_DECK) ... #elif defined(PLATFORM_VITA)`).

- [ ] **Step 3: Add the GPU line**

In `VitaPerf_Init` (called from `sdl2.c:568`, before the GL context exists), nothing can be queried. Instead write the line from `DeckVideo_Init` end: `fprintf` of `GL_RENDERER` and `GL_VERSION` into `userdata/perf_summary.txt` is not possible before `VitaPerf_Init` opens it, so add `void VitaPerf_NoteGl(const char *renderer, const char *version)` in `vita_perf.c` that stores both strings and prints them in the summary header; call it from `DeckVideo_Init` under `PLATFORM_DECK`.

- [ ] **Step 4: Write the CSV reader**

Read the header at `vita_perf.c:220` (`frame,render3d,target_fps,frame_ms,c0_active_ms,other_ms,...`). Write `tools/deck_tests/perf_report.sh` with `awk` (no Python, Pillow is not installed in the container): select rows where `render3d == 1`, sort `frame_ms`, print the figures above. Make it executable.

- [ ] **Step 5: Build, run the baseline, and record it**

```bash
DECK_RUN 'cmake --build build-deck -j8 2>&1 | tail -5'
DECK_PLAY 'DECK_GL_DEBUG=1'
DECK_RUN 'sh tools/deck_tests/perf_report.sh build-deck/dist/userdata/perf_frames.csv'
```

Marcos plays a fixed route: Continue, walk from the player's house out to the town, 60 seconds, run along a path, then stand still 10 seconds. Expected: the report prints. Save the output as `docs/results/stage2-baseline.txt` under a table "Build / FPS mode / p50 / p95 / p99 / max / over 16.7 / over 33.3". All later measured tasks add a row.

- [ ] **Step 6 (PROBE-DEPENDENT): Compare with the probe**

Read the probe report section "frame time" (if present). Record the difference between its numbers and the baseline row in the same file. A difference over 20 percent in p50 means the two runs were not equivalent; repeat the baseline once before using it.

- [ ] **Step 7: Commit**

```bash
git add src/platform/vita/vita_perf.c tools/deck_tests/perf_report.sh docs/results/stage2-baseline.txt docs/DECK_NOTES.md
git commit -m "feat(deck): Deck perf telemetry, report script and baseline"
```

---

### Task 7: Link-time optimization (measured)

**Agent:** Sonnet

**Files:**
- Modify: `CMakeLists.txt` (the `if(DECK)` block at lines 208-215 sets `-O3 -march=znver2 -mtune=znver2 -msse2`; read it first)

**Interfaces:**
- Produces: CMake option `DECK_LTO` (default OFF) adding `-flto` to compile and link flags for the Deck target only.

- [ ] **Step 1: Read the flag block and the link rules**

Run: `sed -n 205,216p CMakeLists.txt; grep -n "ld -r\|DECK_LD\|objcopy" CMakeLists.txt | head`
The build links partial objects with `ld -r` and runs `objcopy` (stage 1 Task 3). LTO objects cannot go through `ld -r` / `objcopy` as plain ELF. Find which objects take that path; only objects the game links through `target_link_libraries` can use `-flto`.

- [ ] **Step 2: Add the option and a separate build directory**

Add `option(DECK_LTO "LTO for the Deck build" OFF)` and, when ON, `-flto=auto` to `CMAKE_C_FLAGS` and `CMAKE_EXE_LINKER_FLAGS` for the game target. Configure a second tree so the baseline binary stays intact:

```bash
DECK_RUN 'cmake -S . -B build-deck-lto -DDECK=ON -DDECK_LTO=ON && cmake --build build-deck-lto -j8 2>&1 | tail -20'
```

Expected: links. If it fails on the `ld -r` objects, read the first error, keep those objects non-LTO, and retry (max 3 times, then record the failure in `docs/DECK_NOTES.md` and stop; do not force it).

- [ ] **Step 3: Measure against the baseline**

Copy `build-deck/dist/userdata` to `build-deck-lto/dist/userdata` (settings and save) and run the Task 6 Step 5 route once with each binary, twice each, alternating. Add rows to `docs/results/stage2-baseline.txt`.

- [ ] **Step 4: Decision rule**

Keep LTO only if the p50 improves by at least 3 percent or p99 by at least 1 ms, in both runs, and the smoke test (Task 10) passes. Otherwise leave `DECK_LTO` OFF, keep the option, and record the numbers.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt docs/results/stage2-baseline.txt docs/DECK_NOTES.md
git commit -m "build(deck): DECK_LTO option, measured"
```

---

### Task 8: Profile-guided optimization (measured, separate from LTO)

**Agent:** Sonnet

**Files:**
- Modify: `CMakeLists.txt`; Create: `tools/deck/pgo.sh`

**Interfaces:**
- Produces: `tools/deck/pgo.sh gen|use` building `build-deck-pgo-gen` with `-fprofile-generate -fprofile-update=atomic` and `build-deck-pgo` with `-fprofile-use -fprofile-correction`. `-fprofile-update=atomic` matters because the game runs worker threads (`docs/THREADS.md`).

- [ ] **Step 1: Only start if Tasks 6 and 7 show a need**

Read `docs/results/stage2-baseline.txt`. If the 60 FPS p99 is already under 16.7 ms and the smoke and play tests are smooth, record "PGO not justified by measurement" in `docs/DECK_NOTES.md` and stop. The spec (section 4) allows PGO only if the numbers justify it.

- [ ] **Step 2: Generate**

Build the instrumented binary, run the Task 6 route and a wild battle (Task 9 must be done first for a representative profile), quit cleanly so the `.gcda` files are written (a crash discards them), then build with `-fprofile-use`.

- [ ] **Step 3: Measure and decide**

Same alternating protocol and decision rule as Task 7 Step 3 and 4.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt tools/deck/pgo.sh docs/results/stage2-baseline.txt docs/DECK_NOTES.md
git commit -m "build(deck): PGO script, measured"
```

---

### Task 9: 3D battle

**Agent:** Sonnet

**Files:**
- Modify: `tools/deck/settings.txt`
- Read: `src/platform/voxel/voxel_battle.c`, `voxel_battle.h`, `docs/VOXEL_ENGINE.md` (battle section)
- Possibly modify: files named by the logger.

**Interfaces:**
- Consumes: the `voxel_battle` setting (`vita_extras.c:287`); the 3D active condition at `sdl2.c:726-733`: `VOXEL_3D` on and (`VoxelWorld_IsMapAvailable()` or (`VoxelWorld_IsBattleMapAvailable()` and `voxel_battle` on and not `VoxelBattle_IsLink()`)).

- [ ] **Step 1: Read how the battle enters the 3D path**

Run: `grep -n "VoxelBattle_\|BattleCamera\|battleCam" src/platform/voxel/voxel_renderer.c | head -30`, then read `VoxelBattle_IsLink` in `voxel_battle.c`. The battle overlay (pass 6) draws the GBA battle UI over the 3D arena with the 288-px line at uniform x3.4 mapping and RGBA4444 (`docs/RENDERING.md`, "Pass 6"); the texture for it is created at `voxel_renderer.c:3585-3586`.

- [ ] **Step 2: Enable and run a wild battle**

Set `voxel_battle=1` in `tools/deck/settings.txt` and in `build-deck/dist/userdata/settings.txt`. Run `DECK_PLAY 'DECK_GL_DEBUG=1' 2>&1 | tee /tmp/deck_gl_battle.log`: Continue, walk into tall grass until a wild battle starts, play two turns, run away.
Expected: the 3D arena appears with camera movement, the battle HUD is readable, no `GLERR` lines.

- [ ] **Step 3 (PROBE-DEPENDENT): Use the probe's battle result**

Read the probe report section "battle" (if the probe entered a battle). Decision rule:
  - If the probe saw GL errors or a crash entering the battle: start from its call site, read the code there, fix it as a new lettered item of Task 2, and add the symptom to `docs/DECK_NOTES.md`.
  - If the probe did not enter a battle, Step 2 is the first test; any failure is diagnosed with the Task 1 bisect macro.
  - If the battle works, record the p50/p95/p99 of the battle section of `perf_frames.csv` as new rows in `docs/results/stage2-baseline.txt`.

- [ ] **Step 4: Verify the overlay alignment**

Take a screenshot in the battle with the HUD open. The HUD must align with the arena at the letterbox edges (bars black, no HUD clipped). If the overlay is offset, the cause is a viewport or scissor call in `voxel_renderer.c:5521` or `:5689`; compare against Task 3 Step 1.

- [ ] **Step 5: Commit**

```bash
git add tools/deck/settings.txt src/platform docs/DECK_NOTES.md docs/results/stage2-baseline.txt
git commit -m "feat(deck): 3D wild battle on desktop GL"
```

---

### Task 10: Smoke test and manual play checklist

**Agent:** Haiku (script), Marcos (checklist)

**Files:**
- Create: `tools/deck_tests/smoke3d.sh`
- Modify: `docs/DECK_NOTES.md`

**Interfaces:**
- Consumes: Task 1 logger. Produces: exit code 0 on pass, 1 on fail.

- [ ] **Step 1: Write the smoke test**

`tools/deck_tests/smoke3d.sh` (same style as stage 1's `smoke.sh`): starts the game from `dist/` with `DECK_GL_DEBUG=1 timeout 30`, fails if it exits with a code other than 124, or if `userdata/gl_errors.log` is non-empty after the run, or if the log contains `Segmentation`:

```bash
#!/bin/sh
cd "$(dirname "$0")/../../build-deck/dist" || exit 1
rm -f userdata/gl_errors.log
DECK_GL_DEBUG=1 timeout 30 ./pokeemerald >/tmp/deck_smoke3d.log 2>&1
code=$?
if [ "$code" -ne 124 ]; then
    echo "FAIL: exited with $code before 30 s"; tail -5 /tmp/deck_smoke3d.log; exit 1
fi
if [ -s userdata/gl_errors.log ]; then
    echo "FAIL: GL errors"; head -5 userdata/gl_errors.log; exit 1
fi
echo "PASS: 30 s with 3D enabled, no GL errors"
```

Limit: it only reaches the title screen unattended, so it proves startup with 3D enabled, context creation, FBO completeness and the absence of load-time GL errors, not the overworld. The overworld is covered by Step 3. Run it under the container with the same display env as `DECK_PLAY`.

- [ ] **Step 2: Run it**

Run: `ssh deck "distrobox enter emerald-build -- bash -lc 'cd /run/media/deck/SD512/emerald-deck/src && DISPLAY=:0 XAUTHORITY=\$(ls /run/user/1000/xauth_*) XDG_RUNTIME_DIR=/run/user/1000 sh tools/deck_tests/smoke3d.sh'"`
Expected: `PASS: 30 s with 3D enabled, no GL errors`.

- [ ] **Step 3: Manual play checklist (Marcos)**

With `DECK_GL_DEBUG=1`, `voxel_3d=1`, `voxel_battle=1`, `voxel_blur=1`, Continue from the stage 1 save, tick each line:

- [ ] Overworld renders in 3D with sun direction and cast shadows; shadows move with the in-game clock.
- [ ] Walk 60 seconds across at least two maps and one map connection; no pop-in holes lasting more than a second.
- [ ] Depth-of-field blur on outdoors, off indoors and on fades; toggling `voxel_blur` in the menu or file works.
- [ ] Water animates, flowers animate, NPC billboards show correct animation frames.
- [ ] Enter and leave a building; the camera and fade are smooth.
- [ ] Open the START menu in 3D and close it; text boxes appear over the 3D world aligned with it.
- [ ] Wild battle: 3D arena, both Pokemon visible, HUD readable, attack animation plays, running away returns to the overworld.
- [ ] 30 FPS and 60 FPS options each give a steady picture; battery power draw at 30 FPS is lower (note the Steam Deck's performance overlay reading).
- [ ] Black bars top and bottom (window 1280x800 gives a 1280x720 picture); nothing drawn in the bars.
- [ ] `userdata/gl_errors.log` empty at the end; `perf_summary.txt` p99 recorded in `docs/results/stage2-baseline.txt`.

Anything failing is recorded in `docs/DECK_NOTES.md` with the exact symptom and the `GLERR` line if any.

- [ ] **Step 4: Commit**

```bash
git add tools/deck_tests/smoke3d.sh docs/DECK_NOTES.md docs/results
git commit -m "feat(deck): stage 2 voxel 3D smoke test and checklist"
```

---

## Self-review

- **Spec coverage:** 3D on and error logging (Task 1); renderer shim differences (Task 2, items A-H); FBO and letterbox (Task 3); resolution not fixed at 960x544 and scale options (Task 4); `SDL_GL_SetSwapInterval` pacing with 30 and 40 FPS (Task 5); `vita_perf.c` telemetry kept and measured (Task 6); `-O3 -march=znver2` already set, LTO (Task 7) and PGO (Task 8) as separate measured tasks; 3D battle (Task 9); smoke test and manual checklist (Task 10). Not covered here by design: VBO conversion of the per-frame client arrays, which the spec says to do only if the profile shows draw calls are the bottleneck; add it as a task after Task 6 if `perf_summary.txt` shows it. SSE2 NEON replacement was done in stage 1.
- **Placeholders:** every PROBE-DEPENDENT step names what to read and the decision rule; no other deferred text.
- **Name consistency:** `DeckGl_CheckErrors`, `DECK_GL_AT`, `DeckVideo_SetSwapInterval`, `DeckGlViewport`, `DeckVideo_GetViewport`, `DECK_LOGICAL_WIDTH/HEIGHT` match `src/platform/deck/deck_video.h` and `compat/vitaGL.h` as they are at commit time of this draft. `DeckVideo_GetInternalSize` and `VitaPerf_NoteGl` are new in this plan.
