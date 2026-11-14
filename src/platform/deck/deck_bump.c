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
    "            vec3 Np = normalize(N - 3.0 * (gx * T + gy * B) * inv);\n"
    "            vec3 L = normalize(mat3(gl_ModelViewMatrix) * uBumpSun);\n"
    "            dd = dot(Np, L) - dot(N, L);\n"
    "        }\n"
    "    }\n"
    // the baked vertex colour already holds the cast shadows: where it is dark the sun term fades (no double darkening)
    "    float lit = mix(0.4, 1.0, smoothstep(0.5, 0.9, dbLum(vcol)));\n"
    "    float raw = dd * uBumpP.y * lit * 1.6 + lap * 3.0;\n"
    "    return 1.0 + uBumpP.x * clamp(raw, -1.0, 1.0);\n"
    "}\n";

static char sSrc[8192];
static float sSun[3] = { 0.0f, 1.0f, 0.0f };
static float sSunW = 0.35f;

bool DeckBump_Enabled(void)
{
    return VitaExtras_GetFast(FAST_EXTRA_VOXEL_SHADERS) != 0 && DeckLevel(FAST_EXTRA_VOXEL_BUMP) >= 0;
}

bool DeckBump_Build(DeckGlProgram *out, DeckBumpUniforms *u, const char *name, const char *vs, const char *fs)
{
    static const char kVer[] = "#version 120\n";
    const char *body = strncmp(fs, kVer, sizeof(kVer) - 1) == 0 ? fs + sizeof(kVer) - 1 : fs;

    memset(u, 0, sizeof(*u));
    u->par = u->sun = u->tex = -1;
    if (snprintf(sSrc, sizeof(sSrc), "%s%s%s", kVer, kBumpGlsl, body) < (int)sizeof(sSrc)
        && DeckGl_BuildProgram(out, name, vs, sSrc))
    {
        u->ok = true;
        u->par = DeckGl_Uniform(out, "uBumpP");
        u->sun = DeckGl_Uniform(out, "uBumpSun");
        u->tex = DeckGl_Uniform(out, "uBumpTex");
        return true;
    }
    DeckGl_Log("[BUMP] %s: shader with the bump block failed, building it without\n", name);
    return DeckGl_BuildProgram(out, name, vs, fs);
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
    // amplitude of the multiplier around 1.0: LOW +-10 percent, MEDIUM +-15, HIGH +-20 (0 = OFF, nothing changes)
    const float amp = u->ok && DeckBump_Enabled() ? DeckLevelPick(FAST_EXTRA_VOXEL_BUMP, 0.10f, 0.15f, 0.20f) : 0.0f;

    DeckGl_Set4f(u->par, amp, sSunW, 0.0f, 0.0f);
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
