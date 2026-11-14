#include <math.h>
#include <stdio.h>
#include <string.h>
#include "deck_bump.h"
#include "deck_levels.h"
#include "platform/vita_extras.h"

// GLSL placed after "#version 120" of a fragment shader. The program calls deckBumpMul() once the texel is known to be kept
// (after its discard) and multiplies the colour with the result; uBumpP.x == 0 returns 1.0 before any texture tap, so a
// program built with the block draws the old picture when the setting is OFF.
static const char *kBumpGlsl =
    "#define DECK_BUMP 1\n"
    "uniform vec4 uBumpP;\n"          // x amplitude (0 = off), y sun weight
    "uniform vec3 uBumpSun;\n"        // world space, toward the sun, unit length
    "uniform vec3 uBumpTex;\n"        // texture width, height, slot size (0 = none)
    "float dbLum(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }\n"
    // height of the texel at integer offset o from the texel under tp
    "float dbH(sampler2D s, vec2 tp, vec2 o, float hc) {\n"
    "    vec2 q = floor(tp) + o;\n"
    "    float slot = uBumpTex.z;\n"
    "    if (slot > 0.5) {\n"
    "        vec2 org = floor(tp / slot) * slot;\n"
    "        q = clamp(q, org, org + (slot - 1.0));\n"
    "    } else {\n"
    "        q = clamp(q, vec2(0.0), uBumpTex.xy - 1.0);\n"
    "    }\n"
    "    vec4 t = texture2D(s, (q + 0.5) / uBumpTex.xy);\n"
    "    return (slot > 0.5 || t.a > 0.5) ? dbLum(t.rgb) : hc;\n"
    "}\n"
    // Ground micro-relief (voxel_groundrelief): a procedural height by ground kind (classified from the texel colour: grass
    // clumps, sand ripples, path grain) plus a low frequency undulation, lit by the sun like the bump. Shading only.
    "uniform vec4 uGroundP;\n"        // x amplitude (0 = off)
    "float dgHash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }\n"
    "float dgNoise(vec2 p) {\n"
    "    vec2 i = floor(p); vec2 f = fract(p); f = f * f * (3.0 - 2.0 * f);\n"
    "    return mix(mix(dgHash(i), dgHash(i + vec2(1.0, 0.0)), f.x), mix(dgHash(i + vec2(0.0, 1.0)), dgHash(i + vec2(1.0, 1.0)), f.x), f.y);\n"
    "}\n"
    // Fine grain in world space (noise frequencies per tile; fixed world scale, not screen scale, so nothing shimmers when walking).
    // Was (before the "much smaller" round): grass 1.5 and 3.6, sand 5.5 and 0.9, path 9 and 17, undulation 0.16 and 0.07 at 2.2 and 1.2.
    // Now: grass 5 and 12, sand 15 and 3, path 18 and 29; the undulation keeps its scales (0.16, 0.07) at x0.25 amplitude. The finest octave of each kind fades out when it
    // gets near the pixel size (aa), so it cannot alias.
    "float dgHeight(vec2 w, vec3 k, float fp) {\n"       // k: grass, sand, path weights; fp: tiles per pixel
    "    float a2 = 1.0 - smoothstep(0.25, 0.55, 12.0 * fp);\n"
    "    float a3 = 1.0 - smoothstep(0.25, 0.55, 29.0 * fp);\n"
    "    float g = dgNoise(w * 5.0) * 0.65 + (dgNoise(w * 12.0) * a2 + 0.5 * (1.0 - a2)) * 0.35;\n"
    "    float r = sin((w.x * 0.6 + w.y) * 15.0 + dgNoise(w * 3.0) * 4.0) * 0.5 + 0.5;\n"
    "    float p = dgNoise(w * 18.0) * 0.7 + (dgNoise(w * 29.0) * a3 + 0.5 * (1.0 - a3)) * 0.3;\n"
    "    return k.x * g * 0.8 + k.y * r + k.z * p;\n"
    "}\n"
    "float dgUndulation(vec2 w) { return (dgNoise(w * 0.16) * 2.2 + dgNoise(w * 0.07 + 5.0) * 1.2) * 0.25; }\n"
    "float deckGroundMul(vec2 w, vec3 base, vec3 vcol, vec3 eye) {\n"
    "    vec3 dp1 = dFdx(eye);\n"             // derivatives outside any branch
    "    vec3 dp2 = dFdy(eye);\n"
    "    if (uGroundP.x <= 0.0) return 1.0;\n"
    "    vec3 cr = cross(dp1, dp2);\n"
    "    float crLen = length(cr);\n"
    "    vec3 up = normalize(mat3(gl_ModelViewMatrix) * vec3(0.0, 1.0, 0.0));\n"
    "    float flatK = crLen > 1.0e-12 ? smoothstep(0.7, 0.95, abs(dot(cr / crLen, up))) : 1.0;\n"   // cliffs and walls: none
    "    float lum = dot(base, vec3(0.299, 0.587, 0.114));\n"
    "    float grass = smoothstep(0.02, 0.12, base.g - max(base.r, base.b));\n"
    "    float sand = (1.0 - grass) * smoothstep(0.08, 0.20, base.r - base.b) * smoothstep(0.45, 0.70, lum);\n"
    "    float path = max(0.0, 1.0 - grass - sand);\n"
    "    float water = clamp((base.b - max(base.r, base.g * 0.8) - 0.05) * 5.0, 0.0, 1.0);\n"
    "    vec3 k = vec3(grass, sand, path);\n"
    "    float fp = max(length(dFdx(w)), length(dFdy(w)));\n"
    "    float e = 0.02;\n"
    "    float h = dgHeight(w, k, fp);\n"
    "    float gx = (dgHeight(w + vec2(e, 0.0), k, fp) - h) / e;\n"
    "    float gz = (dgHeight(w + vec2(0.0, e), k, fp) - h) / e;\n"
    "    float u0 = dgUndulation(w);\n"
    "    float ux = (dgUndulation(w + vec2(0.5, 0.0)) - u0) / 0.5 * (1.0 - uGroundP.y);\n"   // indoors (uGroundP.y = 1): no slow undulation at all
    "    float uz = (dgUndulation(w + vec2(0.0, 0.5)) - u0) / 0.5 * (1.0 - uGroundP.y);\n"
    "    vec3 Np = normalize(vec3(-(gx * 0.09 + ux * 0.9), 1.0, -(gz * 0.09 + uz * 0.9)));\n"
    "    float dd = dot(Np, uBumpSun) - uBumpSun.y;\n"
    "    float raw = dd * (0.4 + 0.6 * uBumpP.y) * 2.4 + (h - 0.5) * 0.25;\n"
    "    return 1.0 + uGroundP.x * flatK * (1.0 - water) * clamp(raw, -1.0, 1.0);\n"
    "}\n"
    "float deckBumpMul(sampler2D s, vec2 uv, vec3 base, vec3 vcol, vec3 eye) {\n"
    "    if (uBumpP.x <= 0.0) return 1.0;\n"
    "    vec2 tp = uv * uBumpTex.xy;\n"
    "    float hc = dbLum(base);\n"
    "    float h00 = dbH(s, tp, vec2(-1.0, -1.0), hc);\n"
    "    float h10 = dbH(s, tp, vec2( 0.0, -1.0), hc);\n"
    "    float h20 = dbH(s, tp, vec2( 1.0, -1.0), hc);\n"
    "    float h01 = dbH(s, tp, vec2(-1.0,  0.0), hc);\n"
    "    float h21 = dbH(s, tp, vec2( 1.0,  0.0), hc);\n"
    "    float h02 = dbH(s, tp, vec2(-1.0,  1.0), hc);\n"
    "    float h12 = dbH(s, tp, vec2( 0.0,  1.0), hc);\n"
    "    float h22 = dbH(s, tp, vec2( 1.0,  1.0), hc);\n"
    "    float gx = ((h20 + 2.0 * h21 + h22) - (h00 + 2.0 * h01 + h02)) * 0.125;\n"
    "    float gy = ((h02 + 2.0 * h12 + h22) - (h00 + 2.0 * h10 + h20)) * 0.125;\n"
    "    gx /= 1.0 + 2.0 * abs(gx);\n"   // soft clamp: a seam between two pieces of art cannot make a hard line
    "    gy /= 1.0 + 2.0 * abs(gy);\n"
    "    float lap = clamp(hc - 0.125 * (h00 + h10 + h20 + h01 + h21 + h02 + h12 + h22), -0.2, 0.2);\n"
    // surface frame from the screen derivatives (no normal or tangent attributes on these meshes)
    "    vec3 dp1 = dFdx(eye);\n"
    "    vec3 dp2 = dFdy(eye);\n"
    "    vec2 du1 = dFdx(uv);\n"   // derivatives outside any branch: undefined inside divergent flow
    "    vec2 du2 = dFdy(uv);\n"
    "    vec3 N = cross(dp1, dp2);\n"
    "    float nl = length(N);\n"
    "    float dd = 0.0;\n"
    "    if (nl > 1.0e-12) {\n"
    "        N /= nl;\n"
    "        if (dot(N, eye) > 0.0) N = -N;\n"      // face the viewer
    "        vec3 p2 = cross(dp2, N);\n"
    "        vec3 p1 = cross(N, dp1);\n"
    "        vec3 T = p2 * du1.x + p1 * du2.x;\n"
    "        vec3 B = p2 * du1.y + p1 * du2.y;\n"
    "        float m = max(dot(T, T), dot(B, B));\n"
    "        if (m > 1.0e-20) {\n"
    "            float inv = inversesqrt(m);\n"
    "            vec3 Np = normalize(N - 5.0 * (gx * T + gy * B) * inv);\n"
    "            vec3 L = normalize(mat3(gl_ModelViewMatrix) * uBumpSun);\n"
    "            dd = dot(Np, L) - dot(N, L);\n"
    "        }\n"
    "    }\n"
    // the baked vertex colour already holds the cast shadows: where it is dark the sun term fades (no double darkening)
    "    float lit = mix(0.55, 1.0, smoothstep(0.5, 0.9, dbLum(vcol)));\n"
    "    float raw = dd * uBumpP.y * lit * 2.8 + lap * 4.5;\n"
    "    return 1.0 + uBumpP.x * clamp(raw, -1.0, 1.0);\n"
    "}\n";

