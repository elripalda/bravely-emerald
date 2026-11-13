# Steam Deck Visual Upgrade, Phase 1 (shader base and first post effects) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** On the Steam Deck only, record a real frame-time and present-interval baseline, then add a small GLSL 1.20 shader foundation and a post chain on an RGBA8 scene buffer: bloom driven by the existing `light.bloom`, a time-of-day colour grade, a vignette and a smooth separable depth-of-field blur that replaces the blocky 1/4-res bands, all switchable from `settings.txt` with Low / Medium / High presets. With `voxel_shaders=0` the renderer takes exactly the old code path.

**Architecture:** All new code lives in new files under `src/platform/deck/` (`deck_gl_shader.c/.h`: GL 2.0 entry points through `SDL_GL_GetProcAddress`, program build with logs, uniform setters, FBO helper, GPU timer; `deck_post.c/.h`: scene and half-res targets, embedded shader strings, settings resolution, the post passes). The existing renderer gets small `#ifdef PLATFORM_DECK` hooks: create and free the targets next to the existing diorama FBOs, pick the RGBA8 scene target in `Frame_BeginPasses` when any post effect is on (no longer only when the DoF is on), and run `DeckPost_Run` instead of `Pass_DepthOfField` at the same point of `VoxelRenderer_RenderFrame`. The legacy RGBA5551 diorama FBO and `Pass_DepthOfField` stay untouched and remain the fallback (shaders off, target incomplete, other render size, battles). Telemetry gains a present-interval column and post chain CPU/GPU columns.

**Tech Stack:** C (gnu99), desktop OpenGL 2.1 compatibility context on Mesa radeonsi, GLSL 1.20 as embedded C strings, SDL2 (static, 32-bit), CMake `-DDECK=ON` build in the `emerald-build` distrobox, POSIX sh and awk for reports, Python 3 + Pillow (in the container) for screenshot statistics.

**Spec:** `docs/superpowers/specs/2026-11-13-deck-visual-upgrade-design.md` (phase 1 of section 5; options A1, B1, G1, G3; section 4 settings; Decisions 2026-11-13).

## Global Constraints

- Scope: Steam Deck build only. Every change in a shared file sits inside `#ifdef PLATFORM_DECK` (the Deck also defines `PLATFORM_VITA`, `CMakeLists.txt:78-81`, so never guard with `!PLATFORM_VITA`). New files go under `src/platform/deck/`, which the Vita build excludes (`CMakeLists.txt:103`). The Vita build output must not change.
- Shaders: GLSL `#version 120` on the existing compatibility context, written as embedded C string literals (Decision 11). `glUseProgram(0)` returns to fixed-function after every shader pass. No loose shader files.
- Settings: keys in `userdata/settings.txt` (format `key=value`, parsed by `VitaExtras_Init`, `src/platform/vita/vita_extras.c:354-397`). New keys: `voxel_quality` (0 Low, 1 Medium, 2 High; default 1 = Medium, Decision 7), `voxel_shaders` (0 off = legacy picture, 1 on; default 1), `voxel_grade` (grade + vignette: 0 off, 1 on, 2 auto = preset; default 2), `voxel_bloom` (0 off, 1 on, 2 auto; default 2). Existing `voxel_blur` (0/1, default 1) stays the DoF switch. Presets: Low = grade + vignette + DoF, no bloom; Medium = "faithful plus" (grade, vignette, bloom, DoF); High = the stronger Octopath look (same effects, stronger values) (Decision 2).
- Display: window 1280x800 fullscreen (`deck_video.c:117-118`), the 960x544 picture letterboxed through the `glViewport` macro (`compat/vitaGL.h:13` -> `DeckGlViewport`, `deck_video.c:180-197`). The macro letterboxes and enables `GL_SCISSOR_TEST` only when framebuffer 0 is bound and the call is exactly `(0, 0, 960, 544)`; any other call disables the scissor and passes through. So: bind the FBO first, then set the viewport.
- Frame rate: 60 FPS lock is the target (`voxel_fps=1`; the default `voxel_fps=0` is 30 FPS, `vita_extras.c:134,276`, `sdl2.c:744`). Budget rule from the spec section 4: High keeps 3D frames at p99 under 14 ms at 60 FPS.
- Post effects apply in the field only, not in 3D battles (Decision 12). No DoF indoors (same rule as today, `voxel_renderer.c:4742`).
- Code is written at `F:\emerald-deck` on branch `deck-port`, pushed with `git push deck deck-port:main`, pulled on the Deck. Nothing goes to GitHub unless Marcos asks.
- Exactly ONE game instance at a time. Before every start, list instances; stop one only by pid. Never `pkill -x pokeemerald` (the main thread is renamed `MainThread`, so name matching is unreliable). List with `ssh deck 'ps -eo pid,args | grep [p]okeemerald'`, stop with `ssh deck 'kill <pid>'`.
- Never guess an error cause. On any error, crash or wrong picture: first grep the exact message in `docs/results/`, `docs/DECK_NOTES.md`, `docs/RENDERING.md`, `docs/VOXEL_ENGINE.md`, `.superpowers/sdd/2026-11-13-deck-stage1-boot/*.md`; then read the code; then GL documentation. Change one thing at a time with a written hypothesis.
- Agents: Sonnet implements; Opus reviews GL correctness (Task 4 and Task 8); Haiku does command-only steps marked "Haiku". Each agent: 30 to 60 tool calls, at most 3 retries, report to the orchestrator in 40 lines or fewer. Edits to `voxel_renderer.c` are never split across parallel agents.
- Commit messages end with the trailer `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>` (written as a second `-m`).

## Commands used throughout

Run a command in the build container on the Deck from this PC, written `DECK_RUN '<cmd>'`:

```bash
ssh deck "distrobox enter emerald-build -- bash -lc 'cd /run/media/deck/SD512/emerald-deck/src && <cmd>'"
```

Start the game with a window (Desktop Mode, run it as a background command; it blocks until the game exits), written `DECK_PLAY '<env>'`:

```bash
ssh deck "distrobox enter emerald-build -- bash -lc 'cd /run/media/deck/SD512/emerald-deck/src/build-deck/dist && DISPLAY=:0 XAUTHORITY=\$(ls /run/user/1000/xauth_*) XDG_RUNTIME_DIR=/run/user/1000 <env> ./run.sh'"
```

List and stop the game (written `DECK_PS` and `DECK_KILL <pid>`):

```bash
ssh deck 'ps -eo pid,args | grep [p]okeemerald'
ssh deck 'kill <pid>'
```

`DECK_PS` must print nothing before a `DECK_PLAY`. Stop only the pid whose args are `./pokeemerald`. `kill` (SIGTERM) does not flush the CSV buffer; the CSV and summary are flushed every summary window (`vita_perf.c:644-646`, at most 120 3D frames or 5 s), so the last few seconds of a run may be missing. Leave 10 s of standing still at the end of every measured route.

Sync and build after a change (written `SYNC_BUILD`):

```bash
git push deck deck-port:main
ssh deck 'git -C /run/media/deck/SD512/emerald-deck/src pull'
DECK_RUN 'cmake --build build-deck -j8 2>&1 | tail -20'
```

Screenshot on the Deck host (not in the container), written `DECK_SHOT <label>` (script from Task 7; before Task 7 exists use the `spectacle` line from `docs/DECK_NOTES.md:117` with the same `DISPLAY`/`XAUTHORITY`):

```bash
ssh deck 'sh /run/media/deck/SD512/emerald-deck/src/tools/deck/shots.sh <label>'
```

Fixed measured route (spec section 5, extended with a battle for this phase): Continue from the save, walk out of the house into town, run along a path into grass until a wild battle starts, play two turns, run away, walk back, stand still 10 s. About 3 minutes. Marcos plays it; the agent starts and stops the game.

---

### Task 1: Baseline, present interval, swap interval check, GL/GLSL log

**Agent:** Sonnet; Haiku for Steps 1 to 3 (commands only).

**Files:**
- Create: `tools/deck/setkey.sh`, `docs/results/deck-visual-phase1-perf.txt`
- Modify: `src/platform/vita/vita_perf.h` (Deck declarations), `src/platform/vita/vita_perf.c` (lines 24-25 area, 225-233, 379-387, 744-771, after 772), `src/platform/deck/deck_video.c` (lines 130, 141-145, 199-202), `tools/deck/perf_report.sh`, `docs/DECK_NOTES.md`

**Interfaces:**
- Produces (`vita_perf.h`, Deck only): `VitaPerf_DeckMarkPostStart(void)`, `VitaPerf_DeckMarkPostEnd(void)`, `VitaPerf_DeckSetPostGpuMs(double ms)`.
- Produces CSV columns (Deck only, appended at the end so existing columns keep their names): `present_ms`, `post_ms`, `post_gpu_ms`.
- Produces: `DeckVideo_SetSwapInterval` only calls SDL when the interval changes and logs the result to `userdata/debug.log`.

- [ ] **Step 1: Read what is already known**

Run: `grep -rn -i "swap interval\|swap_ms\|present\|vsync" docs/DECK_NOTES.md .superpowers/sdd/2026-11-13-deck-stage1-boot/ docs/results/ 2>/dev/null | head -20`
Known so far (`.superpowers/sdd/2026-11-13-deck-stage1-boot/stage2a-report.md:35`): `frame_ms` is work time (frame start to after the swap), not the present interval; `swap_ms` averaged 0.54 ms, so the swap did not visibly block. `debug.log` holds vendor/renderer but the `version=` value was never written into the docs.

- [ ] **Step 2: Add the settings helper (Haiku)**

`tools/deck/setkey.sh`:

```sh
#!/bin/sh
# usage: setkey.sh <settings.txt> <key> <value>
# Sets key=value in a settings.txt; appends the line when the key is absent. Prints the result.
f="$1"; k="$2"; v="$3"
[ -f "$f" ] || { echo "no $f"; exit 1; }
if grep -q "^$k=" "$f"; then
    sed -i "s/^$k=.*/$k=$v/" "$f"
else
    echo "$k=$v" >> "$f"
fi
grep "^$k=" "$f"
```

Commit and sync it: `git add tools/deck/setkey.sh && git commit -m "tools(deck): setkey.sh for settings.txt" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"`, then the first two lines of `SYNC_BUILD`.

- [ ] **Step 3: Baseline run with the unchanged binary (Haiku + Marcos)**

```bash
DECK_RUN 'sh tools/deck/setkey.sh build-deck/dist/userdata/settings.txt voxel_fps 1'
DECK_RUN 'sh tools/deck/setkey.sh build-deck/dist/userdata/settings.txt voxel_3d 1'
DECK_RUN 'sh tools/deck/setkey.sh build-deck/dist/userdata/settings.txt voxel_battle 1'
DECK_RUN 'sh tools/deck/setkey.sh build-deck/dist/userdata/settings.txt voxel_blur 1'
DECK_PS
```

`DECK_PS` must be empty. Start `DECK_PLAY ''` in the background; Marcos plays the fixed route (about 3 minutes); then `DECK_PS`, `DECK_KILL <pid>`. Save and report:

```bash
DECK_RUN 'mkdir -p ../perf/p1-baseline && cp build-deck/dist/userdata/perf_frames.csv build-deck/dist/userdata/perf_summary.txt ../perf/p1-baseline/ && cp build-deck/dist/userdata/debug.log ../perf/p1-baseline/ 2>/dev/null; sh tools/deck/perf_report.sh ../perf/p1-baseline'
DECK_RUN 'grep "\[GL\]" build-deck/dist/userdata/debug.log | tail -5'
```

Expected: the report prints 3D frames, frame ms, FPS, draw3D and chunk build lines. Write the full output, the `[GL] ... version=` line and the date into a new file `docs/results/deck-visual-phase1-perf.txt` under a heading `== baseline (before phase 1, voxel_fps=1, voxel_blur=1) ==`.

- [ ] **Step 4: Present interval in `vita_perf.c` (Deck only)**

