# Steam Deck visual upgrade (HD-2D look): design

Date: 2026-11-13
Status: draft for review by Marcos. No code has been written for this plan.
Branch: `deck-port`. Scope: Steam Deck build only (`PLATFORM_DECK`); the Vita build must not change.

Citations are `file:line` in this tree as of commit `b0fa638`. Anything not read in the code or
docs is marked **UNVERIFIED**. All costs in this document are estimates until measured with
`tools/deck/perf_report.sh` on the Deck; no Deck frame-time baseline has been recorded yet
(`docs/results/stage2-baseline.txt`, asked for by the stage 2 plan, does not exist).

---

## 1. Summary and recommended direction

**Target look.** The GBA pixel art stays exactly as drawn (nearest filtering, 1-bit alpha
cut-outs, the same tiles and sprites). What changes is everything around it, the way Octopath
Traveler and Bravely Default II frame their sprites: warm directional sunlight with soft bloom
on bright surfaces, a time-of-day colour grade, a real tilt-shift depth of field with smooth
bokeh-like blur instead of the current blocky bands, vignette, grass and tree crowns that move in
the wind, tall grass and flowers that stand up out of the ground and part around the player,
denser rain with splashes and wet ground, drifting pollen, leaves and fireflies, and an ambient
sound bed (birds by day, insects at night, wind, rain, water) under the music.

**Approach.** One small shader foundation (GLSL 1.20 programs on the existing compatibility
context, attached only to the passes we upgrade) carries almost every effect. The vertex format
and VBO layout stay as they are: the renderer's arrays already map one-to-one to `gl_Vertex`,
`gl_MultiTexCoord0` and `gl_Color`, and the one free byte (vertex alpha, always 255 on chunk
geometry) carries per-vertex flags such as sway weight. Passes we do not touch stay
fixed-function. Every effect has a `settings.txt` switch and belongs to a Low / Medium / High
preset.

**Recommended order** (details in section 5):

1. Shader foundation plus post chain quick win: RGBA8 scene buffer, bloom (the renderer already
   computes a per-weather bloom amount it never uses), colour grade, vignette.
2. Wind sway for tree crowns and the existing long grass.
3. Ambient audio bed (independent of the GPU work, can run in parallel with 2).
4. Standing tall grass and flowers, with player bend.
5. Rain upgrade: GPU-driven streaks, splashes, wet ground, mist.
6. Particles: pollen, leaves, fireflies, water glints in one GPU-driven system.
7. HD depth of field (depth-based, separable) and a higher 3D internal resolution.
8. Optional, decided later: real-time sun shadow map.

**Deliberately skipped or deferred:**

- Real-time shadow maps (phase 8, optional): the terrain's sun shadows are already ray-traced
  into vertex colours at chunk build (`voxel_lighting.c`), so a shadow map would duplicate them
  and force a rework of the lighting cache. Highest risk for the smallest visible gain.
- Screen-space reflections, SSAO, normal-mapped pixel art, HDR float buffers: no normals exist in
  the vertex format, the art is not authored for them, and the contact AO is already baked.
- Any Vita work: effects are behind `PLATFORM_DECK`; the Vita keeps today's look.
- Copyrighted audio of any kind (no Pokémon game audio, no audio ripped from other games).
- New 3D models: everything stays cards, tufts and billboards made from the existing art.

---

## 2. Current state per topic

### 2.1 Renderer foundation

- Context: SDL2 desktop GL, compatibility profile, version 2.1 requested
  (`src/platform/deck/deck_video.c:111-113`), swap interval 1 (`deck_video.c:130`). The GL
  version Mesa actually returns is logged to `userdata/debug.log` (`deck_video.c:109-148`) but
  no recorded value exists in the docs: **UNVERIFIED** (Mesa radeonsi normally returns a 4.6
  compatibility context when 2.1 is asked for).
- Build: the Deck defines both `PLATFORM_VITA` and `PLATFORM_DECK` (`CMakeLists.txt:65,79-81`)
  and compiles with `-DGL_GLEXT_PROTOTYPES` and the compat headers (`CMakeLists.txt:119`), so
  `glBindFramebuffer` and friends already link directly. Whether `glCreateShader` and the other
  GL 2.0 entry points link the same way is **UNVERIFIED**; the loader will fetch them through
  `SDL_GL_GetProcAddress` to avoid depending on it.
- One vertex format for everything: `VoxelGlVertex` = 3 floats position, 2 floats UV, 4 bytes
  RGBA, 24 bytes (`src/platform/voxel/voxel_renderer.c:87-92`). Chunk passes bind it with
  `glVertexPointer` / `glTexCoordPointer` / `glColorPointer` from the chunk VBO
  (`voxel_renderer.c:4849-4852`, `4882-4885`); per-frame passes use client arrays
  (`sBatchGlVerts`, `voxel_renderer.c:371-372`).
- Chunk vertex alpha is always 255 (`PackVoxelVertices`, `voxel_renderer.c:769-782`, line 780):
  the alpha byte of chunk geometry is free. Opaque passes draw without blend or alpha test
  (`Pass_SetupWorld3D`, `voxel_renderer.c:4785-4799`); cut-out passes use
  `glAlphaFunc(GL_GREATER, 0)` (`voxel_renderer.c:4810-4812`, `4871-4872`), so any non-zero
  vertex alpha still draws the same in fixed-function.
- Chunk crossings: chunk geometry stays where it was built and a per-chunk translation is pushed
  on the modelview (`ChunkOffsets_Apply`, `voxel_renderer.c:1283-1295`). A shader that needs a
  stable world position (sway phase) must add that offset as a uniform.
- Frame order: `VoxelRenderer_RenderFrame` (`voxel_renderer.c:5767-5823`): ground, trees,
  buildings, cast shadows, reflections, entities, day/night multiply, lamps, 3D atmosphere,
  2D atmosphere, then depth of field, then the GBA overlay and the new UI.