static char sSrc[16384];
static float sSun[3] = { 0.0f, 1.0f, 0.0f };
static float sSunW = 0.35f;

bool DeckBump_GroundOn(void)
{
    return VitaExtras_GetFast(FAST_EXTRA_VOXEL_SHADERS) != 0 && DeckLevel(FAST_EXTRA_VOXEL_GROUNDRELIEF) >= 0;
}

bool DeckBump_Enabled(void)
{
    return VitaExtras_GetFast(FAST_EXTRA_VOXEL_SHADERS) != 0 && DeckLevel(FAST_EXTRA_VOXEL_BUMP) >= 0;
}

bool DeckBump_Build(DeckGlProgram *out, DeckBumpUniforms *u, const char *name, const char *vs, const char *fs)
{
    static const char kVer[] = "#version 120\n";
    const char *body = strncmp(fs, kVer, sizeof(kVer) - 1) == 0 ? fs + sizeof(kVer) - 1 : fs;

    memset(u, 0, sizeof(*u));
    u->par = u->sun = u->tex = u->ground = -1;
    if (snprintf(sSrc, sizeof(sSrc), "%s%s%s", kVer, kBumpGlsl, body) < (int)sizeof(sSrc)
        && DeckGl_BuildProgram(out, name, vs, sSrc))
    {
        u->ok = true;
        u->par = DeckGl_Uniform(out, "uBumpP");
        u->sun = DeckGl_Uniform(out, "uBumpSun");
        u->tex = DeckGl_Uniform(out, "uBumpTex");
        u->ground = DeckGl_Uniform(out, "uGroundP");
        return true;
    }
    DeckGl_Log("[BUMP] %s: shader with the bump block failed, building it without\n", name);
    return DeckGl_BuildProgram(out, name, vs, fs);
}