After the two path defines (`vita_perf.c:24-25`) add:

```c
#ifdef PLATFORM_DECK
// Extra CSV columns of the Deck build (present interval and post chain), appended at the end.
#define PERF_CSV_DECK_COLUMNS ",present_ms,post_ms,post_gpu_ms"
#else
#define PERF_CSV_DECK_COLUMNS ""
#endif
```

In `VitaPerf_Init`, change the last header string (`vita_perf.c:233`) from `"ui2_render_ms,ui2_tick_ms,ui2_quads\n");` to `"ui2_render_ms,ui2_tick_ms,ui2_quads" PERF_CSV_DECK_COLUMNS "\n");`.

Right before `void VitaPerf_MarkSwapStart(void)` (`vita_perf.c:379`) add:

```c
#ifdef PLATFORM_DECK
static double sDeckLastSwapEndMs = 0.0; // end of the previous 3D swap; 0 after a frame without one
static double sDeckPresentMs = 0.0;     // end of this swap minus end of the previous one
static double sDeckPostStartMs = 0.0;
static double sDeckPostMs = 0.0;        // CPU time of DeckPost_Run this frame
static double sDeckPostGpuMs = -1.0;    // GPU time of an earlier frame's post chain, -1 unknown

void VitaPerf_DeckMarkPostStart(void)
{
    sDeckPostStartMs = VitaPerf_NowMs();
}

void VitaPerf_DeckMarkPostEnd(void)
{
    sDeckPostMs = VitaPerf_NowMs() - sDeckPostStartMs;
}

void VitaPerf_DeckSetPostGpuMs(const double ms)
{
    sDeckPostGpuMs = ms;
}
#endif
```

Replace the body of `VitaPerf_MarkSwapEnd` (`vita_perf.c:384-387`) with (the Vita line stays as it is):

```c
void VitaPerf_MarkSwapEnd(void)
{
    sCurSwapMs = VitaPerf_NowMs() - sSwapStartMs;
#ifdef PLATFORM_DECK
    {
        const double endMs = sSwapStartMs + sCurSwapMs;
        sDeckPresentMs = (sDeckLastSwapEndMs > 0.0) ? (endMs - sDeckLastSwapEndMs) : 0.0;
        sDeckLastSwapEndMs = endMs;
    }
#endif
}
```

In `VitaPerf_EndFrame`, the row format string (`vita_perf.c:753`) changes from `"%.3f,%.3f,%u\n",` to `"%.3f,%.3f,%u",`, and right after the closing `);` of that `fprintf` (`vita_perf.c:771`) add:

```c
#ifdef PLATFORM_DECK
        fprintf(sCsvFile, ",%.2f,%.3f,%.3f\n", sDeckPresentMs, sDeckPostMs, sDeckPostGpuMs);
#else
        fputc('\n', sCsvFile);
#endif
```

After the closing brace of the `if (sCsvFile != NULL)` block (`vita_perf.c:772`) add:

```c
#ifdef PLATFORM_DECK
    // A frame without a 3D swap breaks the chain: the next 3D frame has no present interval.
    if (!didRender3D)
        sDeckLastSwapEndMs = 0.0;
    sDeckPresentMs = 0.0;
    sDeckPostMs = 0.0;
    sDeckPostGpuMs = -1.0;
#endif
```

In `vita_perf.h`, before `// Core 1 (AgbMain GBA Simulation) Stage Markers` add:

```c
#ifdef PLATFORM_DECK
// Deck post chain (src/platform/deck/deck_post.c): CPU time of DeckPost_Run, and the GPU time
// of the post chain measured with GL_TIME_ELAPSED two frames earlier (-1 when unknown).
// CSV columns post_ms and post_gpu_ms; present_ms comes from VitaPerf_MarkSwapEnd.
void VitaPerf_DeckMarkPostStart(void);
void VitaPerf_DeckMarkPostEnd(void);
void VitaPerf_DeckSetPostGpuMs(double ms);
#endif
```

- [ ] **Step 5: Swap interval with checked results, GLSL version in the log (`deck_video.c`)**

Replace `DeckVideo_SetSwapInterval` (`deck_video.c:199-202`) with:

```c
// sdl2.c asks for the interval every 3D frame (sdl2.c:836, eglSwapInterval -> here). Only a change
// reaches SDL; each change is logged with SDL's answer, so the log shows whether vsync took.
static int sSwapInterval = -1000;

void DeckVideo_SetSwapInterval(int interval)
{
    if (interval == sSwapInterval)
        return;
    sSwapInterval = interval; // also on failure, so a refused value is logged once, not per frame
    const int ret = SDL_GL_SetSwapInterval(interval);
    DeckDebugLog("[GL] swap interval %d: SDL_GL_SetSwapInterval=%d (%s) | SDL_GL_GetSwapInterval=%d | video driver %s\n",
                 interval, ret, ret == 0 ? "ok" : SDL_GetError(), SDL_GL_GetSwapInterval(),
                 SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "none");
}
```