- Projection is fixed to a 960/544 aspect (`voxel_renderer.c:4759`); the 960x544 picture is
  letterboxed into the 1280x800 window by the `glViewport` macro
  (`src/platform/deck/compat/vitaGL.h:12`, `DeckGlViewport` at `deck_video.c:180`), fitted 16:9
  and centred (`deck_video.c:214` onwards), i.e. scaled by about 1.33 with nearest filtering.

### 2.2 Sunlight and shadows

- Sun direction and strength come from the in-game clock (`VitaDayNight_GetSun`,
  `include/platform/vita_daynight.h:41-45`). When the sun moves, `VoxelLighting_SetSun` bumps the
  world epoch and the chunks are rebuilt progressively (`voxel_renderer.c:4222-4232`).
- Terrain sunlight, cast shadows of trees, buildings and relief, and contact AO are ray-traced on
  the CPU per vertex during chunk builds and baked into vertex colours (`voxel_lighting.c`,
  caches at `voxel_lighting.c:22-60`; "Lighting is most of a build ... ~6 ms on average and up to
  ~35 ms on mountains" on the Vita, `docs/VOXEL_ENGINE.md` "Chunks").
- Entities get flat decal/cast silhouettes in one colour through `GL_COMBINE`
  (`Pass_CastShadows`, `voxel_renderer.c:4979-5011`).
- Weather and time of day: `VoxelLight` holds sun/shade tints, haze, dapple, rays, **bloom** and
  motes (`voxel_renderer.c:408-418`), set per weather in `BaseLightFor`
  (`voxel_renderer.c:2803-2905`). `light.bloom` is interpolated (`voxel_renderer.c:2765`) but no
  field pass reads it; only the swoop transition has its own bloom (`voxel_swoop.c:127-190`).
- Day/night tint: one full-screen multiply quad (`Pass_DayNightGrade`, `voxel_renderer.c:5092-5141`);
  moonlight and fades are merged into one full-screen quad (`Pass_Atmosphere2D`,
  `voxel_renderer.c:5217-5322`). Haze is baked into vertex colours on the CPU (comment at
  `voxel_renderer.c:784-785`).
- Sun rays: four fixed screen-space diagonal shafts with a slow drift (`DrawRays`,
  `voxel_renderer.c:3306` onwards), drawn only when `light.rays > 0` (sunny weather).
- Colour grade: a fixed "slightly more saturated GBA art" grade baked at pack time
  (`voxel_grade.c`, `BakeGradedVertexColor` called at `voxel_renderer.c:779`).

### 2.3 Weather

- Weather classes: clear, sun, rain, fog, particles (snow), shade, sand, ash
  (`src/platform/voxel/voxel_world.h:143-149`).
- Rain: 56 to 96 flat-colour streak quads, velocity (-2.2, -15, 0.6), width 0.035, length 1.05
  (`sRainKind`, `voxel_renderer.c:450-453`), hard cap of 128 particles for every kind
  (`voxel_renderer.c:3101`), generated on the CPU each frame from 256 seeds wrapped in a box
  around the camera (`DrawWeather`, `voxel_renderer.c:3093-3223`; spans in
  `voxel_tuning.h:70-77`). Untextured, alpha-blended, no depth write. No splashes, no ground
  wetness, no ripples. Lightning: a full-screen additive flash (`voxel_renderer.c:5251` onwards,
  inside `Pass_Atmosphere2D`). Rain also lowers sun tint and raises haze
  (`voxel_renderer.c:2823-2830`).
- Fog and sand banks: textured sheets (`DrawFogBanks`, `voxel_renderer.c:3026-3090`;
  `voxel_tuning.h:59-66`).

### 2.4 Sway

- Nothing moves today. Tree crowns are fixed world-space cards leant back 50 degrees
  (`EmitCrownCard`, `src/platform/voxel/voxel_tree.c:401` onwards); small trees and long grass use
  one card on their cell (`EmitSmallCell`, `voxel_tree.c:318` onwards). They are emitted in the tree
  phase of the chunk build (`voxel_renderer.c:2242-2277`) and drawn by `Pass_Trees`
  (`voxel_renderer.c:4866-4888`) with the tree atlas (`trees.rgba5551`, 128x128, 1-bit alpha,
  `voxel_tree.h:6-7`).

### 2.5 Tall grass and flowers

- Long grass (Route 119/120) is already a standing card: `METATILE_General_LongGrass` 0x015 maps
  to `VOXEL_TREE_GRASS` (`voxel_tree.c:200`), plus Fortree's long grass at 0x208 and 0x281-0x283
  (`voxel_tree.c:167-170`; labels in `include/constants/metatile_labels.h:195-201`). Its tuft is
  16x32 texels at (80, 32) of the tree atlas (comment, `voxel_tree.c:314-317`).
- Ordinary tall grass is drawn flat as ground: `METATILE_General_TallGrass` 0x00D and its tree
  variants 0x1C6, 0x1C7, 0x025 (`metatile_labels.h:240-243`; `voxel_tree.c:250,256` maps them to
  0x00D as ground). Tall grass is identified by behaviour elsewhere in the port:
  `MetatileBehavior_IsTallGrass(behavior) || MetatileBehavior_IsLongGrass(behavior)`
  (`voxel_battle.c:140`), behaviour read with `UNPACK_BEHAVIOR(GetMetatileAttribute(...))`
  (`voxel_world.c:756`).
- Flowers are flat animated ground. The General tileset's flower animation writes 4 tiles at BG
  tile 508 (`src/tileset_anims.c:658-662`); Mauville and Ever Grande have their own flower
  animations (`tileset_anims.c:56,64`, tile offsets **UNVERIFIED**). There is no flower metatile
  label in `metatile_labels.h` (the only "Flowers" labels are secret-base decorations,
  `metatile_labels.h:604-607`). The atlas already finds the metatiles that show animated tiles
  (`VoxelAnim_*`, `voxel_atlas.h:166-185`) and has two precedents for composing partial
  metatiles: "ground variants" without the upper layer and "cut tiles" with a clear background
  (`voxel_atlas.h:58-72`).

### 2.6 Particles

- Dust motes: 32 fixed motes (`VOXEL_MOTE_COUNT`, `voxel_tuning.h:81-85`; specs
  `voxel_renderer.c:479` onwards) that drift, bob and glint, additive, untextured diamonds
  (`DrawMotes`, `voxel_renderer.c:3225-3290`), shown when `light.motes > 0` (sunny weather).
- Snow, ash and sand reuse `DrawWeather` (`voxel_renderer.c:455-468`).
- No leaves, pollen, fireflies or water glints.

### 2.7 Depth of field and post

- Scene FBO 960x544 with a **RGBA5551** colour texture and a 16-bit depth renderbuffer
  (`voxel_renderer.c:3620-3636`); scratch FBO 240x136 RGBA5551 (`voxel_renderer.c:3638-3651`).
  On the Deck the completeness of both is logged (`DeckGlCheckFbos`, `deck_video.c:90`, called at
  `voxel_renderer.c:3654`) but not acted on: `sDioramaFboReady` only checks the ids
  (`voxel_renderer.c:3657`). The logged result is not recorded anywhere: **UNVERIFIED**.
- DoF: one bilinear downscale to 1/4, then the sharp scene plus two blurred bands with an alpha
  gradient (`Pass_DepthOfField`, `voxel_renderer.c:5324-5477`; band sizes and strengths
  `voxel_tuning.h:89-92`). Only when `voxel_blur` is on, outdoors and not faded
  (`voxel_renderer.c:4740-4743`). Blur is a single 4x bilinear tap, so it looks blocky, and it
  ignores depth: tall things in the bands blur the same as the ground.
- No bloom, vignette or tone/colour grading pass; the FBO path only exists when DoF is on.

### 2.8 Characters

- Entity billboards, alpha-tested, from the sprite atlas, plus a reversed-depth ivory silhouette
  of the player behind roofs (`Pass_Entities`, `voxel_renderer.c:5036` onwards). No outline or rim
  light. The player position is in the entity snapshot (`playerWorldX/Z`,
  `include/platform/voxel_snapshot.h:11-12`).

### 2.9 Audio

- SDL audio device, 48 kHz (`VITA_AUDIO_RATE`, `src/platform/sdl2.c:73`), float32 stereo,
  512-sample device buffer, push model with `SDL_QueueAudio` (`sdl2.c:630-648`).
- The game thread (AgbMain) mixes m4a and CGB channels per game frame, scales by 0.125 and clamps
  (`src/music_player.c:764-781`), then calls `Platform_QueueAudio` (`music_player.c:784`).
- `Platform_QueueAudio` (`sdl2.c:1058-1116`): drops the newest frame when more than about 100 ms
  is queued, applies the volume setting (`sdl2.c:1086`), a DC-blocking high-pass
  (`sdl2.c:1092-1100`), then queues. Timing: the game thread's VBlank wait is a small PLL that
  keeps the SDL queue near ~19,200 bytes (`docs/THREADS.md` "Pacing").
- No ambient sound of any kind.

### 2.10 Settings and performance telemetry

- Voxel keys in `settings.txt`: `voxel_3d`, `voxel_battle`, `voxel_res`, `voxel_fps`,
  `voxel_blur` (`src/platform/vita/vita_extras.c:93-99`; enum `include/platform/vita_extras.h:52-70`),
  read with `VitaExtras_GetFast` (example `voxel_renderer.c:4740`).
- Telemetry: `vita_perf.c` writes `perf_frames.csv` (frame, render3d, frame_ms, draw3d_ms,
  build_ms, worker_compute_ms ...) and `perf_summary.txt`; draw calls and vertex counts per frame
  go through `VitaPerf_MarkDraw3DEnd` (`vita_perf.h:33-34`, counted by the `glDrawArrays` wrapper at
  `voxel_renderer.c:30-39`). `tools/deck/perf_report.sh` summarises 3D frames (avg, 1% low,
  frames over 16.7 / 33.3 ms, draw3D, chunk builds). DECK_NOTES.md notes the summary still has
  Vita labels.

---

## 3. Options per wish

Effort is in agent-days (one agent, one focused day, including a Deck test). Costs are GPU
time per frame at 960x544 on the Deck's RDNA2 iGPU unless stated; all are estimates
(**UNVERIFIED** until measured).

### 3.a Shader foundation (prerequisite for b, d, e, f, g)

| Option | What | Effort | Risk |
|---|---|---|---|
| A1 | GLSL 1.20 programs in the compat context, small loader (compile, link, log), shaders as C strings, a per-frame uniform block set by hand (`uTime`, `uWind`, `uSunDir`, `uPlayer`, `uChunkOffset`). `glUseProgram(0)` returns to fixed-function for untouched passes. | S (1) | Low |
| A2 | Same, but shader files loaded from `app0/shaders/` with hot reload on a key for tuning. | S+ (1.5) | Low |
| A3 | Port the whole renderer to a core profile (no fixed-function). | L (8+) | High, touches every pass, no visual gain by itself |

Facts that make A1 work without touching vertex layouts:

- The client-state calls map directly to GLSL 1.20 built-ins: `glVertexPointer` to `gl_Vertex`,
  `glTexCoordPointer` to `gl_MultiTexCoord0`, `glColorPointer` (4 x `GL_UNSIGNED_BYTE`,
  normalised) to `gl_Color`. Matrices come from `gl_ModelViewProjectionMatrix`, so
  `ChunkOffsets_Apply`'s push/pop keeps working.
- Per-vertex flags travel in the alpha byte of chunk geometry (always 255 today,
  `voxel_renderer.c:780`). Values must stay above 0 so the fixed-function alpha test still
  passes (safe for the Vita path, which never reads them).
- In a 2.x compatibility context the fixed alpha test still runs after a fragment shader; to be
  independent of that, cut-out shaders `discard` at alpha 0 as well.
- Shaders replace `GL_MODULATE` (texture x vertex colour) in the passes they cover; the
  `GL_COMBINE` shadow pass stays fixed-function.

Illustrative only (not final code):

```glsl
// illustrative: chunk vertex shader skeleton, GLSL 1.20
#version 120
uniform float uTime; uniform vec3 uWind; uniform vec2 uChunkOffset;
varying vec2 vUv; varying vec4 vCol;
void main() {
    vec4 p = gl_Vertex;
    float w = clamp((gl_Color.a * 255.0 - 128.0) / 127.0, 0.0, 1.0); // sway weight from alpha flag
    vec2 world = p.xz + uChunkOffset;
    p.x += w * uWind.z * sin(uTime * 1.7 + dot(world, vec2(0.37, 0.61))) * uWind.x;
    gl_Position = gl_ModelViewProjectionMatrix * p;
    vUv = gl_MultiTexCoord0.xy; vCol = vec4(gl_Color.rgb, 1.0);
}
```

Recommendation: **A1**, with an environment variable to dump compile logs; add A2's override
directory later only if tuning sessions need it. All new code in new files under
`src/platform/deck/` (`deck_gl_shader.c/.h`) plus `#ifdef PLATFORM_DECK` hooks in
`voxel_renderer.c`.

### 3.b Sunlight

| Option | Look | Effort | GPU cost | Risk | Cannot do |
|---|---|---|---|---|---|
| B1 Post chain on the scene FBO: bright-pass bloom (half res, separable), time-of-day grade, vignette | Octopath's glow on sunlit paths and lamps, warm evenings, blue nights | M (2) | ~0.3-0.6 ms | Low | No new shadows |
| B2 Grade through a 3D LUT (16^3 or 32^3) blended between dawn/day/dusk/night LUTs | Full art-directable grade, replaces the day/night multiply quad | +S (1) on top of B1 | ~0.05 ms | Low | Needs someone to author LUTs (can start from generated curves) |
| B3 World-space light shafts: additive textured beam cards slanted along `uSunDir`, placed at tree-crown gaps and building edges, fading with distance and haze | The "god ray" beams through trees | M (2) | ~0.1-0.2 ms | Medium: placement heuristics | Not true volumetrics |
| B4 Softer and richer decal shadows for entities: blurred blob plus the existing silhouette, stretched along the sun | Characters sit in the world better | S (1) | negligible | Low | No self-shadowing |
| B5 Real-time sun shadow map (1024^2 or 2048^2 depth from the sun, PCF), replacing the baked ray shadows | Crisp moving shadows, swaying crowns cast swaying shadows | L (6-8) | ~0.5-1.5 ms | High: double-darkening with baked rays unless the bake changes; lighting hash and chunk rebuild policy change; crown cards are 1-bit cut-outs that must be drawn into the depth map with alpha test | Not on the Vita; changes the established look |

Notes:

- B1 needs the scene FBO in **RGBA8** (today RGBA5551, `voxel_renderer.c:3627-3628`): bloom and
  grading on 5-bit colour band visibly. On the Deck only, also switch the depth to a 24-bit
  depth **texture** so later DoF (3.g) can read it.
- The FBO path must run whenever any post effect is on, not only with DoF
  (`dioramaActive`, `voxel_renderer.c:4740-4743`), and must fall back to direct rendering if
  `glCheckFramebufferStatus` is not complete (today it is only logged).
- Fades and moonlight are drawn inside the FBO (`Pass_Atmosphere2D`). The grade must either run
  before them or be faded out with `fadeAmount`, or a fade to white becomes tinted.
- The existing per-weather `light.bloom` values (sun 0.10, rain 0.08 + 0.30 x bolt, etc.,
  `voxel_renderer.c:2803-2905`) give B1 its strength curve for free.
- Sky/ambient tint: there is no sky in view (camera pitched down, clear colour black,
  `voxel_renderer.c:4767-4770`), so "sky tint" means the grade's shadow colour (blue shade by
  day, purple at dusk), done in B1/B2.