static bool sIndoor;

void DeckBump_SetIndoor(bool indoor)
{
    sIndoor = indoor;
}

// Indoor floors (houses, Pokemon Center, lab: tile patterns, carpets, wood): the colour classification of deckGroundMul has no kind for them (they
// fall into "path" grain) and deckBumpMul would run its Sobel over the tile pattern at 0.18 / 0.30 / 0.45. Indoors both are cut to a faint
// fine grain: the albedo relief x0.12 and the procedural ground x0.35, and the slow undulation is off.
void DeckBump_ApplyIndoor(const DeckBumpUniforms *u)
{
    if (!sIndoor || !u->ok)
        return;
    {
        const float amp = DeckBump_Enabled() ? 0.12f * DeckLevelPick(FAST_EXTRA_VOXEL_BUMP, 0.18f, 0.30f, 0.45f) : 0.0f;
        const float gAmp = DeckBump_GroundOn() ? 0.35f * DeckLevelPick(FAST_EXTRA_VOXEL_GROUNDRELIEF, 0.20f, 0.35f, 0.50f) : 0.0f;

        DeckGl_Set4f(u->par, amp, sSunW, 0.0f, 0.0f);
        DeckGl_Set4f(u->ground, gAmp, 1.0f, 0.0f, 0.0f);
    }
}