In `DeckVideo_Init` replace `SDL_GL_SetSwapInterval(1);` (`deck_video.c:130`) with `DeckVideo_SetSwapInterval(1);` (declared in `deck_video.h:11`). In the `DeckDebugLog` call at `deck_video.c:141-145` add the GLSL version and the `vblank_mode` environment variable (Mesa's driconf override): change the format's first line to `"[GL] vendor=%s | renderer=%s | version=%s | glsl=%s | vblank_mode=%s\n"` and add after the `GL_VERSION` argument:

```c
                     (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION),
                     getenv("vblank_mode") ? getenv("vblank_mode") : "unset",
```

- [ ] **Step 6: Report the new columns (`tools/deck/perf_report.sh`)**

In the first summary `awk` (after the `FPS:` printf) add `printf "frame ms p99 %.2f\n", v[k1]` (the list is sorted worst first, so `v[k1]` is the 1% boundary). Append at the end of the script, before the `if [ -f "$sum" ]` block:

```sh
# Deck: present interval = time between the ends of two consecutive 3D swaps
# (vita_perf.c VitaPerf_MarkSwapEnd). CSVs from before phase 1 have no such column.
awk -F, 'NR==1 {for (i=1;i<=NF;i++) c[$i]=i; if (!("present_ms" in c)) exit; next}
         $c["render3d"]==1 && $c["present_ms"]>0 {print $c["present_ms"]}' "$csv" | sort -rn > "$tmp"
if [ -s "$tmp" ]; then
    awk '{v[NR]=$1; s+=$1}
         END {
           n=NR; k1=int(n*0.01); if (k1<1) k1=1
           for (i=1;i<=n;i++) {if (v[i]>17.5) m1++; if (v[i]>34.2) m2++}
           printf "present ms: avg %.2f  p50 %.2f  p99 %.2f  max %.2f  (shown FPS %.1f)\n", s/n, v[int(n/2)+1], v[k1], v[1], 1000/(s/n)
           printf "present over 17.5 ms: %d (%.2f%%)  over 34.2 ms: %d (%.2f%%)\n", m1, 100*m1/n, m2, 100*m2/n
         }' "$tmp"
else
    echo "present ms: no column (CSV from before phase 1)"
fi
awk -F, 'NR==1 {for (i=1;i<=NF;i++) c[$i]=i; if (!("post_ms" in c)) exit; next}
         $c["render3d"]==1 && $c["post_ms"]>0 {
           p+=$c["post_ms"]; np++
           if ($c["post_gpu_ms"]>0) {g+=$c["post_gpu_ms"]; ng++; if ($c["post_gpu_ms"]>gm) gm=$c["post_gpu_ms"]}
         }
         END {
           if (np==0) {print "post: not run"; exit}
           printf "post CPU ms: avg %.3f over %d frames\n", p/np, np
           if (ng>0) printf "post GPU ms: avg %.3f  max %.3f\n", g/ng, gm
           else print "post GPU ms: n/a (no GL_ARB_timer_query result)"
         }' "$csv"
```

- [ ] **Step 7: Build, run, read the pacing**

`SYNC_BUILD`. Expected: no error. `DECK_PS` empty, then `DECK_PLAY ''` (background), Marcos plays the fixed route, `DECK_KILL <pid>`. Then:

```bash
DECK_RUN 'mkdir -p ../perf/p1-present60 && cp build-deck/dist/userdata/perf_frames.csv build-deck/dist/userdata/perf_summary.txt build-deck/dist/userdata/debug.log ../perf/p1-present60/ && sh tools/deck/perf_report.sh ../perf/p1-present60'
DECK_RUN 'grep "\[GL\]" build-deck/dist/userdata/debug.log | tail -6'
DECK_RUN 'head -1 build-deck/dist/userdata/perf_frames.csv | tr , "\n" | tail -3'
```

Expected: the last three header names are `present_ms`, `post_ms`, `post_gpu_ms`; the `[GL]` lines show `glsl=...` and one `swap interval 1: SDL_GL_SetSwapInterval=0 (ok) | SDL_GL_GetSwapInterval=1`. Paced to the display at 60 FPS means: present p50 between 16.2 and 17.2 ms and "present over 17.5 ms" under 2%. Repeat once with `voxel_fps 0` (setkey) and save to `../perf/p1-present30`: expected present p50 about 33.3 ms and a log line `swap interval 2: ... =0 (ok) | ...=2`. Set `voxel_fps 1` again afterwards.

- [ ] **Step 8: If the present interval does not pace, gather evidence and stop**

If `SDL_GL_SetSwapInterval` returned non-zero, `SDL_GL_GetSwapInterval` differs from the request, or present p50 is far from 16.7 / 33.3 ms, collect (do not change any setting yet):

```bash
ssh deck 'loginctl list-sessions; ps -eo comm | grep -E "^(kwin_x11|kwin_wayland|gamescope|Xwayland)" | sort -u'
ssh deck 'cat ~/.drirc 2>/dev/null; ls -l /etc/drirc 2>/dev/null'
DECK_RUN 'env | grep -i -E "vblank|sdl_video|display" '
```

Hypotheses to check against that output, one at a time: (a) Mesa `vblank_mode=0` in the environment or drirc disables vsync; (b) the session is Wayland and SDL runs on Xwayland, where interval 2 may be unsupported; (c) KWin compositing paces differently from a direct flip. Write the evidence into `docs/DECK_NOTES.md` under `## Phase 1: swap interval` and report to the orchestrator; Marcos decides on any change to the session. The 60 Hz rendering cadence itself is already driven by the game-frame semaphore (`sdl2.c:752-768`), so the rest of phase 1 can proceed with the measured numbers.

- [ ] **Step 9: Record and commit**

Append the Step 7 reports to `docs/results/deck-visual-phase1-perf.txt` under `== present interval (no visual change) ==`. Add to `docs/DECK_NOTES.md` a line with the exact `GL_VERSION` and GLSL strings and the swap interval results.

```bash
git add src/platform/vita/vita_perf.c src/platform/vita/vita_perf.h src/platform/deck/deck_video.c tools/deck/perf_report.sh docs/results/deck-visual-phase1-perf.txt docs/DECK_NOTES.md
git commit -m "perf(deck): present interval and post columns, checked swap interval, GLSL log" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Shader loader, program and uniform helpers, FBO helper, GPU timer

**Agent:** Sonnet.

**Files:**
- Create: `src/platform/deck/deck_gl_shader.h`, `src/platform/deck/deck_gl_shader.c`
- Modify: `src/platform/deck/deck_video.c` (include, one call after the log block at line 145)

**Interfaces:**
- Produces: the declarations in `deck_gl_shader.h` below. All functions run on the GL (Core 0 / main) thread only.
- Consumes: `VITA_DATA_DIR` from `vita_platform.h` (already used by `deck_video.c:6,16`), `SDL_GL_GetProcAddress`.

- [ ] **Step 1: Write `deck_gl_shader.h`**

```c
#ifndef GUARD_DECK_GL_SHADER_H
#define GUARD_DECK_GL_SHADER_H

#include <stdbool.h>
#include <GL/gl.h>
#include <GL/glext.h>

// GLSL 1.20 programs on the compatibility context (Deck only). GL 2.0 entry points come from
// SDL_GL_GetProcAddress; nothing here relies on GL_GLEXT_PROTOTYPES for them.
// Logs go to userdata/gl_errors.log and stderr. GL (main) thread only.

typedef struct
{
    GLuint program;      // 0 when the build failed
    const char *name;
    GLint uTime, uWind;  // -1 when the program does not use them
} DeckGlProgram;

typedef enum
{
    DECK_GL_DEPTH_NONE = 0,
    DECK_GL_DEPTH_TEXTURE24,       // depth texture (readable by later DoF work)
    DECK_GL_DEPTH_RENDERBUFFER24,
} DeckGlDepth;

typedef struct
{
    GLuint fbo, color, depthTex, depthRb;
    int w, h;
    DeckGlDepth depth;
    bool complete;
} DeckGlTarget;

void DeckGl_Log(const char *fmt, ...);
bool DeckGl_Init(void);            // once per context; false: no GLSL 1.20 or an entry point is missing
bool DeckGl_SelfTest(void);        // builds and deletes a trivial program, logs the result
bool DeckGl_BuildProgram(DeckGlProgram *out, const char *name, const char *vs, const char *fs);
void DeckGl_DeleteProgram(DeckGlProgram *p);
void DeckGl_UseProgram(const DeckGlProgram *p); // NULL: glUseProgram(0); else also sets uTime/uWind
GLint DeckGl_Uniform(const DeckGlProgram *p, const char *name);
void DeckGl_Set1i(GLint loc, int v);
void DeckGl_Set1f(GLint loc, float v);
void DeckGl_Set2f(GLint loc, float a, float b);
void DeckGl_Set3f(GLint loc, float a, float b, float c);
void DeckGl_Set4f(GLint loc, float a, float b, float c, float d);
void DeckGl_ActiveTexture(GLenum unit);
// Per-frame values for every program that declares uTime / uWind (wind = dirX, dirZ, strength).
void DeckGl_SetFrameUniforms(float timeSeconds, float windX, float windZ, float windStrength);

// RGBA8 colour target with optional depth; logs glCheckFramebufferStatus; restores the binding.
bool DeckGl_CreateTarget(DeckGlTarget *t, int w, int h, DeckGlDepth depth, GLint filter);
void DeckGl_DeleteTarget(DeckGlTarget *t);

// GL_TIME_ELAPSED timer (GL_ARB_timer_query), two queries in turn.
bool DeckGl_TimerInit(void);
void DeckGl_TimerShutdown(void);
void DeckGl_TimerBegin(void);
void DeckGl_TimerEnd(void);
double DeckGl_TimerLastMs(void);   // newest finished result, -1 if none

#endif
```

- [ ] **Step 2: Write `deck_gl_shader.c`**

```c
#include "deck_gl_shader.h"
#include <SDL2/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "vita_platform.h"

#define DECK_GL_LOG_PATH VITA_DATA_DIR "gl_errors.log"

static struct
{
    PFNGLCREATESHADERPROC CreateShader;
    PFNGLSHADERSOURCEPROC ShaderSource;
    PFNGLCOMPILESHADERPROC CompileShader;
    PFNGLGETSHADERIVPROC GetShaderiv;
    PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog;
    PFNGLDELETESHADERPROC DeleteShader;
    PFNGLCREATEPROGRAMPROC CreateProgram;
    PFNGLATTACHSHADERPROC AttachShader;
    PFNGLLINKPROGRAMPROC LinkProgram;
    PFNGLGETPROGRAMIVPROC GetProgramiv;
    PFNGLGETPROGRAMINFOLOGPROC GetProgramInfoLog;
    PFNGLDELETEPROGRAMPROC DeleteProgram;
    PFNGLUSEPROGRAMPROC UseProgram;
    PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation;
    PFNGLUNIFORM1IPROC Uniform1i;
    PFNGLUNIFORM1FPROC Uniform1f;
    PFNGLUNIFORM2FPROC Uniform2f;
    PFNGLUNIFORM3FPROC Uniform3f;
    PFNGLUNIFORM4FPROC Uniform4f;
    PFNGLACTIVETEXTUREPROC ActiveTexture;
    PFNGLGENQUERIESPROC GenQueries;
    PFNGLDELETEQUERIESPROC DeleteQueries;
    PFNGLBEGINQUERYPROC BeginQuery;
    PFNGLENDQUERYPROC EndQuery;
    PFNGLGETQUERYOBJECTIVPROC GetQueryObjectiv;
    PFNGLGETQUERYOBJECTUI64VPROC GetQueryObjectui64v;
} sGl;

static bool sLoaded, sAvailable;
static float sTime, sWind[3];

static GLuint sTimerQ[2];
static bool sTimerOn, sTimerUsed[2];
static unsigned sTimerIdx;
static double sTimerLastMs = -1.0;

void DeckGl_Log(const char *fmt, ...)
{
    char line[1536];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    fputs(line, stderr);
    FILE *f = fopen(DECK_GL_LOG_PATH, "a");
    if (f != NULL)
    {
        fputs(line, f);
        fclose(f);
    }
}

// glXGetProcAddress can return non-NULL for functions the driver lacks, so a non-NULL pointer is
// necessary, not sufficient: DeckGl_SelfTest proves the path by compiling and linking.
#define DECK_GL_LOAD(field, name)                                              \
    do                                                                         \
    {                                                                          \
        sGl.field = (__typeof__(sGl.field))SDL_GL_GetProcAddress(name);        \
        if (sGl.field == NULL)                                                 \
        {                                                                      \
            DeckGl_Log("[SHADER] missing entry point %s\n", name);             \
            ok = false;                                                        \
        }                                                                      \
    } while (0)

bool DeckGl_Init(void)
{
    if (sLoaded)
        return sAvailable;
    sLoaded = true;

    const char *glsl = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
    int major = 0, minor = 0;
    if (glsl == NULL || sscanf(glsl, "%d.%d", &major, &minor) != 2 || major * 100 + minor < 120)
    {
        DeckGl_Log("[SHADER] GLSL 1.20 not available (GL_SHADING_LANGUAGE_VERSION=%s)\n", glsl ? glsl : "NULL");
        return false;
    }

    bool ok = true;
    DECK_GL_LOAD(CreateShader, "glCreateShader");
    DECK_GL_LOAD(ShaderSource, "glShaderSource");
    DECK_GL_LOAD(CompileShader, "glCompileShader");
    DECK_GL_LOAD(GetShaderiv, "glGetShaderiv");
    DECK_GL_LOAD(GetShaderInfoLog, "glGetShaderInfoLog");
    DECK_GL_LOAD(DeleteShader, "glDeleteShader");
    DECK_GL_LOAD(CreateProgram, "glCreateProgram");
    DECK_GL_LOAD(AttachShader, "glAttachShader");
    DECK_GL_LOAD(LinkProgram, "glLinkProgram");
    DECK_GL_LOAD(GetProgramiv, "glGetProgramiv");
    DECK_GL_LOAD(GetProgramInfoLog, "glGetProgramInfoLog");
    DECK_GL_LOAD(DeleteProgram, "glDeleteProgram");
    DECK_GL_LOAD(UseProgram, "glUseProgram");
    DECK_GL_LOAD(GetUniformLocation, "glGetUniformLocation");
    DECK_GL_LOAD(Uniform1i, "glUniform1i");
    DECK_GL_LOAD(Uniform1f, "glUniform1f");
    DECK_GL_LOAD(Uniform2f, "glUniform2f");
    DECK_GL_LOAD(Uniform3f, "glUniform3f");
    DECK_GL_LOAD(Uniform4f, "glUniform4f");
    DECK_GL_LOAD(ActiveTexture, "glActiveTexture");
    sAvailable = ok;
    DeckGl_Log("[SHADER] GL_VERSION=%s | GLSL=%s | GL 2.0 entry points %s\n",
               (const char *)glGetString(GL_VERSION), glsl, ok ? "loaded" : "MISSING");
    return ok;
}

static GLuint CompileStage(GLenum type, const char *name, const char *src)
{
    GLint status = 0;
    const GLuint s = sGl.CreateShader(type);
    sGl.ShaderSource(s, 1, &src, NULL);
    sGl.CompileShader(s);
    sGl.GetShaderiv(s, GL_COMPILE_STATUS, &status);
    if (!status)
    {
        char log[1024];
        GLsizei n = 0;
        sGl.GetShaderInfoLog(s, (GLsizei)sizeof(log), &n, log);
        DeckGl_Log("[SHADER] %s %s compile FAILED:\n%.*s\n", name,
                   type == GL_VERTEX_SHADER ? "vertex" : "fragment", (int)n, log);
        sGl.DeleteShader(s);
        return 0;
    }
    return s;
}

bool DeckGl_BuildProgram(DeckGlProgram *out, const char *name, const char *vs, const char *fs)
{
    memset(out, 0, sizeof(*out));
    out->name = name;
    out->uTime = -1;
    out->uWind = -1;
    if (!sAvailable)
        return false;

    const GLuint v = CompileStage(GL_VERTEX_SHADER, name, vs);
    const GLuint f = (v != 0) ? CompileStage(GL_FRAGMENT_SHADER, name, fs) : 0;
    if (f == 0)
    {
        if (v != 0)
            sGl.DeleteShader(v);
        return false;
    }
    const GLuint p = sGl.CreateProgram();
    GLint linked = 0;
    sGl.AttachShader(p, v);
    sGl.AttachShader(p, f);
    sGl.LinkProgram(p);
    sGl.DeleteShader(v); // attached shaders are only flagged; freed with the program
    sGl.DeleteShader(f);
    sGl.GetProgramiv(p, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        char log[1024];
        GLsizei n = 0;
        sGl.GetProgramInfoLog(p, (GLsizei)sizeof(log), &n, log);
        DeckGl_Log("[SHADER] %s link FAILED:\n%.*s\n", name, (int)n, log);
        sGl.DeleteProgram(p);
        return false;
    }
    out->program = p;
    out->uTime = sGl.GetUniformLocation(p, "uTime");
    out->uWind = sGl.GetUniformLocation(p, "uWind");
    DeckGl_Log("[SHADER] %s: linked (program %u)\n", name, (unsigned)p);
    return true;
}

void DeckGl_DeleteProgram(DeckGlProgram *p)
{
    if (p->program != 0 && sAvailable)
        sGl.DeleteProgram(p->program);
    p->program = 0;
}

void DeckGl_UseProgram(const DeckGlProgram *p)
{
    if (!sAvailable)
        return;
    if (p == NULL || p->program == 0)
    {
        sGl.UseProgram(0);
        return;
    }
    sGl.UseProgram(p->program);
    if (p->uTime >= 0)
        sGl.Uniform1f(p->uTime, sTime);
    if (p->uWind >= 0)
        sGl.Uniform3f(p->uWind, sWind[0], sWind[1], sWind[2]);
}

GLint DeckGl_Uniform(const DeckGlProgram *p, const char *name)
{
    return (sAvailable && p->program != 0) ? sGl.GetUniformLocation(p->program, name) : -1;
}

void DeckGl_Set1i(GLint loc, int v) { if (loc >= 0) sGl.Uniform1i(loc, v); }
void DeckGl_Set1f(GLint loc, float v) { if (loc >= 0) sGl.Uniform1f(loc, v); }
void DeckGl_Set2f(GLint loc, float a, float b) { if (loc >= 0) sGl.Uniform2f(loc, a, b); }
void DeckGl_Set3f(GLint loc, float a, float b, float c) { if (loc >= 0) sGl.Uniform3f(loc, a, b, c); }
void DeckGl_Set4f(GLint loc, float a, float b, float c, float d) { if (loc >= 0) sGl.Uniform4f(loc, a, b, c, d); }

void DeckGl_ActiveTexture(GLenum unit)
{
    if (sAvailable)
        sGl.ActiveTexture(unit);
}

void DeckGl_SetFrameUniforms(float timeSeconds, float windX, float windZ, float windStrength)
{
    sTime = timeSeconds;
    sWind[0] = windX;
    sWind[1] = windZ;
    sWind[2] = windStrength;
}

bool DeckGl_SelfTest(void)
{
    static const char kVs[] =
        "#version 120\n"
        "void main() { gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex; gl_FrontColor = gl_Color; }\n";
    static const char kFs[] =
        "#version 120\n"
        "uniform float uTime;\n"
        "void main() { gl_FragColor = gl_Color * (0.5 + 0.5 * sin(uTime)); }\n";
    DeckGlProgram p;
    if (!DeckGl_Init() || !DeckGl_BuildProgram(&p, "selftest", kVs, kFs))
    {
        DeckGl_Log("[SHADER] self-test FAILED\n");
        return false;
    }
    DeckGl_DeleteProgram(&p);
    DeckGl_Log("[SHADER] self-test OK\n");
    return true;
}

bool DeckGl_CreateTarget(DeckGlTarget *t, int w, int h, DeckGlDepth depth, GLint filter)
{
    static const char *const kDepthNames[] = { "none", "texture24", "renderbuffer24" };
    GLint prev = 0;
    memset(t, 0, sizeof(*t));
    t->w = w;
    t->h = h;
    t->depth = depth;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);

    glGenTextures(1, &t->color);
    glBindTexture(GL_TEXTURE_2D, t->color);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    glGenFramebuffers(1, &t->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->color, 0);
    if (depth == DECK_GL_DEPTH_TEXTURE24)
    {
        glGenTextures(1, &t->depthTex);
        glBindTexture(GL_TEXTURE_2D, t->depthTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, t->depthTex, 0);
    }
    else if (depth == DECK_GL_DEPTH_RENDERBUFFER24)
    {
        glGenRenderbuffers(1, &t->depthRb);
        glBindRenderbuffer(GL_RENDERBUFFER, t->depthRb);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, t->depthRb);
    }
    const GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev);
    glBindTexture(GL_TEXTURE_2D, 0);
    t->complete = (st == GL_FRAMEBUFFER_COMPLETE);
    DeckGl_Log("[FBO] %dx%d RGBA8 depth=%s: status 0x%04x %s\n", w, h, kDepthNames[depth],
               (unsigned)st, t->complete ? "COMPLETE" : "INCOMPLETE");
    return t->complete;
}