Recommendation: **B1 + B2 in phase 1 (B2 can start as procedural curves), B4 and B3 in later
phases; B5 deferred to an explicit go/no-go after phase 6** (decision 6).

### 3.c Rain

| Option | Effort | CPU / GPU | Risk |
|---|---|---|---|
| C1 Raise counts on the current CPU path (cap 128 to ~800), add wind slant and per-streak alpha variation | S (0.5) | CPU ~0.1-0.3 ms building quads; GPU negligible | Low; limited look |
| C2 GPU-driven streaks: a static VBO of N quads whose seed is in the UV and corner in colour; the vertex shader computes position from `uTime`, `uWind`, the camera box (same wrap maths as `DrawWeather`), stretches along velocity, fades near the camera; textured soft streak | M (1.5) | CPU ~0; GPU ~0.1 ms for 2,000 streaks | Low-medium |
| C3 Splashes and ripples: ring sprites on the ground where streaks end. Needs the ground height: the camera ground (`sCamera.ground`) is enough on flat routes; per-cell height from relief (`VoxelRelief_*`) is better. Ripple decals on water and puddle cells | M (1.5) | CPU small (spawn ~60 per frame) or GPU-only with a height texture | Medium: heights on cliffs and roofs |
| C4 Wet ground: ground fragment shader with `uWet` (0..1, ramps up over ~20 s of rain, dries slowly): darkens and saturates albedo, adds a view-dependent sheen using `dFdx/dFdy` normals (flat ground has no normal attribute) and a faint sky reflection colour | S (1) | ~0.1 ms | Low |
| C5 Mist: reuse `DrawFogBanks` sheets with a rain colour at low height during heavy rain | S (0.5) | negligible | Low |

