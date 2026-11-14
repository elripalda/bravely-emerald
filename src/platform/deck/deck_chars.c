#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "deck_chars.h"
#include "deck_levels.h"
#include "deck_bump.h"
#include "../voxel/voxel_entities.h"
#include "deck_gl_shader.h"
#include "platform/vita_extras.h"

// ---- G4 outline and rim --------------------------------------------------------------------------------
static const char *kOutVs =
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

// Sprite texel (alpha > 0.5): the plain modulate result plus a warm rim where the texel toward the sun is clear.
// Clear texel with a sprite texel at +-1 in x or y: a dark outline (blended). Everything else is discarded as before.
static const char *kOutFs =
    "#version 120\n"
    "uniform sampler2D uTex;\n"
    "uniform vec2 uTexel;\n"
    "uniform vec2 uSunDir;\n"       // texels toward the sun, in uv
    "uniform vec2 uParams;\n"       // outline alpha, rim strength
    "varying vec2 vUv;\n"
    "varying vec4 vCol;\n"
    "varying vec3 vEye;\n"
    "void main() {\n"
    "    vec4 c = texture2D(uTex, vUv);\n"
    "    float bm = 1.0;\n"
    "#ifdef DECK_BUMP\n"
    "    bm = deckBumpMul(uTex, vUv, c.rgb, vCol.rgb, vEye);\n"   // before the branch: the screen derivatives need the whole quad
    "#endif\n"
    "    if (c.a > 0.5) {\n"
    "        float toSun = texture2D(uTex, vUv + uSunDir * uTexel).a;\n"
    "        float rim = (1.0 - step(0.5, toSun)) * uParams.y;\n"
    "        vec3 rgb = c.rgb * vCol.rgb * bm + vec3(0.34, 0.24, 0.10) * rim * vCol.rgb;\n"
    "        gl_FragColor = vec4(min(rgb, vec3(1.0)), c.a * vCol.a);\n"
    "        return;\n"
    "    }\n"
    "    float n = max(max(texture2D(uTex, vUv + vec2(uTexel.x, 0.0)).a, texture2D(uTex, vUv - vec2(uTexel.x, 0.0)).a),\n"
    "                  max(texture2D(uTex, vUv + vec2(0.0, uTexel.y)).a, texture2D(uTex, vUv - vec2(0.0, uTexel.y)).a));\n"
    "    if (n < 0.5 || uParams.x <= 0.0) discard;\n"
    "    gl_FragColor = vec4(vec3(0.04, 0.06, 0.13) * vCol.rgb, uParams.x * vCol.a);\n"
    "}\n";

static DeckGlProgram sOut;
static DeckBumpUniforms sOBump;
static GLint sOTex = -1, sOTexel = -1, sOSun = -1, sOParams = -1;
static bool sBlend = false;

// ---- B4 blob shadow ------------------------------------------------------------------------------------
static const char *kBlobVs =
    "#version 120\n"
    "varying vec2 vUv;\n"
    "varying vec4 vCol;\n"
    "void main() {\n"
    "    vUv = gl_MultiTexCoord0.xy;\n"
    "    vCol = gl_Color;\n"
    "    gl_Position = ftransform();\n"
    "}\n";

static const char *kBlobFs =
    "#version 120\n"
    "varying vec2 vUv;\n"
    "varying vec4 vCol;\n"
    "void main() {\n"
    "    float r = length(vUv);\n"
    "    float k = 1.0 - smoothstep(0.0, 1.0, r);\n"
    "    float a = vCol.a * k * k;\n"
    "    if (a < 0.004) discard;\n"
    "    gl_FragColor = vec4(vCol.rgb, a);\n"
    "}\n";

static DeckGlProgram sBlob;

typedef struct
{
    float x, y, z, u, v;
    uint8_t r, g, b, a;
} BlobVertex;

static BlobVertex sBlobVerts[DECK_CHARS_MAX_BLOBS * 6];

#define BLOB_HALF_X 0.40f   // tiles
#define BLOB_HALF_Z 0.26f
#define BLOB_SUN_PUSH 0.07f // centre pushed away from the sun (the silhouette falls the same way)

void DeckChars_Init(void)
{
    sOut.program = 0;
    sBlob.program = 0;
    if (DeckBump_Build(&sOut, &sOBump, "char-outline", kOutVs, kOutFs))
    {
        sOTex = DeckGl_Uniform(&sOut, "uTex");
        sOTexel = DeckGl_Uniform(&sOut, "uTexel");
        sOSun = DeckGl_Uniform(&sOut, "uSunDir");
        sOParams = DeckGl_Uniform(&sOut, "uParams");
    }
    else
        DeckGl_Log("[CHARS] outline shader build failed: plain billboards stay\n");
    if (!DeckGl_BuildProgram(&sBlob, "char-blob", kBlobVs, kBlobFs))
        DeckGl_Log("[CHARS] blob shader build failed: silhouette shadows only\n");
    DeckGl_Log("[CHARS] init: outline %s, blob shadow %s\n", sOut.program != 0 ? "ready" : "failed", sBlob.program != 0 ? "ready" : "failed");
}