void DeckGl_DeleteTarget(DeckGlTarget *t)
{
    if (t->fbo) glDeleteFramebuffers(1, &t->fbo);
    if (t->color) glDeleteTextures(1, &t->color);
    if (t->depthTex) glDeleteTextures(1, &t->depthTex);
    if (t->depthRb) glDeleteRenderbuffers(1, &t->depthRb);
    memset(t, 0, sizeof(*t));
}

bool DeckGl_TimerInit(void)
{
    const char *ext = (const char *)glGetString(GL_EXTENSIONS);
    bool ok = true;
    sTimerOn = false;
    if (!sAvailable || ext == NULL || strstr(ext, "GL_ARB_timer_query") == NULL)
    {
        DeckGl_Log("[SHADER] GPU timer off (GL_ARB_timer_query not listed)\n");
        return false;
    }
    DECK_GL_LOAD(GenQueries, "glGenQueries");
    DECK_GL_LOAD(DeleteQueries, "glDeleteQueries");
    DECK_GL_LOAD(BeginQuery, "glBeginQuery");
    DECK_GL_LOAD(EndQuery, "glEndQuery");
    DECK_GL_LOAD(GetQueryObjectiv, "glGetQueryObjectiv");
    DECK_GL_LOAD(GetQueryObjectui64v, "glGetQueryObjectui64v");
    if (!ok)
        return false;
    sGl.GenQueries(2, sTimerQ);
    sTimerUsed[0] = sTimerUsed[1] = false;
    sTimerIdx = 0;
    sTimerLastMs = -1.0;
    sTimerOn = true;
    return true;
}

void DeckGl_TimerShutdown(void)
{
    if (sTimerOn)
        sGl.DeleteQueries(2, sTimerQ);
    sTimerOn = false;
}

void DeckGl_TimerBegin(void)
{
    if (!sTimerOn)
        return;
    const unsigned i = sTimerIdx & 1u;
    if (sTimerUsed[i])
    {
        GLint ready = 0;
        sGl.GetQueryObjectiv(sTimerQ[i], GL_QUERY_RESULT_AVAILABLE, &ready);
        if (ready)
        {
            GLuint64 ns = 0;
            sGl.GetQueryObjectui64v(sTimerQ[i], GL_QUERY_RESULT, &ns);
            sTimerLastMs = (double)ns / 1.0e6;
        }
        else
        {
            sTimerLastMs = -1.0; // not ready: never stall the frame waiting for it
        }
    }
    sGl.BeginQuery(GL_TIME_ELAPSED, sTimerQ[i]);
}

void DeckGl_TimerEnd(void)
{
    if (!sTimerOn)
        return;
    sGl.EndQuery(GL_TIME_ELAPSED);
    sTimerUsed[sTimerIdx & 1u] = true;
    sTimerIdx++;
}

double DeckGl_TimerLastMs(void)
{
    return sTimerOn ? sTimerLastMs : -1.0;
}
```

If the compiler reports an unknown `PFNGL...PROC` type, read the installed header first (`DECK_RUN 'grep -n "PFNGLCREATESHADERPROC\|PFNGLACTIVETEXTUREPROC\|PFNGLGETQUERYOBJECTUI64VPROC" /run/media/deck/SD512/emerald-deck/deps32/include/GL/glext.h /usr/include/GL/gl.h /usr/include/GL/glext.h 2>/dev/null | head'`) and use the name it defines; do not write the typedef from memory.

- [ ] **Step 3: Call the self-test from `DeckVideo_Init`**

In `deck_video.c` add `#include "deck_gl_shader.h"` after `#include <GL/glext.h>` (line 8). Inside the block that ends at `deck_video.c:146`, after the `DeckDebugLog(...)` call (lines 141-145), add:

```c
        DeckDebugLog("[GL] shader self-test: %s (details in gl_errors.log)\n", DeckGl_SelfTest() ? "OK" : "FAILED");
```

It must stay after `remove(GLERR_LOG_PATH)` (line 140) so the log of this run is kept.

- [ ] **Step 4: Build and check the log**

`SYNC_BUILD`. Expected: no compile error in `deck_gl_shader.c`. `DECK_PS` empty, `DECK_PLAY ''` (background), wait 20 s, `DECK_PS`, `DECK_KILL <pid>`. Then:

```bash
DECK_RUN 'grep "\[SHADER\]" build-deck/dist/userdata/gl_errors.log; grep "self-test" build-deck/dist/userdata/debug.log'
```

Expected: `[SHADER] GL_VERSION=... | GLSL=... | GL 2.0 entry points loaded`, `[SHADER] selftest: linked (program N)`, `[SHADER] self-test OK`, and in `debug.log` `[GL] shader self-test: OK`. On FAILED: the compile/link log is in `gl_errors.log`; read it, do not change the shader blindly.

- [ ] **Step 5: Commit**

```bash
git add src/platform/deck/deck_gl_shader.c src/platform/deck/deck_gl_shader.h src/platform/deck/deck_video.c
git commit -m "feat(deck): GLSL 1.20 loader, program/uniform helpers, FBO helper, GPU timer" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Settings keys and presets skeleton

**Agent:** Haiku (mechanical edits), Sonnet checks the result.

**Files:**
- Modify: `include/platform/vita_extras.h` (enum, lines 44-74), `src/platform/vita/vita_extras.c` (lines 93-99, 134, 274-276)

**Interfaces:**
- Produces: `FAST_EXTRA_VOXEL_QUALITY`, `FAST_EXTRA_VOXEL_SHADERS`, `FAST_EXTRA_VOXEL_GRADE`, `FAST_EXTRA_VOXEL_BLOOM` (Deck only), read with `VitaExtras_GetFast`. Defaults come from the `fallback` field of new `gVitaExtras` rows: `VitaExtras_Init` seeds every row's fallback before reading the file (`vita_extras.c:361-366`), so an old `settings.txt` without the keys still gets them.
- Validation: the parser stores any integer (`vita_extras.c:385-389`); range checks are done where the values are read (`DeckPost_ReadConfig`, Task 4).

- [ ] **Step 1: Enum entries**

In `include/platform/vita_extras.h`, between `FAST_EXTRA_UI_THEME,` (line 72) and `FAST_EXTRA_COUNT` (line 73):

```c
#ifdef PLATFORM_DECK
    FAST_EXTRA_VOXEL_QUALITY,  // Deck effects preset: 0 low, 1 medium, 2 high
    FAST_EXTRA_VOXEL_SHADERS,  // Deck shader master switch: 0 off (legacy picture), 1 on
    FAST_EXTRA_VOXEL_GRADE,    // Deck colour grade + vignette: 0 off, 1 on, 2 auto (preset)
    FAST_EXTRA_VOXEL_BLOOM,    // Deck bloom: 0 off, 1 on, 2 auto (preset)
#endif
```

- [ ] **Step 2: Key lookup**

In `LookupFastId`, `case 'v':`, after the `voxel_blur` line (`vita_extras.c:98`) and before `break;`:

```c
#ifdef PLATFORM_DECK
        if (strcmp(key, "voxel_quality") == 0) return FAST_EXTRA_VOXEL_QUALITY;
        if (strcmp(key, "voxel_shaders") == 0) return FAST_EXTRA_VOXEL_SHADERS;
        if (strcmp(key, "voxel_grade") == 0) return FAST_EXTRA_VOXEL_GRADE;
        if (strcmp(key, "voxel_bloom") == 0) return FAST_EXTRA_VOXEL_BLOOM;
#endif
```

- [ ] **Step 3: Value labels and rows with defaults**

After `sVoxelFpsValues` (`vita_extras.c:134`):

```c
#ifdef PLATFORM_DECK
static const char *const sVoxelQualityValues[] = { "LOW", "MEDIUM", "HIGH", NULL };
static const char *const sVoxelFxValues[] = { "OFF", "ON", "AUTO", NULL };
#endif
```

After the `voxel_fps` row (`vita_extras.c:276`):

```c
#ifdef PLATFORM_DECK
    { VITA_TAB_WORLD,   "EFFECTS",    "EFFECTS PRESET",    "voxel_quality",    3, 1, sVoxelQualityValues, NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS",    "SHADERS",           "voxel_shaders",    2, 1, gVitaExtrasOffOn,    NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS",    "COLOR GRADE",       "voxel_grade",      3, 2, sVoxelFxValues,      NULL, NULL, NULL },
    { VITA_TAB_WORLD,   "EFFECTS",    "BLOOM",             "voxel_bloom",      3, 2, sVoxelFxValues,      NULL, NULL, NULL },
#endif
```

Labels use only letters and spaces (characters already used by existing rows); `MAX_SETTINGS_ENTRIES` is 112 (`vita_extras.c:23`), far above the row count.

- [ ] **Step 4: Build and verify the defaults are written**

`SYNC_BUILD`. Then, with no game running (`DECK_PS` empty):

```bash
DECK_RUN 'cd build-deck/dist/userdata && mv settings.txt settings.txt.p1keep'
```

`DECK_PLAY ''` (background), wait 15 s, `DECK_PS`, `DECK_KILL <pid>`.

```bash
DECK_RUN 'cd build-deck/dist/userdata && grep -E "^voxel_(quality|shaders|grade|bloom|blur|fps)=" settings.txt; mv settings.txt.p1keep settings.txt; grep -c . settings.txt'
```

Expected: `voxel_quality=1`, `voxel_shaders=1`, `voxel_grade=2`, `voxel_bloom=2`, `voxel_blur=1`, `voxel_fps=0` (fresh defaults, written by `VitaExtras_Save` when the file is missing, `vita_extras.c:371-374`), then the original file is back. Then `DECK_RUN 'sh tools/deck/setkey.sh build-deck/dist/userdata/settings.txt voxel_fps 1'`.

- [ ] **Step 5: Commit**

```bash
git add include/platform/vita_extras.h src/platform/vita/vita_extras.c
git commit -m "feat(deck): voxel_quality/shaders/grade/bloom settings keys" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 4: RGBA8 scene target, post module and renderer hooks (FBO path decoupled from DoF)

