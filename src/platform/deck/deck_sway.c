#include "deck_sway.h"
#include <stdlib.h>
#include <time.h>
#include "deck_gl_shader.h"
#include "platform/vita_extras.h"

static DeckGlProgram sProg;
static GLint sTex = -1, sChunk = -1, sFrame = -1, sBend[DECK_SWAY_WALKERS] = { -1, -1, -1, -1 };
static float sWalk[DECK_SWAY_WALKERS][2];
static int sWalkN;
static bool sReady = false;

// Vertex alpha = 255 - round(190 * weight) (voxel_mesh_builder.h swayW). Weight 0 at the foot, 0.5 at the
// top of a crown, 1 at the top of long grass. Wave phase comes from the world position, so vertices shared
// by neighbouring cards, rows and chunks move identically (no tears).
static const char *kVs =
    "#version 120\n"
    "uniform float uTime;\n"
    "uniform vec3 uWind;\n"          // dirX, dirZ, strength (DeckGl_SetFrameUniforms)
    "uniform vec2 uChunkOffset;\n"
    "uniform vec2 uFrameOffset;\n"   // chunk draw offset: vertex + it = the frame the walkers are in
    "uniform vec3 uBend0;\n"         // walker x, z (current frame), 1 = present, 0 = unused
    "uniform vec3 uBend1;\n"
    "uniform vec3 uBend2;\n"
    "uniform vec3 uBend3;\n"
    "varying vec2 vUv;\n"
    "varying vec3 vCol;\n"
    "void main() {\n"
    "    float raw = 255.0 - floor(gl_Color.a * 255.0 + 0.5);\n"
    "    float bendFlag = mod(raw, 2.0);\n"   // byte = 2 * round(95 * w * scale) + bend (voxel_mesh_builder.c)
    "    float w = clamp((raw - bendFlag) / 190.0, 0.0, 1.0);\n"
    "    vec4 p = gl_Vertex;\n"
    "    vec2 world = p.xz + uChunkOffset;\n"
    "    float ph = dot(world, vec2(0.37, 0.61));\n"
    "    float s = (sin(uTime * 1.7 + ph) + 0.5 * sin(uTime * 0.9 + ph * 0.5 + 1.3)) / 1.5;\n"
    "    float gust = 0.7 + 0.3 * sin(uTime * 0.35 + dot(world, vec2(0.08, 0.05)));\n"
    "    float str = 0.25 + (uWind.z - 0.25) * 0.5;\n"
    "    float d = w * 0.8 * str * gust * (0.35 + 0.65 * s);\n"
    "    p.xz += vec2(uWind.x, uWind.y) * d;\n"
    "    if (bendFlag > 0.5) {\n"
    "        vec2 f = p.xz + uFrameOffset;\n"
    "        vec3 b[4]; b[0] = uBend0; b[1] = uBend1; b[2] = uBend2; b[3] = uBend3;\n"
    "        for (int i = 0; i < 4; i++) {\n"
    "            vec2 dv = f - b[i].xy;\n"
    "            float dist = length(dv);\n"
    "            float k = b[i].z * w * clamp(1.0 - dist / 0.9, 0.0, 1.0);\n"
    "            k *= k;\n"
    "            p.xz += (dist > 0.001 ? dv / dist : vec2(0.0, 1.0)) * (0.6 * k);\n"
    "            p.y -= 0.3 * k;\n"
    "        }\n"
    "    }\n"
    "    gl_Position = gl_ModelViewProjectionMatrix * p;\n"
    "    vUv = gl_MultiTexCoord0.xy;\n"
    "    vCol = gl_Color.rgb;\n"
    "}\n";

static const char *kFs =
    "#version 120\n"
    "uniform sampler2D uTex;\n"
    "varying vec2 vUv;\n"
    "varying vec3 vCol;\n"
    "void main() {\n"
    "    vec4 t = texture2D(uTex, vUv);\n"
    "    if (t.a <= 0.0) discard;\n"
    "    gl_FragColor = vec4(t.rgb * vCol, t.a);\n"
    "}\n";