Cannot do: true occlusion under roofs (streaks will fall through awnings) without a height map;
accept or clip by building footprint (UNVERIFIED cost).

Recommendation: **C2 + C4 + C5 first, C3 with the camera ground plane, refined to relief heights
if it looks wrong on slopes.** Lightning flash stays; add a short bloom spike from `bolt`.

### 3.d Sway

| Option | What | Effort | Risk |
|---|---|---|---|
| D1 Vertex-shader sway of tree crowns and long grass in `Pass_Trees`, weight in the vertex alpha set at build: 0 at the crown's foot, 1 at the top; phase from world position; amplitude and gust from `uWind` (stronger in rain and sandstorms) | S-M (1.5) | Low |
| D2 CPU sway: rewrite tree vertices each frame | M | Throws away the static VBO design (RENDERING.md "GPU rules"); not recommended |
| D3 Player bend: up to 4 positions (player, follower, nearest NPCs from the snapshot) as uniforms; vertices within ~0.8 tiles lean away, recover with a spring driven by time since the walker left (CPU keeps a tiny per-walker history) | S (1) on top of D1 | Low |

Build-side change: `VoxelVertex` (`voxel_mesh_builder.h:18-23`) has no field for the weight.
Either add one float (`sway`) to `VoxelVertex` and pack it into alpha in `PackVoxelVertices`, or
set a builder flag (like `builder->rounded`, `voxel_mesh_builder.h:77-78`) that `VoxelBuilder_Quad`
turns into per-corner weights. The ground under a tree (`EmitGround` in `voxel_tree.c`) keeps
weight 0. The baked vertex lighting stays as it is (a few centimetres of sway do not need
re-lighting).