**Agent:** Sonnet implements; Opus reviews the GL state and the fallback before commit.

**Files:**
- Create: `src/platform/deck/deck_post.h`, `src/platform/deck/deck_post.c`, `tools/deck_tests/shot_stats.py`
- Modify: `src/platform/voxel/voxel_renderer.c` at: includes (after line 28), end of the FBO block in `VoxelRenderer_Init` (after line 3657), `VoxelRenderer_Shutdown` (after line 3725), `VoxelFrame` (after line 3854), `Frame_BeginPasses` (lines 4744-4751), new `Pass_DeckPost` after `Pass_DepthOfField` (after line 5475), `VoxelRenderer_RenderFrame` (lines 5817-5818)

**Interfaces:**
- Consumes: `DeckGl_*` (Task 2), `FAST_EXTRA_VOXEL_*` (Task 3), `VitaPerf_DeckMark*` (Task 1), `DeckGlViewport` (`deck_video.h:15`), `VitaDayNight_Get` (`include/platform/vita_daynight.h:55`; `daylight` 1.0 noon, 0.35 dusk, 0 night; `glow` 0 day, 0.25 dusk, 1 night, `vita_daynight.c:84-93`).
- Produces (`deck_post.h`): `DeckPost_Init`, `DeckPost_Shutdown`, `DeckPost_ReadConfig`, `DeckPost_WantScene`, `DeckPost_BindScene`, `DeckPost_Run`, types `DeckPostFrame`, `DeckPostConfig`.
- Post chain order: scene (RGBA8 + depth) -> bright pass to half res -> separable blur (bloom) -> scene copy to half res -> separable blur (DoF) -> one composite into the letterbox (DoF mix, bloom add, grade, vignette) -> `Pass_Overlay2D` and `Ui_Render` unchanged on top (`voxel_renderer.c:5820-5823`), so text and menus are never blurred or graded.
- Fades: fades and moonlight are drawn inside the scene by `Pass_Atmosphere2D` (`voxel_renderer.c:5217`); grade, vignette and bloom are scaled by `1 - fadeAmount` so a fade to white or black is not tinted (spec risk 7).

- [ ] **Step 1: Write `deck_post.h`**

```c
#ifndef GUARD_DECK_POST_H
#define GUARD_DECK_POST_H

#include <stdbool.h>

// Deck post chain on an RGBA8 scene target: bloom, time-of-day grade, vignette, smooth DoF.
// GL (Core 0) thread only. The legacy diorama FBO and Pass_DepthOfField stay the fallback.

typedef struct
{
    int renderW, renderH;  // f->renderW / f->renderH (must equal the scene target size)
    bool indoor;           // no DoF indoors (same rule as Frame_BeginPasses)
    float fade;            // f->fadeAmount: grade, vignette and bloom fade out with it
    float bloom;           // f->light.bloom (per weather, BaseLightFor)
    float daylight, glow;  // VitaDayNight_Get(); indoor passes 1 and 0 (neutral day)
    float dofTopRows, dofBottomRows, dofTopAlpha, dofBottomAlpha; // VOXEL_DOF_* (voxel_tuning.h:89-92)
} DeckPostFrame;

typedef struct
{
    int quality;   // voxel_quality: 0 low, 1 medium, 2 high
    bool shaders;  // voxel_shaders
    bool grade;    // voxel_grade, resolved against the preset (grade + vignette)
    bool bloom;    // voxel_bloom, resolved against the preset
    bool blur;     // voxel_blur
} DeckPostConfig;

void DeckPost_Init(int sceneW, int sceneH);
void DeckPost_Shutdown(void);
DeckPostConfig DeckPost_ReadConfig(void);
// true: this frame's 3D scene goes to the RGBA8 scene target. Call before binding a framebuffer.
bool DeckPost_WantScene(int renderW, int renderH, bool indoor);
void DeckPost_BindScene(void);
// Leaves: framebuffer 0 with the letterboxed viewport and scissor, program 0, texture unit 0
// active with GL_TEXTURE_2D enabled, blend/depth test/alpha test off, depth mask off, client
// arrays off, projection glOrtho(0,1,1,0), modelview identity (as Pass_DepthOfField leaves it).
void DeckPost_Run(const DeckPostFrame *in);

#endif
```

- [ ] **Step 2: Write `deck_post.c`**