void DeckChars_Shutdown(void)
{
    if (sOut.program != 0)
        DeckGl_DeleteProgram(&sOut);
    if (sBlob.program != 0)
        DeckGl_DeleteProgram(&sBlob);
}

bool DeckChars_OutlineBegin(float sunDx, float sunDz, float sunStrength)
{
    const float lvl = DeckLevelPick(FAST_EXTRA_VOXEL_OUTLINE, 0.5f, 1.0f, 1.0f);

    // The program also runs for the relief alone (outline OFF): outline alpha and rim 0, clear texels discarded
    if (sOut.program == 0 || VitaExtras_GetFast(FAST_EXTRA_VOXEL_SHADERS) == 0 || (lvl <= 0.0f && !(sOBump.ok && DeckBump_Enabled())))
        return false;
    DeckGl_UseProgram(&sOut);
    DeckGl_Set1i(sOTex, 0);
    DeckGl_Set2f(sOTexel, 1.0f / (float)VOXEL_SPRITE_ATLAS_W, 1.0f / (float)VOXEL_SPRITE_ATLAS_H);
    DeckGl_Set2f(sOSun, sunDx > 0.0f ? -1.0f : 1.0f, sunDz > 0.0f ? 1.0f : -1.0f);
    DeckGl_Set2f(sOParams, lvl > 0.0f ? 0.62f : 0.0f, 0.9f * sunStrength * lvl);
    DeckBump_Set(&sOBump, (float)VOXEL_SPRITE_ATLAS_W, (float)VOXEL_SPRITE_ATLAS_H, 0.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    sBlend = true;
    return true;
}

void DeckChars_OutlineEnd(void)
{
    if (sBlend)
        glDisable(GL_BLEND);
    sBlend = false;
    DeckGl_UseProgram(NULL);
}

bool DeckChars_ShadowEnabled(void)
{
    return sBlob.program != 0 && VitaExtras_GetFast(FAST_EXTRA_VOXEL_SHADERS) != 0
        && DeckLevel(FAST_EXTRA_VOXEL_SHADOW) >= 0;
}

// Silhouette peak 0.36 x 0.62 = 0.22, blob peak 0.20: where they overlap the sum is about 0.38, the old single shadow was 0.36.
float DeckChars_SilhouetteScale(void)
{
    return DeckChars_ShadowEnabled() ? 0.62f : 1.0f;
}

void DeckChars_DrawBlobs(const float (*blobs)[4], int count, float sunDx, float sunDz)
{
    static const float kCorner[6][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, -1 }, { 1, 1 }, { -1, 1 } };
    const float lvl = DeckLevelPick(FAST_EXTRA_VOXEL_SHADOW, 0.5f, 1.0f, 1.0f);
    int n = 0;

    if (!DeckChars_ShadowEnabled() || count <= 0)
        return;
    if (count > DECK_CHARS_MAX_BLOBS)
        count = DECK_CHARS_MAX_BLOBS;
    {
        const float len = sqrtf(sunDx * sunDx + sunDz * sunDz);
        const float px = len > 0.001f ? sunDx / len * BLOB_SUN_PUSH : 0.0f;
        const float pz = len > 0.001f ? sunDz / len * BLOB_SUN_PUSH : 0.0f;

        for (int i = 0; i < count; ++i)
        {
            const uint8_t a = (uint8_t)(blobs[i][3] * lvl * 255.0f + 0.5f);

            if (a == 0)
                continue;
            for (int k = 0; k < 6; ++k)
            {
                BlobVertex *v = &sBlobVerts[n++];

                v->x = blobs[i][0] + px + kCorner[k][0] * BLOB_HALF_X;
                v->y = blobs[i][1];
                v->z = blobs[i][2] + pz + kCorner[k][1] * BLOB_HALF_Z;
                v->u = kCorner[k][0];
                v->v = kCorner[k][1];
                v->r = 8; v->g = 20; v->b = 40;          // the indigo of the silhouette shadows
                v->a = a;
            }
        }
    }
    if (n == 0)
        return;
    DeckGl_UseProgram(&sBlob);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, sizeof(BlobVertex), &sBlobVerts[0].x);
    glTexCoordPointer(2, GL_FLOAT, sizeof(BlobVertex), &sBlobVerts[0].u);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(BlobVertex), &sBlobVerts[0].r);
    glDrawArrays(GL_TRIANGLES, 0, n);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    DeckGl_UseProgram(NULL);
}