Recommendation: **D1 then D3**. Trees sway slightly (a couple of texels at the top), grass more.

### 3.e Unflatten tall grass and flowers

Classification (no invented ids):

- Tall grass: per cell, `MetatileBehavior_IsTallGrass(behavior)` with the behaviour read as in
  `voxel_world.c:739-756` (the pattern `voxel_battle.c:140` already uses). This covers
  `METATILE_General_TallGrass` 0x00D and its variants 0x1C6, 0x1C7, 0x025
  (`metatile_labels.h:240-243`) and any secondary tileset's tall grass with the same behaviour.
  Long grass keeps its current `VOXEL_TREE_GRASS` path.
- Flowers: there is no behaviour or label for them. Classify by tile content: a metatile whose
  upper layer references the General flower tiles 508-511 (`tileset_anims.c:661`), plus the
  Mauville and Ever Grande flower tiles once their offsets are read from
  `tileset_anims.c` (UNVERIFIED). Computed once per tileset pair when the atlas is built
  (the atlas already maps metatiles to the tiles they use, `voxel_atlas.h:93-95`).

Art options:

| Option | Look | Effort | Risk |
|---|---|---|---|
| E1 Reuse the long-grass tuft (tree atlas 80,32) for tall grass, tinted per tileset | Consistent, cheap; all tall grass looks like Route 119 grass, a bit taller than the original | S (1) | Low |
| E2 Cut-out of the metatile's own art: a new atlas variant that composes only the upper layer with palette index 0 transparent (as the relief "cut tiles" do, `voxel_atlas.h:66-72`), stood up as 2-3 crossed or leant cards per cell, plus the ground variant without the upper layer under it. Flowers keep their animation because they sample the animated atlas | Most faithful: the same tufts and flowers, now standing | M-L (3) | Medium: atlas id space (`VOXEL_METATILE_IDS`), animation dirty-row tracking for the new ids |
| E3 New hand-drawn tuft and flower sprites added to `trees.rgba5551` | Closest to Octopath; art in the GBA style | M (2) + art time | Licensing: the generator is Emerald 3DS's CC BY-NC-SA script (`docs/ASSETS.md`); new art must come from you or a CC0 source |

Geometry and budget: a tuft is 2 crossed quads (12 vertices, non-indexed `GL_TRIANGLES`), 1-2
tufts per cell; flowers 1 small card each. A chunk is 8x8 cells (`voxel_renderer.c:293`); a
chunk full of tall grass is at most 64 x 24 = 1,536 vertices (37 KB). Today the tree region is
capped at 768 vertices per chunk and silently truncated (`CHUNK_TREE_CAP`,
`voxel_renderer.c:296`, clamp at `:2274`), so the cap must rise to about 3,072 (and
`CHUNK_VERTS_PER_SLOT`, `:302`, with it). `treeFirstVert` is `uint16_t` (`voxel_renderer.c:2380`);
19,200 + 2,304 extra still fits under 65,535. With ~30 visible chunks the worst case adds about
50,000 vertices a frame: negligible on RDNA2.

Lighting and threads: tufts are emitted in the tree phase of `ComputeChunkJob`
(`voxel_renderer.c:2242-2277`) on the chunk worker, so no new thread rules. Per-vertex ray
lighting is most of a build's cost; tufts should take one cached light sample at their foot
(`VoxelLighting_Sample`, `voxel_lighting.h:35`) instead of rays per vertex. Their content is
already part of the chunk hash through the metatile ids; no new lighting hash input.

Interplay with the GBA: when the player walks in tall grass the game shows a grass field effect
over the player's legs. How the 3D path draws that effect today (billboard or hidden) is
UNVERIFIED; if it draws, it must be hidden on cells that now have standing tufts or the player
gets two layers of grass. Encounters and behaviour are untouched (render only).

Recommendation: **E1 for tall grass first (quick, visible), E2 for flowers and as the long-term
tall grass, E3 only if you want to supply art.** Player bend (D3) applies to all of them.

### 3.f Particles (dust, pollen, fireflies, leaves, water sparkle)

| Option | Effort | CPU / GPU | Notes |
|---|---|---|---|
| F1 Extend the CPU path (`DrawMotes` style) with more kinds | S per kind | CPU ~0.05 ms per 100 quads | Simple, already proven |
| F2 One GPU particle system: a static VBO of quads with seeds in UV and kind/corner in colour; the vertex shader animates by kind (drift, bob, flutter, spiral fall, blink) in a camera-relative wrapped box; one draw call per blend mode | M (2) | CPU ~0; GPU ~0.1 ms for 2,000 | Rain (C2) uses the same system |
| F3 Point sprites (`GL_POINT_SPRITE`, `gl_PointSize`) | S | lowest | Square and unrotated: fine for fireflies and glints, wrong for streaks and leaves |

Kinds and their drivers:

- Pollen / dust: the current motes, made denser and textured, by `light.motes` (sun).
- Fireflies: night (`VitaDayNight_Get()->glow`, `vita_daynight.h:28-33`), on routes, near grass;
  additive, they also feed bloom.
- Falling leaves: spawned near tree cells. Needs a small per-chunk list of tree positions written
  at build (`treeCount` already exists per chunk); emitted in the GPU system as anchored emitters.
- Water sparkle: better done in the ground shader on water cells (flag in the vertex alpha of
  water ground vertices, set where the builder lays water) as sun-direction glints, than as
  particles.

Recommendation: **F2 for everything moving, water glints in the ground shader, F3 not used.**

### 3.g HD-2D post: DoF, bloom, grade, characters

| Option | Effort | GPU | Notes |
|---|---|---|---|
| G1 Keep the band DoF, replace the single 1/4 tap by a half-res separable 9-tap Gaussian | S (1) | ~0.2 ms | Smooth instead of blocky; still ignores depth |
| G2 Depth-based tilt-shift: circle of confusion from scene depth (needs the depth texture) and screen y, half-res separable blur, near and far layers, optional hexagonal bokeh highlights on bright pixels | M (2.5) | ~0.4-0.8 ms | The real Octopath diorama look; tall buildings stay sharp where they are in focus |
| G3 Bloom, grade, vignette | in B1/B2 | | Shared chain |
| G4 Character outline and rim: billboard fragment shader sampling 4 neighbours of the sprite texel for a 1-texel dark outline, and a warm rim on texels whose neighbour toward the sun's screen direction is transparent | S (1) | negligible | Helps sprites read against busy 3D ground |
| G5 Higher 3D internal resolution: render the scene FBO at the letterboxed window size (about 1280x720) or 2x supersampled, keep the 960x544 UI path | M (2) | 1.8x to 4x fill cost | Non-integer 1.33x nearest scaling of the current output is visible; projection is hard-coded to 960/544 (`voxel_renderer.c:4759`), UI and overlay assume 960x544. Overlaps stage 2 plan Task 4 (resolution and scale options) |

Post chain order (proposed): scene into RGBA8 FBO with depth texture, then bright pass and
bloom (half res), then DoF, then one composite shader (bloom add, LUT grade, vignette, optional
grain), then present into the letterbox, then `Pass_Overlay2D` and `Ui_Render` unchanged on top
(`voxel_renderer.c:5815-5818`), so text and menus are never blurred or graded.

Recommendation: **G1 in phase 1 (cheap, visible), G2 + G5 in phase 7, G4 with phase 4 or 6.**

### 3.h Ambient audio

Where to mix: in `Platform_QueueAudio` (`sdl2.c:1058`), after the volume scale (`sdl2.c:1086`)
and before the DC blocker (`sdl2.c:1092`), on the game thread (AgbMain) that calls it once per
game frame (`music_player.c:784`). The ambient mixer adds into the same buffer, so the sample
count per frame, the queue target and the 60 Hz PLL are untouched. It must output silence when
the queue is cleared for fast forward (`sdl2.c:1066-1069`) and skip the frame when the newest
frame is dropped. Because it runs on the game thread it can read game state directly (map type,
weather, time of day) without crossing threads; `gMapHeader.mapType` values are in
`include/constants/map_types.h:4-13`, outdoor test `VitaGame_IsOutdoor` (`vita_daynight.h:68`),
daylight `VitaDayNight_Get()->daylight` (`vita_daynight.h:31`).

Selection (proposed, tunable):

| Context | Layers |
|---|---|
| Route / town by day | birds (sparse, randomised calls, stereo-panned), light wind |
| Dusk | fewer birds, first crickets |
| Night outdoors | crickets / insects, occasional owl (if a CC0 sample exists), lower wind |
| Rain / thunderstorm | rain bed scaled by weather amount, distant thunder synced to `bolt` |
| Ocean route, beach towns, near water | waves / water lapping |
| Underground / caves | low room tone, drips |
| Indoors, battles, menus | off (fade out over 1 s); cries and SE untouched |

Sample sources (no copyrighted game audio):

| Option | Quality | Effort | Licence risk |
|---|---|---|---|
| H1 CC0 field recordings you choose and supply (for example from sources that mark files CC0), stored as WAV in `app0/ambient/` with a `LICENSES.txt` listing each file's origin; decoded with `SDL_LoadWAV` (built into SDL2) | Best for birds and insects | M (2) | None if each file is verified CC0 by you |
| H2 Procedural synthesis: wind (filtered noise with slow gusts), rain (noise plus random droplet transients), waves (shaped noise swells), crickets (amplitude-modulated sine bursts) | Good for wind, rain, waves, insects; birds sound synthetic | M (2) | None |
| H3 Mix: procedural for wind/rain/waves/insects, CC0 recordings for birds only | Best balance | M (2.5) | Minimal |

Volume: new `ambient_volume` key (0-10, default 6) in `settings.txt`, multiplied by the master
`PLATFORM_SETTING_VOLUME`; ambient kept about 15-20 dB below the music and passed through a soft
limiter so the existing clamp (`music_player.c:776-779`) is not pushed. CPU cost: a few voices of
mixing and noise at 48 kHz, well under 0.1 ms per frame on Zen 2 (estimate).

Recommendation: **H3**, with procedural layers first so the feature works before any sample is
chosen.

---

## 4. Performance budget

Current numbers: **none recorded on the Deck.** `perf_report.sh` exists (`tools/deck/perf_report.sh`)
but no baseline file is in the tree, and `perf_summary.txt` still uses Vita labels
(`docs/DECK_NOTES.md`, Task 7). The only measured figures are the Vita's (chunk build ~6 ms
average, up to ~35 ms on mountains, `docs/VOXEL_ENGINE.md`; overlay pass ~5 + 2 ms,
`docs/RENDERING.md` "Pass 6"). Phase 1 therefore starts by recording a baseline.