```c
#include "deck_post.h"
#include "deck_gl_shader.h"
#include "deck_video.h"
#include "platform/vita_extras.h"
#include "../vita/vita_perf.h"
#include <stdlib.h>
#include <string.h>

// ---- Presets (Decision 2: Medium "faithful plus", High stronger). Starting values, tuned in Task 5.
typedef struct
{
    float grade;       // strength of the time-of-day tint
    float saturation;  // 1.0 = unchanged
    float vignette;    // darkening at the corners
    float bloomGain;   // composite bloom = light.bloom * bloomGain (0 = no bloom in this preset)
    float threshold;   // bright pass luminance threshold
    int bloomPasses;   // separable blur iterations at half resolution
} DeckPostPreset;

static const DeckPostPreset kPresets[3] = {
    /* low    */ { 0.60f, 1.04f, 0.12f, 0.0f, 0.70f, 1 },
    /* medium */ { 0.80f, 1.06f, 0.18f, 3.0f, 0.65f, 1 },
    /* high   */ { 1.00f, 1.12f, 0.30f, 5.0f, 0.58f, 2 },
};

// Tints per time of day: shadows (cool by day, purple at dusk, deep blue at night) and highlights.
static const float kShadowTint[3][3] = { { 0.94f, 0.97f, 1.06f }, { 1.02f, 0.90f, 1.08f }, { 0.86f, 0.92f, 1.12f } };
static const float kHighTint[3][3]   = { { 1.04f, 1.01f, 0.95f }, { 1.10f, 0.97f, 0.84f }, { 0.97f, 1.00f, 1.05f } };

// ---- Shaders (GLSL 1.20). Quads are drawn in [0,1]^2 with gl_Vertex; vPos is the screen
// position, y up, which is also the uv of every target (all targets cover the full frame).
static const char kFullscreenVs[] =
    "#version 120\n"
    "varying vec2 vPos;\n"
    "void main() {\n"
    "    vPos = gl_Vertex.xy;\n"
    "    gl_Position = vec4(gl_Vertex.xy * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

static const char kBrightFs[] =
    "#version 120\n"
    "uniform sampler2D uScene;\n"
    "uniform float uThreshold;\n"
    "varying vec2 vPos;\n"
    "void main() {\n"
    "    vec3 c = texture2D(uScene, vPos).rgb;\n"
    "    float l = dot(c, vec3(0.299, 0.587, 0.114));\n"
    "    gl_FragColor = vec4(c * smoothstep(uThreshold, 1.0, l), 1.0);\n"
    "}\n";

static const char kCopyFs[] =
    "#version 120\n"
    "uniform sampler2D uScene;\n"
    "varying vec2 vPos;\n"
    "void main() {\n"
    "    gl_FragColor = vec4(texture2D(uScene, vPos).rgb, 1.0);\n"
    "}\n";

// 9-tap Gaussian folded into 5 bilinear taps (offsets between texels, GL_LINEAR source).
static const char kBlurFs[] =
    "#version 120\n"
    "uniform sampler2D uSrc;\n"
    "uniform vec2 uStep;\n"
    "varying vec2 vPos;\n"
    "void main() {\n"
    "    vec3 c = texture2D(uSrc, vPos).rgb * 0.2270270270;\n"
    "    c += texture2D(uSrc, vPos + uStep * 1.3846153846).rgb * 0.3162162162;\n"
    "    c += texture2D(uSrc, vPos - uStep * 1.3846153846).rgb * 0.3162162162;\n"
    "    c += texture2D(uSrc, vPos + uStep * 3.2307692308).rgb * 0.0702702703;\n"
    "    c += texture2D(uSrc, vPos - uStep * 3.2307692308).rgb * 0.0702702703;\n"
    "    gl_FragColor = vec4(c, 1.0);\n"
    "}\n";

// uDof: x top band height, y bottom band height (fractions of the frame), z/w blur at the edges.
// Same band shape as Pass_DepthOfField (linear ramp from the edge alpha to 0), smooth blur.
static const char kCompositeFs[] =
    "#version 120\n"
    "uniform sampler2D uScene;\n"
    "uniform sampler2D uBloom;\n"
    "uniform sampler2D uBlur;\n"
    "uniform float uBloomAmt;\n"
    "uniform float uBlurOn;\n"
    "uniform vec4 uDof;\n"
    "uniform float uGradeAmt;\n"
    "uniform vec3 uShadowTint;\n"
    "uniform vec3 uHighTint;\n"
    "uniform float uSaturation;\n"
    "uniform float uVignette;\n"
    "uniform float uAspect;\n"
    "varying vec2 vPos;\n"
    "void main() {\n"
    "    vec3 c = texture2D(uScene, vPos).rgb;\n"
    "    if (uBlurOn > 0.5) {\n"
    "        float wTop = uDof.z * clamp((vPos.y - (1.0 - uDof.x)) / uDof.x, 0.0, 1.0);\n"
    "        float wBot = uDof.w * clamp((uDof.y - vPos.y) / uDof.y, 0.0, 1.0);\n"
    "        c = mix(c, texture2D(uBlur, vPos).rgb, max(wTop, wBot));\n"
    "    }\n"
    "    c += texture2D(uBloom, vPos).rgb * uBloomAmt;\n"
    "    float l = dot(c, vec3(0.299, 0.587, 0.114));\n"
    "    vec3 tint = mix(uShadowTint, uHighTint, smoothstep(0.15, 0.75, l));\n"
    "    vec3 g = mix(vec3(l), c * tint, uSaturation);\n"
    "    c = mix(c, g, uGradeAmt);\n"
    "    vec2 d = (vPos - 0.5) * vec2(uAspect, 1.0);\n"
    "    float v = 1.0 - smoothstep(0.35, 0.95, length(d));\n"
    "    c *= mix(1.0, v, uVignette);\n"
    "    gl_FragColor = vec4(clamp(c, 0.0, 1.0), 1.0);\n"
    "}\n";

static bool sReady;
static bool sSizeWarned;
static DeckGlTarget sScene;
static DeckGlTarget sHalf[3]; // 0 bloom, 1 blur scratch, 2 DoF
static DeckGlProgram sBright, sCopy, sBlur, sComposite;
static GLint sBrightScene, sBrightThreshold, sCopyScene, sBlurSrc, sBlurStep;
static GLint sCScene, sCBloom, sCBlur, sCBloomAmt, sCBlurOn, sCDof, sCGradeAmt, sCShadow, sCHigh, sCSat, sCVignette, sCAspect;

static const float kQuad[8] = { 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f };

static float Clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static int ClampKey(int v, int lo, int hi, int def)
{
    return (v < lo || v > hi) ? def : v;
}

DeckPostConfig DeckPost_ReadConfig(void)
{
    DeckPostConfig c;
    c.quality = ClampKey(VitaExtras_GetFast(FAST_EXTRA_VOXEL_QUALITY), 0, 2, 1);
    c.shaders = ClampKey(VitaExtras_GetFast(FAST_EXTRA_VOXEL_SHADERS), 0, 1, 1) != 0;
    const int grade = ClampKey(VitaExtras_GetFast(FAST_EXTRA_VOXEL_GRADE), 0, 2, 2);
    const int bloom = ClampKey(VitaExtras_GetFast(FAST_EXTRA_VOXEL_BLOOM), 0, 2, 2);
    c.grade = (grade == 2) ? true : (grade == 1);                          // on in every preset
    c.bloom = (bloom == 2) ? (kPresets[c.quality].bloomGain > 0.0f) : (bloom == 1);
    c.blur = VitaExtras_GetFast(FAST_EXTRA_VOXEL_BLUR) != 0;
    return c;
}

static bool BuildPrograms(void)
{
    if (!DeckGl_BuildProgram(&sBright, "post_bright", kFullscreenVs, kBrightFs)
        || !DeckGl_BuildProgram(&sCopy, "post_copy", kFullscreenVs, kCopyFs)
        || !DeckGl_BuildProgram(&sBlur, "post_blur", kFullscreenVs, kBlurFs)
        || !DeckGl_BuildProgram(&sComposite, "post_composite", kFullscreenVs, kCompositeFs))
        return false;
    sBrightScene = DeckGl_Uniform(&sBright, "uScene");
    sBrightThreshold = DeckGl_Uniform(&sBright, "uThreshold");
    sCopyScene = DeckGl_Uniform(&sCopy, "uScene");
    sBlurSrc = DeckGl_Uniform(&sBlur, "uSrc");
    sBlurStep = DeckGl_Uniform(&sBlur, "uStep");
    sCScene = DeckGl_Uniform(&sComposite, "uScene");
    sCBloom = DeckGl_Uniform(&sComposite, "uBloom");
    sCBlur = DeckGl_Uniform(&sComposite, "uBlur");
    sCBloomAmt = DeckGl_Uniform(&sComposite, "uBloomAmt");
    sCBlurOn = DeckGl_Uniform(&sComposite, "uBlurOn");
    sCDof = DeckGl_Uniform(&sComposite, "uDof");
    sCGradeAmt = DeckGl_Uniform(&sComposite, "uGradeAmt");
    sCShadow = DeckGl_Uniform(&sComposite, "uShadowTint");
    sCHigh = DeckGl_Uniform(&sComposite, "uHighTint");
    sCSat = DeckGl_Uniform(&sComposite, "uSaturation");
    sCVignette = DeckGl_Uniform(&sComposite, "uVignette");
    sCAspect = DeckGl_Uniform(&sComposite, "uAspect");
    return true;
}

void DeckPost_Shutdown(void)
{
    DeckGl_DeleteProgram(&sBright);
    DeckGl_DeleteProgram(&sCopy);
    DeckGl_DeleteProgram(&sBlur);
    DeckGl_DeleteProgram(&sComposite);
    DeckGl_DeleteTarget(&sScene);
    for (int i = 0; i < 3; i++)
        DeckGl_DeleteTarget(&sHalf[i]);
    DeckGl_TimerShutdown();
    sReady = false;
}

void DeckPost_Init(int sceneW, int sceneH)
{
    const char *force = getenv("DECK_POST_FORCE_FAIL");
    bool ok;
    sReady = false;
    sSizeWarned = false;
    if (!DeckGl_Init())
    {
        DeckGl_Log("[POST] shaders unavailable: legacy path\n");
        return;
    }
    ok = DeckGl_CreateTarget(&sScene, sceneW, sceneH, DECK_GL_DEPTH_TEXTURE24, GL_NEAREST);
    if (!ok)
    {
        DeckGl_DeleteTarget(&sScene);
        ok = DeckGl_CreateTarget(&sScene, sceneW, sceneH, DECK_GL_DEPTH_RENDERBUFFER24, GL_NEAREST);
    }
    for (int i = 0; i < 3 && ok; i++)
        ok = DeckGl_CreateTarget(&sHalf[i], sceneW / 2, sceneH / 2, DECK_GL_DEPTH_NONE, GL_LINEAR);
    ok = ok && BuildPrograms();
    if (ok && force != NULL && strcmp(force, "0") != 0)
    {
        DeckGl_Log("[POST] DECK_POST_FORCE_FAIL set: forcing the fallback\n");
        ok = false;
    }
    if (!ok)
    {
        DeckPost_Shutdown();
        DeckGl_Log("[POST] fallback: legacy diorama FBO and Pass_DepthOfField in use\n");
        return;
    }
    DeckGl_TimerInit();
    sReady = true;
    const DeckPostConfig c = DeckPost_ReadConfig();
    DeckGl_Log("[POST] ready: scene %dx%d RGBA8 depth=%s | config quality=%d shaders=%d grade=%d bloom=%d blur=%d\n",
               sScene.w, sScene.h, sScene.depth == DECK_GL_DEPTH_TEXTURE24 ? "texture24" : "renderbuffer24",
               c.quality, (int)c.shaders, (int)c.grade, (int)c.bloom, (int)c.blur);
}

bool DeckPost_WantScene(int renderW, int renderH, bool indoor)
{
    if (!sReady)
        return false;
    if (renderW != sScene.w || renderH != sScene.h)
    {
        if (!sSizeWarned)
            DeckGl_Log("[POST] render size %dx%d differs from the scene target %dx%d: legacy path\n",
                       renderW, renderH, sScene.w, sScene.h);
        sSizeWarned = true;
        return false;
    }
    const DeckPostConfig c = DeckPost_ReadConfig();
    if (!c.shaders)
        return false;
    return c.grade || c.bloom || (c.blur && !indoor);
}

void DeckPost_BindScene(void)
{
    glBindFramebuffer(GL_FRAMEBUFFER, sScene.fbo);
}

static void DrawQuad(void)
{
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, kQuad);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glDisableClientState(GL_VERTEX_ARRAY);
}

static void BindTex(int unit, GLuint tex)
{
    DeckGl_ActiveTexture(GL_TEXTURE0 + (GLenum)unit);
    glBindTexture(GL_TEXTURE_2D, tex);
}

// Bind first, then set the viewport (raw glViewport here: deck_post.c does not see the macro).
static void BeginTarget(const DeckGlTarget *t)
{
    glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    glViewport(0, 0, t->w, t->h);
}

static void BlurPass(const DeckGlTarget *dst, const DeckGlTarget *src, float stepU, float stepV)
{
    BeginTarget(dst);
    BindTex(0, src->color);
    DeckGl_UseProgram(&sBlur);
    DeckGl_Set1i(sBlurSrc, 0);
    DeckGl_Set2f(sBlurStep, stepU, stepV);
    DrawQuad();
}

static void Blur(const DeckGlTarget *t, const DeckGlTarget *tmp, int passes)
{
    for (int i = 0; i < passes; i++)
    {
        BlurPass(tmp, t, 1.0f / (float)t->w, 0.0f);
        BlurPass(t, tmp, 0.0f, 1.0f / (float)t->h);
    }
}

static void GradeTints(float daylight, float glow, float shadow[3], float high[3])
{
    const float dusk = Clamp01(glow * 4.0f) * Clamp01(daylight / 0.35f);
    const float night = Clamp01(1.0f - daylight / 0.35f);
    const float day = Clamp01(1.0f - dusk - night);
    const float sum = day + dusk + night; // >= 1 by construction
    for (int i = 0; i < 3; i++)
    {
        shadow[i] = (kShadowTint[0][i] * day + kShadowTint[1][i] * dusk + kShadowTint[2][i] * night) / sum;
        high[i] = (kHighTint[0][i] * day + kHighTint[1][i] * dusk + kHighTint[2][i] * night) / sum;
    }
}

void DeckPost_Run(const DeckPostFrame *in)
{
    const DeckPostConfig cfg = DeckPost_ReadConfig();
    const DeckPostPreset *const pr = &kPresets[cfg.quality];
    const float keep = 1.0f - Clamp01(in->fade);
    const bool doBloom = cfg.bloom && in->bloom > 0.0f && pr->bloomGain > 0.0f;
    const bool doBlur = cfg.blur && !in->indoor;
    float shadow[3], high[3];

    VitaPerf_DeckMarkPostStart();
    DeckGl_TimerBegin();
    VitaPerf_DeckSetPostGpuMs(DeckGl_TimerLastMs());

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);

    // Half-resolution reads of the scene: bilinear (one tap averages 2x2 scene texels).
    BindTex(0, sScene.color);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (doBloom)
    {
        BeginTarget(&sHalf[0]);
        DeckGl_UseProgram(&sBright);
        DeckGl_Set1i(sBrightScene, 0);
        DeckGl_Set1f(sBrightThreshold, pr->threshold);
        DrawQuad();
        Blur(&sHalf[0], &sHalf[1], pr->bloomPasses);
    }
    if (doBlur)
    {
        BindTex(0, sScene.color);
        BeginTarget(&sHalf[2]);
        DeckGl_UseProgram(&sCopy);
        DeckGl_Set1i(sCopyScene, 0);
        DrawQuad();
        Blur(&sHalf[2], &sHalf[1], 1);
    }

    // Composite into the letterbox: same viewport call as the legacy present (DeckGlViewport maps
    // (0,0,960,544) on framebuffer 0 to the letterboxed rect and enables the scissor).
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    DeckGlViewport(0, 0, in->renderW, in->renderH);
    BindTex(2, doBlur ? sHalf[2].color : 0);
    BindTex(1, doBloom ? sHalf[0].color : 0);
    BindTex(0, sScene.color);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); // pixel art stays sharp
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    GradeTints(in->daylight, in->glow, shadow, high);
    DeckGl_UseProgram(&sComposite);
    DeckGl_Set1i(sCScene, 0);
    DeckGl_Set1i(sCBloom, 1);
    DeckGl_Set1i(sCBlur, 2);
    DeckGl_Set1f(sCBloomAmt, doBloom ? in->bloom * pr->bloomGain * keep : 0.0f);
    DeckGl_Set1f(sCBlurOn, doBlur ? 1.0f : 0.0f);
    // Legacy band height: VOXEL_DOF_*_ROWS * (renderH / 544) / renderH = rows / 544.
    DeckGl_Set4f(sCDof, in->dofTopRows / 544.0f, in->dofBottomRows / 544.0f, in->dofTopAlpha, in->dofBottomAlpha);
    DeckGl_Set1f(sCGradeAmt, cfg.grade ? pr->grade * keep : 0.0f);
    DeckGl_Set3f(sCShadow, shadow[0], shadow[1], shadow[2]);
    DeckGl_Set3f(sCHigh, high[0], high[1], high[2]);
    DeckGl_Set1f(sCSat, pr->saturation);
    DeckGl_Set1f(sCVignette, cfg.grade ? pr->vignette * keep : 0.0f);
    DeckGl_Set1f(sCAspect, (float)in->renderW / (float)in->renderH);
    DrawQuad();

    // Leave the state Pass_DepthOfField leaves (see deck_post.h).
    DeckGl_UseProgram(NULL);
    BindTex(2, 0);
    BindTex(1, 0);
    BindTex(0, 0);
    glEnable(GL_TEXTURE_2D);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 1.0, 1.0, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_BLEND);

    DeckGl_TimerEnd();
    VitaPerf_DeckMarkPostEnd();
}
```

Notes for the implementer: the composite runs only with `postActive`, which requires `fadeAmount < 0.99` (Step 6), as the legacy DoF does (`voxel_renderer.c:4742`). The quads drawn here do not go through the `glDrawArrays` counting macro of `voxel_renderer.c:30-39` (that macro is local to that file), so `draw_calls` in the CSV keeps meaning "3D passes".

- [ ] **Step 3: Includes and Init/Shutdown hooks in `voxel_renderer.c`**

After `#include "../vita/vita_perf.h"` (line 28):

```c
#ifdef PLATFORM_DECK
#include "../deck/deck_gl_shader.h"
#include "../deck/deck_post.h"
#endif
```

In `VoxelRenderer_Init`, after `sDioramaFboReady = (sDioramaFbo != 0 && sScratchFbo != 0);` (line 3657):

```c
#ifdef PLATFORM_DECK
    // RGBA8 scene target and post chain (deck_post.c); the legacy FBOs above stay the fallback.
    DeckPost_Init(VOXEL_DIORAMA_FBO_W, VOXEL_DIORAMA_FBO_H);
#endif
```