void DeckBump_SetSun(float dx, float dz, float strength)
{
    // direction toward the sun: opposite of the shadow ray (voxel_lighting.c: end = pos - sun * reach), 45 degrees up
    const float l = sqrtf(dx * dx + 1.0f + dz * dz);

    sSun[0] = -dx / l;
    sSun[1] = 1.0f / l;
    sSun[2] = -dz / l;
    // moonlit and overcast scenes keep a third of the directional term: the relief is still there, just flatter
    sSunW = 0.35f + 0.65f * (strength < 0.0f ? 0.0f : (strength > 1.0f ? 1.0f : strength));
}

void DeckBump_Set(const DeckBumpUniforms *u, float texW, float texH, float slot)
{
    // amplitude of the multiplier around 1.0 (0 = OFF, nothing changes). Phase 9 had 0.10 / 0.15 / 0.20, too faint to see in
    // a 1280 shot; raised to 0.18 / 0.30 / 0.45 together with the light response in the GLSL block (items of 2026-11-14).
    const float amp = u->ok && DeckBump_Enabled() ? DeckLevelPick(FAST_EXTRA_VOXEL_BUMP, 0.18f, 0.30f, 0.45f) : 0.0f;
    // ground micro-relief (voxel_groundrelief): multiplier amplitude of the ground programs' procedural height
    const float gAmp = u->ok && DeckBump_GroundOn() ? DeckLevelPick(FAST_EXTRA_VOXEL_GROUNDRELIEF, 0.20f, 0.35f, 0.50f) : 0.0f;

    DeckGl_Set4f(u->par, amp, sSunW, 0.0f, 0.0f);
    DeckGl_Set4f(u->ground, gAmp, 0.0f, 0.0f, 0.0f);
    DeckGl_Set3f(u->sun, sSun[0], sSun[1], sSun[2]);
    DeckGl_Set3f(u->tex, texW, texH, slot);
}

// ---- Plain textured program (buildings) -------------------------------------------------------------------------
static const char *kPlainVs =
    "#version 120\n"
    "varying vec2 vUv;\n"
    "varying vec4 vCol;\n"
    "varying vec3 vEye;\n"
    "void main() {\n"
    "    vUv = gl_MultiTexCoord0.xy;\n"
    "    vCol = gl_Color;\n"
    "    vEye = (gl_ModelViewMatrix * gl_Vertex).xyz;\n"
    "    gl_Position = ftransform();\n"
    "}\n";

static const char *kPlainFs =
    "#version 120\n"
    "uniform sampler2D uTex;\n"
    "varying vec2 vUv;\n"
    "varying vec4 vCol;\n"
    "varying vec3 vEye;\n"
    "void main() {\n"
    "    vec4 t = texture2D(uTex, vUv);\n"
    "    vec4 c = t * vCol;\n"
    "#ifdef DECK_BUMP\n"
    "    c.rgb *= deckBumpMul(uTex, vUv, t.rgb, vCol.rgb, vEye);\n"
    "#endif\n"
    "    gl_FragColor = c;\n"
    "}\n";

static DeckGlProgram sPlain;
static DeckBumpUniforms sPlainU;
static GLint sPlainTex = -1;
static int sPlainState; // 0 not built, 1 ready, -1 failed (not tried again)

bool DeckBump_PlainBegin(float texW, float texH)
{
    if (!DeckBump_Enabled() || sPlainState < 0)
        return false;
    if (sPlainState == 0)
    {
        if (!DeckBump_Build(&sPlain, &sPlainU, "bump-plain", kPlainVs, kPlainFs) || !sPlainU.ok)
        {
            if (sPlain.program != 0)
                DeckGl_DeleteProgram(&sPlain);
            sPlainState = -1;
            DeckGl_Log("[BUMP] plain program failed: buildings stay fixed-function\n");
            return false;
        }
        sPlainTex = DeckGl_Uniform(&sPlain, "uTex");
        sPlainState = 1;
        DeckGl_Log("[BUMP] plain program ready\n");
    }
    DeckGl_UseProgram(&sPlain);
    DeckGl_Set1i(sPlainTex, 0);
    DeckBump_Set(&sPlainU, texW, texH, 0.0f);
    return true;
}

void DeckBump_PlainEnd(void)
{
    DeckGl_UseProgram(NULL);
}