Targets: 60 FPS locked (16.7 ms) at the 1280x800 panel; battery modes 40 FPS (25 ms, still an
experiment in the stage 2 plan, Task 5) and 30 FPS (33.3 ms). Rule: the full High preset must keep
3D frames at p99 under 14 ms at 60 FPS (leaves 2.7 ms for spikes), and add no more than 2 ms of
average Core 0 time.

Added cost per effect (estimates at 960x544; G5 multiplies the fill-bound rows by 1.8-4):

| Effect | GPU ms | CPU ms (Core 0) | Build cost (worker) | Toggle key | Low | Med | High |
|---|---|---|---|---|---|---|---|
| Shader foundation | ~0 | ~0.02 (uniforms) | 0 | `voxel_shaders` (master) | on | on | on |
| RGBA8 FBO + composite (grade, vignette) | 0.1-0.2 | ~0 | 0 | `voxel_grade` | on | on | on |
| Bloom | 0.2-0.4 | ~0 | 0 | `voxel_bloom` | off | on | on |
| Sway (trees, grass, bend) | <0.05 | <0.02 | ~0 | `voxel_sway` | on | on | on |
| Standing tall grass / flowers | 0.1-0.3 | 0 | +5-15% per grass chunk (estimate) | `voxel_grass3d` | off | on | on |
| Rain GPU streaks + splashes | 0.1-0.2 | ~0.05 | 0 | `voxel_rain_hd` | off | on | on |
| Wet ground / water glints | ~0.1 | 0 | 0 | `voxel_wet` | off | on | on |
| Particles (pollen, leaves, fireflies) | ~0.1 | ~0.02 | small (tree list) | `voxel_particles` | off | on | on |
| DoF G1 | ~0.2 | 0 | 0 | `voxel_blur` (exists) | on | on | - |
| DoF G2 (depth, bokeh) | 0.4-0.8 | 0 | 0 | `voxel_blur=2` | - | - | on |
| Light shafts B3 | 0.1-0.2 | ~0.02 | 0 | `voxel_shafts` | off | off | on |
| Character outline/rim G4 | ~0 | 0 | 0 | `voxel_outline` | off | on | on |
| Internal resolution G5 | x1.8-4 fill | ~0 | 0 | `voxel_res` (exists, ignored on Deck today) | 960x544 | 1280x720 | 1280x720 or 2x |
| Ambient audio | 0 | <0.1 (game thread) | 0 | `ambient_volume` | on | on | on |
| Shadow map B5 (if approved) | 0.5-1.5 | ~0.1 | changes the bake | `voxel_shadows` | off | off | on |

Settings plumbing: new keys go in the `'v'` (and a new `'a'`) case of the key parser
(`vita_extras.c:93-99`) with new `FAST_EXTRA_*` entries (`vita_extras.h:68-70`), read per frame
with `VitaExtras_GetFast`. A `voxel_quality=0/1/2` key sets the preset; individual keys override
it. Default preset: decision 7.

---

## 5. Phased roadmap

Each phase is independently testable and committed on its own. "Exit test" is what you can see
or read; perf numbers come from `tools/deck/perf_report.sh` on a fixed route (stage 2 plan's
route: house, out to town, run a path, stand 10 s), compared against the phase 1 baseline. Fixed
screenshot spots: Littleroot at noon, Route 101 grass at dusk, Route 119 in rain, Petalburg at
night (taken with `spectacle`, `docs/DECK_NOTES.md` Task 7).

| Phase | Deliverable | Exit test | Agents | Depends on |
|---|---|---|---|---|
| 1 | Shader foundation (A1) + post chain quick win: RGBA8 scene FBO with completeness check acting on fallback, FBO path decoupled from DoF, composite shader with bloom (from `light.bloom`), grade curves by time of day, vignette, G1 blur; settings keys and presets skeleton | Before/after screenshots at the 4 spots; baseline and new perf report; Vita build compiles unchanged; `voxel_shaders=0` gives the exact old picture | Sonnet implements; Opus reviews GL state and fallback; Haiku adds settings keys | Stage 2 done |
| 2 | Wind sway for tree crowns and long grass (D1), wind from weather | Short video or screenshot pair; trees still meet their trunks; no seam at chunk crossings | Sonnet; Opus reviews vertex weights and chunk offset | 1 |
| 3 | Ambient audio bed (H3, procedural layers first), `ambient_volume` | Listen test per context table; no crackle; queue level unchanged in the log; silent in fast forward | Sonnet; Haiku for settings and licence file | none (parallel with 2) |
| 4 | Standing tall grass (E1) and player/follower bend (D3), cap raise; then flowers (E2) | Route 101/102 screenshots; walking through grass parts it; encounter behaviour unchanged; chunk build time per report within +15% | Sonnet; Opus reviews atlas ids, caps and worker safety | 2 |
| 5 | Rain: GPU streaks (C2), wet ground (C4), mist (C5), splashes (C3) | Route 119/120 rain before/after; thunderstorm on Route 119; perf within budget | Sonnet; Opus reviews | 1 |
| 6 | GPU particle system (F2): pollen, fireflies, leaves; water glints; character outline/rim (G4); entity soft shadows (B4) | Day and night screenshots; particle count shown in debug overlay | Sonnet; Haiku for tuning tables | 1, 5 (shares the system) |
| 7 | Depth-based tilt-shift with bokeh (G2), internal 3D resolution option (G5), light shafts (B3), LUT grade (B2) | Diorama screenshots at the 4 spots; High preset p99 under 14 ms | Sonnet; Opus reviews post chain and letterbox | 1, stage 2 Task 4 |
| 8 (optional) | Sun shadow map (B5), only after a go decision | Side-by-side with baked shadows; build time and frame time report | Opus designs, Sonnet implements | 1-7, decision 6 |

Each implementation agent gets the standing limits (30 tool calls, 3 retries, report to the
orchestrator); edits to `voxel_renderer.c` are never split across parallel agents.

---

## 6. Risks and unverified assumptions