In `VoxelRenderer_Shutdown`, after `sDioramaFboReady = false;` (line 3725):

```c
#ifdef PLATFORM_DECK
    DeckPost_Shutdown();
#endif
```

- [ ] **Step 4: Frame state field**

In `VoxelFrame`, after `bool dioramaActive;` (line 3854):

```c
#ifdef PLATFORM_DECK
    bool postActive;                    // the 3D scene goes to the Deck RGBA8 target (deck_post.c)
#endif
```

- [ ] **Step 5: Render target choice in `Frame_BeginPasses`**

Replace lines 4744-4751 (the `if (dioramaActive) { glBindFramebuffer(... sDioramaFbo); } else { glBindFramebuffer(..., 0); }` block) with:

```c
#ifdef PLATFORM_DECK
    {
        // Per-frame shader uniforms (uTime, uWind = dirX, dirZ, strength); sway shaders use them in phase 2.
        const float amount = f->weatherAmount < 0.0f ? 0.0f : (f->weatherAmount > 1.0f ? 1.0f : f->weatherAmount);
        float windStrength = 0.25f;
        if (f->weather == VOXEL_WEATHER_RAIN)
            windStrength = 0.6f + 0.4f * amount;
        else if (f->weather == VOXEL_WEATHER_SAND)
            windStrength = 0.8f + 0.2f * amount;
        DeckGl_SetFrameUniforms(f->seconds, 0.8f, 0.6f, windStrength);
    }
    // Field only (Decision 12); the FBO path no longer depends on the DoF setting.
    f->postActive = !f->inBattle3D && (fadeAmount < 0.99f) && DeckPost_WantScene(renderW, renderH, isIndoor);
    if (f->postActive)
    {
        DeckPost_BindScene();
    }
    else
#endif
    if (dioramaActive)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, sDioramaFbo);
    }
    else
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
```

The `glViewport(0, 0, renderW, renderH)` that follows (line 4754) then sees the scene FBO bound and passes through without scissor (`deck_video.c:184,195-196`). `f->weather`, `f->weatherAmount` and `f->seconds` are filled by `Frame_ReadAtmosphere` (`voxel_renderer.c:4263-4272`), which runs before `Frame_BeginPasses` (`voxel_renderer.c:5777,5781`).

- [ ] **Step 6: `Pass_DeckPost` and the call site**

After the closing brace of `Pass_DepthOfField` (line 5475):

```c
#ifdef PLATFORM_DECK
// Deck post chain on the RGBA8 scene target; replaces Pass_DepthOfField when f->postActive.
static void Pass_DeckPost(const VoxelFrame *const f)
{
    const bool isIndoor = (f->inst0 != NULL && f->inst0->indoor); // same test as Frame_BeginPasses
    const VitaDayNightLight *const dn = VitaDayNight_Get();
    DeckPostFrame in;
    in.renderW = f->renderW;
    in.renderH = f->renderH;
    in.indoor = isIndoor;
    in.fade = f->fadeAmount;
    in.bloom = f->light.bloom;
    in.daylight = (isIndoor || dn == NULL) ? 1.0f : dn->daylight;
    in.glow = (isIndoor || dn == NULL) ? 0.0f : dn->glow;
    in.dofTopRows = VOXEL_DOF_TOP_ROWS;
    in.dofBottomRows = VOXEL_DOF_BOTTOM_ROWS;
    in.dofTopAlpha = VOXEL_DOF_TOP_ALPHA;
    in.dofBottomAlpha = VOXEL_DOF_BOTTOM_ALPHA;
    DeckPost_Run(&in);
}
#endif
```

In `VoxelRenderer_RenderFrame` replace lines 5817-5818 with:

```c
#ifdef PLATFORM_DECK
    if (f.postActive)
        Pass_DeckPost(&f);
    else
#endif
    if (f.dioramaActive)
        Pass_DepthOfField(&f);
```

- [ ] **Step 7: Screenshot statistics helper**

`tools/deck_tests/shot_stats.py` (runs in the container, Pillow is installed there, `progress.md:19`):

```python
#!/usr/bin/env python3
# usage: shot_stats.py a.png b.png
# Prints the mean RGB of two screenshots and B minus A, to check that a path change keeps colours.
import sys
from PIL import Image, ImageStat

a, b = (Image.open(p).convert("RGB") for p in sys.argv[1:3])
ma, mb = ImageStat.Stat(a).mean, ImageStat.Stat(b).mean
print("A mean", [round(x, 2) for x in ma])
print("B mean", [round(x, 2) for x in mb])
print("B-A   ", [round(y - x, 2) for x, y in zip(ma, mb)])
```

- [ ] **Step 8: Build and test the decoupled path and the fallback**

`SYNC_BUILD`. Expected: no compile error or warning in the touched files. Then four short runs (each: `DECK_PS` empty, `DECK_PLAY '<env>'` in the background, Marcos presses Continue and stands still at the save spot, `DECK_SHOT <label>`, `DECK_PS`, `DECK_KILL <pid>`). Settings with `setkey.sh` before each run:

| Run | settings | env | label |
|---|---|---|---|
| A | `voxel_shaders 0`, `voxel_blur 0` | `DECK_GL_DEBUG=1` | `p1t4-direct` |
| B | `voxel_shaders 1`, `voxel_blur 0`, `voxel_bloom 0`, `voxel_grade 1` | `DECK_GL_DEBUG=1` | `p1t4-post-grade` |
| C | `voxel_shaders 1`, `voxel_blur 1`, `voxel_bloom 2`, `voxel_grade 2` | `DECK_GL_DEBUG=1 DECK_POST_FORCE_FAIL=1` | `p1t4-fallback` |
| D | `voxel_shaders 0`, `voxel_blur 1` | `DECK_GL_DEBUG=1` | `p1t4-legacy-dof` |

After each run: `DECK_RUN 'grep -E "\[(POST|FBO|SHADER)\]|GLERR" build-deck/dist/userdata/gl_errors.log | tail -12'`.

Expected: A and D log `[POST] ready` (the target exists) but draw the old way. B logs `[FBO] 960x544 RGBA8 depth=texture24: status 0x8cd5 COMPLETE` (or the renderbuffer24 line after an INCOMPLETE texture24 line), three `480x272 ... COMPLETE` lines, four `linked` lines, `[POST] ready ...`, no `GLERR` line, and the picture shows the scene through the composite (graded colours, vignette in the corners, nothing blurred). C logs `[POST] DECK_POST_FORCE_FAIL set` and `[POST] fallback`, and its picture equals D (legacy bands). Copy the shots into the container and compare C with D:

```bash
ssh deck 'ls /run/media/deck/SD512/emerald-deck/shots/ | grep p1t4'
DECK_RUN 'python3 tools/deck_tests/shot_stats.py ../shots/<p1t4-legacy-dof file> ../shots/<p1t4-fallback file>'
```

Expected: C minus D within 1.0 on each channel (same path; differences come only from animated water and motes). A against B: B is the graded picture, so the means differ; check by eye with Marcos that nothing is blurred, the 2D overlay and menus are not graded, and the letterbox bars stay black. Note: without DoF, the legacy direct path renders the 3D scene straight into the 1280x720 letterbox, while every FBO path (legacy DoF and post) renders at 960x544 and scales up with nearest filtering; B is therefore slightly less sharp than A by design (higher internal resolution is phase 7, G5). Then open a door to warp (fade to black) and check the fade is not tinted.

Restore: `voxel_shaders 1`, `voxel_blur 1`, `voxel_bloom 2`, `voxel_grade 2`, `voxel_quality 1`.

- [ ] **Step 9: Opus review, then commit**

Opus reads the diff against the checklist of Task 8 items 1 to 6 before the commit. Then:

```bash
git add src/platform/deck/deck_post.c src/platform/deck/deck_post.h src/platform/voxel/voxel_renderer.c tools/deck_tests/shot_stats.py
git commit -m "feat(deck): RGBA8 scene target and post chain, FBO path decoupled from DoF" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Bloom, time-of-day grade and vignette, tuned per preset

**Agent:** Sonnet, with Marcos judging the look (Medium faithful plus, High strong).

**Files:**
- Modify: `src/platform/deck/deck_post.c` (only `kPresets`, `kShadowTint`, `kHighTint` values)
- Modify: `docs/DECK_NOTES.md`

**Interfaces:**
- Consumes: Task 4. Bloom strength per weather comes from `BaseLightFor` (`voxel_renderer.c:2803-2905`: `light.bloom = 0.10f` at 2821, `0.08f + 0.30f * bolt` at 2830, `0.12f` at 2881 and 2889, `0.0f` at 2903).

- [ ] **Step 1: Read which weathers have bloom**

Run: `grep -n "case VOXEL_WEATHER\|light.bloom" src/platform/voxel/voxel_renderer.c | sed -n '1,40p'` and read lines 2803-2905. Write in the report which weather classes give `light.bloom > 0` (only those show bloom; `DeckPost_Run` skips the bloom passes when it is 0).

- [ ] **Step 2: Shots per preset**

For `voxel_quality` 0, 1, 2 (setkey, one run each): Continue, stand at the save spot, `DECK_SHOT p1t5-q<N>-spot1`; then walk to an outdoor spot with sunny weather if the save allows it, `DECK_SHOT p1t5-q<N>-sun`. Spec spots (Littleroot noon, Route 101 dusk, Route 119 rain, Petalburg night) are used where the save and the clock allow; the in-game clock follows local time (`VitaDayNight_SyncToConsole`, `vita_daynight.h:63`), so record the time of each shot.

- [ ] **Step 3: Tune (one value at a time)**

With Marcos looking at the shots: adjust one field of one preset row per round (for example Medium `bloomGain` 3.0 -> 2.5), rebuild, re-shoot the same spot. Stop when Marcos accepts Medium and High. Constraints: Low never has bloom (`bloomGain` 0); grade strength of Medium no more than 0.85 (faithful plus).

- [ ] **Step 4: Fade and flash checks**

Warp through a door (fade to black) and, if reachable, use an action that fades to white (Marcos chooses one he can trigger, for example a field move): no tint on the full fade. In rain with lightning, the bloom spike follows `bolt` (0.30 extra bloom at the flash). Night: shadows bluish, no crushed blacks in the 2D overlay (it is drawn after the post and is untouched).

- [ ] **Step 5: Commit**

Write the final values and the shot file names into `docs/DECK_NOTES.md` under `## Phase 1: post presets`.

```bash
git add src/platform/deck/deck_post.c docs/DECK_NOTES.md
git commit -m "tune(deck): bloom, grade and vignette presets" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Smooth separable DoF in place of the 1/4-res bands

**Agent:** Sonnet.

**Files:**
- Modify: `src/platform/deck/deck_post.c` only if a check below fails (blur passes count or kernel step)

**Interfaces:**
- Consumes: Task 4. Band geometry comes from `VOXEL_DOF_TOP_ROWS` 226, `VOXEL_DOF_BOTTOM_ROWS` 127, `VOXEL_DOF_TOP_ALPHA` 0.82, `VOXEL_DOF_BOTTOM_ALPHA` 0.68 (`voxel_tuning.h:89-92`), the same values `Pass_DepthOfField` uses (`voxel_renderer.c:5410-5418`).

- [ ] **Step 1: Before/after pair**

Same spot, two runs: `voxel_shaders 0` + `voxel_blur 1` (`DECK_SHOT p1t6-bands`), then `voxel_shaders 1` + `voxel_blur 1` (`DECK_SHOT p1t6-smooth`). Copy both to this PC (`scp deck:/run/media/deck/SD512/emerald-deck/shots/p1t6-* .superpowers/sdd/2026-11-13-deck-visual-phase1/`) and compare the top band side by side.

Expected: the bands shot shows 4x4 blocks in the top band; the smooth shot shows no blocks and the same band extent (blur starts at the same row, strongest at the edge).

- [ ] **Step 2: Rules kept**

Indoors (enter a house): no blur in either path. In a 3D battle (`voxel_battle=1`): the legacy path draws (post is field only), check `gl_errors.log` has no new lines and the battle looks as before.

- [ ] **Step 3: Cost**

One run of the fixed route with `voxel_quality 1`; save to `../perf/p1-t6` and run `perf_report.sh ../perf/p1-t6`. Expected: `post GPU ms` line present (or `n/a` if the timer extension is missing, recorded as such). If post GPU average is above 1.0 ms, report before changing anything.

- [ ] **Step 4: Commit (only if Step 1 or 2 needed a change)**

```bash
git add src/platform/deck/deck_post.c
git commit -m "fix(deck): smooth DoF band match" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Smoke test, screenshot script, perf compare