void DeckSway_Init(void)
{
    sReady = false;
    if (!DeckGl_BuildProgram(&sProg, "sway", kVs, kFs))
    {
        DeckGl_Log("[SWAY] shader build failed: tree pass stays fixed-function\n");
        return;
    }
    sTex = DeckGl_Uniform(&sProg, "uTex");
    sChunk = DeckGl_Uniform(&sProg, "uChunkOffset");
    sFrame = DeckGl_Uniform(&sProg, "uFrameOffset");
    sBend[0] = DeckGl_Uniform(&sProg, "uBend0");
    sBend[1] = DeckGl_Uniform(&sProg, "uBend1");
    sBend[2] = DeckGl_Uniform(&sProg, "uBend2");
    sBend[3] = DeckGl_Uniform(&sProg, "uBend3");
    sReady = true;
    DeckGl_Log("[SWAY] ready\n");
}

void DeckSway_Shutdown(void)
{
    if (sProg.program != 0)
        DeckGl_DeleteProgram(&sProg);
    sReady = false;
}

bool DeckSway_Begin(void)
{
    if (!sReady || VitaExtras_GetFast(FAST_EXTRA_VOXEL_SHADERS) == 0 || VitaExtras_GetFast(FAST_EXTRA_VOXEL_SWAY) == 0)
        return false;
    DeckGl_UseProgram(&sProg);
    DeckGl_Set1i(sTex, 0);
    for (int i = 0; i < DECK_SWAY_WALKERS; ++i)
    {
        if (i < sWalkN)
            DeckGl_Set3f(sBend[i], sWalk[i][0], sWalk[i][1], 1.0f);
        else
            DeckGl_Set3f(sBend[i], 0.0f, 0.0f, 0.0f);
    }
    return true;
}

void DeckSway_SetChunkOffset(float x, float z, float frameX, float frameZ)
{
    DeckGl_Set2f(sChunk, x, z);
    DeckGl_Set2f(sFrame, frameX, frameZ);
}

void DeckSway_SetWalkers(const float (*xz)[2], int n)
{
    if (getenv("DECK_BEND_OFF") != NULL) // A/B test aid: no walkers, no bend
        n = 0;
    sWalkN = n < 0 ? 0 : (n > DECK_SWAY_WALKERS ? DECK_SWAY_WALKERS : n);
    for (int i = 0; i < sWalkN; ++i)
    {
        sWalk[i][0] = xz[i][0];
        sWalk[i][1] = xz[i][1];
    }
    {
        // once a second: where the bend uniforms put the walkers (current frame, tile centres)
        static time_t sLast;
        time_t now = time(NULL);

        if (sWalkN > 0 && now != sLast)
        {
            sLast = now;
            DeckGl_Log("[BEND] walkers %d: (%.2f,%.2f) (%.2f,%.2f) (%.2f,%.2f) (%.2f,%.2f)\n", sWalkN,
                       sWalk[0][0], sWalk[0][1], sWalkN > 1 ? sWalk[1][0] : 0.0f, sWalkN > 1 ? sWalk[1][1] : 0.0f,
                       sWalkN > 2 ? sWalk[2][0] : 0.0f, sWalkN > 2 ? sWalk[2][1] : 0.0f,
                       sWalkN > 3 ? sWalk[3][0] : 0.0f, sWalkN > 3 ? sWalk[3][1] : 0.0f);
        }
    }
}

bool DeckGrass_Enabled(void)
{
    return sReady && VitaExtras_GetFast(FAST_EXTRA_VOXEL_SHADERS) != 0 && VitaExtras_GetFast(FAST_EXTRA_VOXEL_GRASS) != 0;
}

void DeckSway_End(void)
{
    DeckGl_UseProgram(NULL);
}