1. **GLSL on the compat context (UNVERIFIED).** Expected to work (GLSL 1.20 with fixed-function
   built-ins is standard in Mesa compatibility contexts), but the actual `GL_VERSION` line in
   `debug.log` has never been recorded. Phase 1 step 1 records it and compiles a trivial shader
   before anything else.
2. **Entry points.** `GL_GLEXT_PROTOTYPES` makes prototypes visible (`CMakeLists.txt:119`);
   linking GL 2.0 functions directly from glvnd's 32-bit `libGL` is UNVERIFIED. Use
   `SDL_GL_GetProcAddress`.
3. **FBO completeness (logged, never read).** RGBA5551 colour plus `GL_DEPTH_COMPONENT16`
   renderbuffer status is logged but `sDioramaFboReady` ignores it (`voxel_renderer.c:3657`). The
   RGBA8 / depth-texture change must check status and fall back.
4. **Alpha-byte flags.** Using vertex alpha as sway weight and water flag assumes nothing reads
   chunk vertex alpha except the alpha test (verified for passes 1-3, `voxel_renderer.c:4802-4888`;
   building pass at `:4895-4976` uses alpha test only on some pages, which is still `> 0`). Any
   future blended chunk pass would break this convention.
5. **Tree cap truncation.** `CHUNK_TREE_CAP` truncates silently (`voxel_renderer.c:2274`); more
   tufts without raising it would drop trees at chunk edges.
6. **Chunk offset and sway phase.** After a crossing, chunks are translated, not rebuilt
   (`docs/VOXEL_ENGINE.md` "Map changes"); the sway phase must use the offset uniform or the
   crowns jump at a map border.
7. **Grade vs fades.** Fades and moonlight are drawn into the FBO; a grade applied after them
   tints fades to black/white. Order or fade-out needed.
8. **GBA grass field effect** may double up with standing tufts (UNVERIFIED how the 3D path shows
   it).
9. **Flower classification** depends on reading Mauville and Ever Grande animation tile offsets
   (UNVERIFIED) and on composing upper-layer cut-outs at run time, which the atlas does today
   only for ids from `buildings.bin` / `relief.bin`.
10. **Vita must not break.** Everything new is under `#ifdef PLATFORM_DECK`; the Deck also
    defines `PLATFORM_VITA` (`CMakeLists.txt:65,79-81`), so guards must test `PLATFORM_DECK`, not
    `!PLATFORM_VITA`. A Vita build is compiled at the end of every phase (needs the VitaSDK
    toolchain available; UNVERIFIED where it runs now).
11. **No baseline.** Without a Deck perf baseline every cost above is a guess; phase 1 records it.
12. **Internal resolution (G5)** touches the projection aspect, the letterbox viewport macro and
    the overlay/UI mapping; it overlaps stage 2 Task 4 and must be designed with it.
13. **Audio timing.** The ambient mixer must not change the number of samples queued per game
    frame, or the VBlank PLL drifts (`docs/THREADS.md` "Pacing").
14. **Licensing.** The tree atlas generator is CC BY-NC-SA (`docs/ASSETS.md`); new art or sounds
    need clear provenance. No game audio.

---

## 7. Decisions needed from you

1. **Vita compatibility:** keep every new effect Deck-only behind `PLATFORM_DECK`, Vita
   unchanged? Recommended: yes.
2. **How far from the original look:** "faithful plus" (pixel art untouched, light, post, motion
   and standing grass added) or a stronger Octopath restyle (heavier grade, strong bloom, heavy
   DoF)? Recommended: faithful plus as the Medium default, the stronger look as High.
3. **Ambient audio sources:** procedural for wind, rain, waves and insects plus CC0 bird
   recordings that you pick and verify, listed in `app0/ambient/LICENSES.txt`? Recommended: yes
   (H3); I can list candidate CC0 sources for you to check, but you choose the files.
4. **Grass and flower art:** start with the existing art (long-grass tuft for tall grass, the
   flowers' own tiles as cut-outs), or do you want to supply new hand-drawn tufts?
   Recommended: existing art first.
5. **Internal 3D resolution:** keep 960x544 or render the 3D scene at the letterbox size
   (about 1280x720), with the UI unchanged? Recommended: 1280x720 in phase 7, with stage 2 Task 4.
6. **Real-time shadow map:** defer to a go/no-go after phase 6? Recommended: defer; the baked ray
   shadows already look right.
7. **Default preset on the Deck:** Recommended: Medium until the baseline is known, High as
   default if the phase 7 report shows p99 under 14 ms at 60 FPS.
8. **Priority order:** foundation + bloom/grade, sway, audio, grass/flowers, rain, particles, DoF,
   shadows: does this match what you care about most? Recommended: as listed.
9. **Ambient default:** on at `ambient_volume=6`, off indoors and in battles? Recommended: yes.
10. **Where settings live:** `settings.txt` keys now, an in-game options page later with the new
    UI (stage 3)? Recommended: yes.
11. **Shader source:** embedded C strings (one binary, nothing to lose) rather than loose files?
    Recommended: embedded, with an optional override directory only if tuning needs it.
12. **Battles:** apply the post chain (bloom, grade, DoF) to the 3D battle stage too, or field
    only at first? Recommended: field only in phases 1-6, battles in phase 7.

## Decisions (2026-11-13, Marcos)

Answered by Marcos: (2) both presets, Medium "faithful plus" and High strong Octopath-style; (3) ambient audio fully procedural, no recordings, so no LICENSES.txt is needed; (4) the 3D scene renders at about 1280x720 (phase 7); (5) grass and flowers reuse existing tile art first.

Taken as the plan's recommended answers because Marcos asked for "as much as we can" and did not object: (1) effects Deck-only behind PLATFORM_DECK, Vita unchanged; (6) sun shadow map deferred to a go/no-go after phase 6; (7) default preset Medium until the baseline exists, High if p99 frame time is under 14 ms; (8) priority order as listed in the phases; (9) ambient on at 6/10, off indoors and in battles; (10) options in settings.txt now, an in-game page later; (11) shaders as embedded C strings; (12) post effects in the field first, battles in phase 7.