**Agent:** Haiku for the runs; Sonnet writes the script and the results file.

**Files:**
- Create: `tools/deck/shots.sh`
- Modify: `tools/deck_tests/smoke.sh` (shader/post log check), `docs/results/deck-visual-phase1-perf.txt`, `docs/DECK_NOTES.md`

**Interfaces:**
- Produces: `tools/deck/shots.sh <label>` (Deck host, prints the saved path), smoke check that fails on `[SHADER] ... FAILED`.

- [ ] **Step 1: Screenshot script**

`tools/deck/shots.sh` (runs on the Deck host; `spectacle` flags from `docs/DECK_NOTES.md:117`):

```sh
#!/bin/sh
# usage: shots.sh <label>   (Deck host, not the container). Full-screen PNG into SD512/emerald-deck/shots.
dir=/run/media/deck/SD512/emerald-deck/shots
mkdir -p "$dir" || exit 1
out="$dir/$1-$(date +%Y%m%d-%H%M%S).png"
DISPLAY=:0 XAUTHORITY=$(ls /run/user/1000/xauth_* | head -1) spectacle -b -n -f -o "$out" || exit 1
echo "$out"
```

Test: game running, `ssh deck 'sh /run/media/deck/SD512/emerald-deck/src/tools/deck/shots.sh test'` prints a path; `ssh deck 'ls -l <path>'` shows a non-empty file. If `spectacle` fails without the two variables or with them, record the working form in `docs/DECK_NOTES.md`.

- [ ] **Step 2: Smoke test extension**

In `tools/deck_tests/smoke.sh`, before the final `echo "PASS: ran 20 s without exiting"`, add:

```sh
if grep -q "\[SHADER\].*FAILED" userdata/gl_errors.log 2>/dev/null; then
    echo "FAIL: shader build failed"; grep "\[SHADER\]" userdata/gl_errors.log | tail -5; exit 1
fi
grep -E "\[SHADER\] self-test|\[POST\] (ready|fallback)" userdata/gl_errors.log 2>/dev/null | tail -2
```

Run (no game running): `DECK_RUN 'DISPLAY=:0 XAUTHORITY=\$(ls /run/user/1000/xauth_*) XDG_RUNTIME_DIR=/run/user/1000 sh tools/deck_tests/smoke.sh'`. Expected: the self-test line, then `PASS`. The `[POST]` line appears only when the voxel renderer is initialised during the 20 s.

- [ ] **Step 3: Perf compare on the fixed route (Marcos plays)**

Four runs, same route, `voxel_fps 1`, each saved to its own directory and reported:

| Run | settings | dir |
|---|---|---|
| legacy | `voxel_shaders 0`, `voxel_blur 1` | `../perf/p1-legacy` |
| low | `voxel_shaders 1`, `voxel_quality 0` | `../perf/p1-low` |
| medium | `voxel_shaders 1`, `voxel_quality 1` | `../perf/p1-medium` |
| high | `voxel_shaders 1`, `voxel_quality 2` | `../perf/p1-high` |

```bash
DECK_RUN 'mkdir -p ../perf/<run> && cp build-deck/dist/userdata/perf_frames.csv build-deck/dist/userdata/perf_summary.txt ../perf/<run>/ && sh tools/deck/perf_report.sh ../perf/<run>'
```

Expected: the legacy run matches the Task 1 baseline within noise (frame ms p99 within 1 ms). Pass criteria for phase 1: High has frame ms p99 under 14 ms and present over 17.5 ms not higher than legacy by more than 1 percentage point; post GPU ms recorded for low/medium/high. Append all four outputs to `docs/results/deck-visual-phase1-perf.txt` under `== phase 1 compare ==` with a short table (avg, p99, present p50/p99, post CPU, post GPU).

- [ ] **Step 4: Before/after shots at the fixed spots**

At each reachable spot: `voxel_shaders 0` shot and `voxel_quality 1` shot (and `voxel_quality 2`), labels `p1-<spot>-legacy`, `p1-<spot>-medium`, `p1-<spot>-high`. Copy them to this PC: `scp deck:/run/media/deck/SD512/emerald-deck/shots/p1-* .superpowers/sdd/2026-11-13-deck-visual-phase1/`. List the files and which spec spots were not reachable with the current save.

- [ ] **Step 5: Commit**

```bash
git add tools/deck/shots.sh tools/deck_tests/smoke.sh docs/results/deck-visual-phase1-perf.txt docs/DECK_NOTES.md
git commit -m "test(deck): phase 1 smoke check, screenshot script, perf compare" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Review gate

**Agent:** Opus (GL correctness), report to the orchestrator; Marcos approves the look.

**Files:** none changed by the reviewer; findings go back to a Sonnet fix round (max 3).

- [ ] **Step 1: Checklist**

1. Guards: `git diff <phase1-base>..HEAD -- src/platform/voxel src/platform/vita include` shows every added line inside `#ifdef PLATFORM_DECK` except the unchanged-behaviour edits in `vita_perf.c` (the split `fputc('\n')` row end on the Vita side and the `PERF_CSV_DECK_COLUMNS ""` header). No `!PLATFORM_VITA`, no `#ifndef PLATFORM_VITA`.
2. Bind-then-viewport: every `glBindFramebuffer` to a target in `deck_post.c` is followed by a raw `glViewport` of that target's size; the composite binds 0 and calls `DeckGlViewport(0, 0, renderW, renderH)`; in `voxel_renderer.c` `DeckPost_BindScene` runs before the macro `glViewport` at line 4754.
3. State on exit of `DeckPost_Run` matches `Pass_DepthOfField`'s exit (`voxel_renderer.c:5377-5474`) plus: program 0, unit 0 active, units 1 and 2 unbound, `GL_TEXTURE_2D` enabled on unit 0, `GL_ARRAY_BUFFER` 0.
4. Sampler feedback: no pass samples the texture of the target it renders into (bright: scene -> half0; blur: half0 <-> half1, half2 <-> half1; copy: scene -> half2; composite: scene/half0/half2 -> 0).
5. Fallback: shaders unavailable, any target incomplete, a program failing, `DECK_POST_FORCE_FAIL`, a render size other than 960x544 (`voxel_res`, `sdl2.c:155-164`), `voxel_shaders=0`, battles and full fades all end in the legacy path with no GL error.
6. Fades: grade, vignette and bloom scale with `1 - fadeAmount`; DoF follows the legacy rule (only below 0.99 fade, never indoors).
7. Lifetime: `DeckPost_Shutdown` frees every target, program and query; a second `VoxelRenderer_Init` after `VoxelRenderer_Shutdown` works (programs rebuilt, `DeckGl_Init` is per context and does not reload).
8. Telemetry: CSV header and rows have the same column count (`DECK_RUN 'awk -F, "{print NF}" build-deck/dist/userdata/perf_frames.csv | sort | uniq -c'` prints one count); `perf_report.sh` works on the Task 1 baseline CSV (no present column) and on a new one.
9. Settings: out-of-range values (`voxel_quality=7`, `voxel_bloom=-1`) fall back to Medium / auto without a crash (one run each, check the `[POST] ready ... config` line).
10. Vita: if a VitaSDK toolchain is available, configure and build the Vita target and confirm it links; if none is available, state that in the report (spec risk 10) and list the shared-file hunks for a later Vita build.
11. Budget: Task 7 Step 3 pass criteria met, or the failing number is reported.
12. Exit test of the spec phase 1: before/after shots exist, baseline and new perf reports exist, `voxel_shaders=0` takes the old code path (Task 4 runs A and D).

- [ ] **Step 2: Verdict**

Report PASS or a list of findings with file:line. Fix rounds by Sonnet (max 3), then Opus re-checks the changed items only.

---

## Self-review

- **Spec coverage (phase 1 row of section 5):** shader foundation A1 with embedded strings, compile/link logs and `uTime`/`uWind` (Task 2, uniforms set per frame in Task 4 Step 5); RGBA8 scene buffer with completeness check that acts (texture24 -> renderbuffer24 -> legacy fallback, Task 2 `DeckGl_CreateTarget`, Task 4 `DeckPost_Init`); FBO path decoupled from DoF (`DeckPost_WantScene`, Task 4 Step 5); composite with bloom from `light.bloom`, time-of-day grade curves, vignette (Task 4, tuned in Task 5); G1 smooth separable blur (Task 4 code, Task 6 check); settings keys and presets skeleton with Medium default (Task 3); baseline and new perf report (Tasks 1 and 7); before/after screenshots (Tasks 4, 6, 7); `voxel_shaders=0` gives the old path (Task 4 runs A/D, Task 8 item 12); Vita unchanged (guards, Task 8 items 1 and 10); depth texture for later DoF (spec 3.b note, Task 2/4); grade versus fades (spec risk 7, Task 4 `keep`); field only (Decision 12). The LUT grade (B2) is left as procedural curves, as section 3.b allows for phase 1; B2 LUTs, G2 and G5 are phase 7.
- **Placeholder scan:** no TBD or TODO. Tuning values in `kPresets` and the tint tables are explicit starting values with a stop rule (Task 5 Step 3). `<pid>`, `<label>`, `<run>` and `<file>` in commands are run-time values read from the previous command's output.
- **Name consistency:** `DeckGl_Init`, `DeckGl_SelfTest`, `DeckGl_BuildProgram`, `DeckGl_UseProgram`, `DeckGl_SetFrameUniforms`, `DeckGl_CreateTarget`, `DeckGl_Timer*` are declared in Task 2 Step 1 and used with the same signatures in Task 4. `DeckPost_Init/Shutdown/ReadConfig/WantScene/BindScene/Run` match between `deck_post.h` and the hooks. `FAST_EXTRA_VOXEL_QUALITY/SHADERS/GRADE/BLOOM` match between Task 3 and `DeckPost_ReadConfig`. `VitaPerf_DeckMarkPostStart/End` and `VitaPerf_DeckSetPostGpuMs` match between Task 1 and `DeckPost_Run`. CSV names `present_ms`, `post_ms`, `post_gpu_ms` match between `vita_perf.c` and `perf_report.sh`.
- **UNVERIFIED, each with its verification step first:** (1) GLSL 1.20 and the GL 2.0 / timer entry points through `SDL_GL_GetProcAddress` on this Mesa context; the `GL_VERSION` string was logged but never recorded (Task 1 Step 3/9, Task 2 Step 4 self-test). (2) Whether swap interval 1/2 paces presents under the Deck desktop session (`swap_ms` 0.54 ms in stage 2a; return values never checked) (Task 1 Steps 5, 7, 8). (3) Completeness of RGBA8 + `GL_DEPTH_COMPONENT24` texture targets on radeonsi; only RGBA5551 + DEPTH16 is recorded COMPLETE (`stage2a-report.md:15`) (Task 4 Step 8 run B). Also: the exact `PFNGL...PROC` names in the installed headers (Task 2 Step 2 grep), `spectacle` over SSH needing `DISPLAY`/`XAUTHORITY` (Task 7 Step 1), reachability of the four spec screenshot spots with the current save (Task 5 Step 2, Task 7 Step 4), whether `VitaDayNight_Get` can return NULL (guarded in `Pass_DeckPost`), and where a Vita build can run (Task 8 item 10).
