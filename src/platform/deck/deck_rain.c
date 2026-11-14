#include "deck_rain.h"
#include "deck_gl_shader.h"
#include "platform/vita_extras.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Rain box = the one DrawWeather uses (voxel_tuning.h VOXEL_WEATHER_*): 24 x 8.5 x 28 around the camera target,
// 20 units north of the target at the far side, starting 0.4 under the ground.
#define BOX_X 24.0f
#define BOX_Y 8.5f
#define BOX_Z 28.0f

#define STREAK_COUNT 2048

static bool sReady = false;
static char sOff[96];

static bool Off(const char *name) { return strstr(sOff, name) != NULL; }

static float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// ---- Vertex buffer helpers: 5 floats a vertex, x y z in the vertex position, corner x y in texcoord 0.
static const float kCorner[6][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, -1 }, { 1, 1 }, { -1, 1 } };

static GLuint MakeQuadVbo(int count, uint32_t seed)
{
    float *v = (float *)malloc((size_t)count * 6 * 5 * sizeof(float));
    GLuint vbo = 0;

    if (v == NULL)
        return 0;
    for (int i = 0; i < count; ++i)
    {
        float s[3];

        for (int c = 0; c < 3; ++c)
        {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            s[c] = (float)(seed & 0xFFFFu) / 65536.0f;
        }
        for (int k = 0; k < 6; ++k)
        {
            float *o = v + ((size_t)i * 6 + (size_t)k) * 5;

            o[0] = s[0]; o[1] = s[1]; o[2] = s[2];
            o[3] = kCorner[k][0]; o[4] = kCorner[k][1];
        }
    }
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)count * 6 * 5 * sizeof(float)), v, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    free(v);
    return vbo;
}

// Binds the quad VBO as vertex (3 floats) + texcoord 0 (2 floats), draws, unbinds.
static void DrawQuadVbo(GLuint vbo, int quads)
{
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, 5 * sizeof(float), (const void *)0);
    glTexCoordPointer(2, GL_FLOAT, 5 * sizeof(float), (const void *)(3 * sizeof(float)));
    glDrawArrays(GL_TRIANGLES, 0, quads * 6);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// ---- C2 streaks ----------------------------------------------------------------------------------------
static const char *kStreakVs =
    "#version 120\n"
    "uniform float uTime;\n"
    "uniform vec3 uTarget;\n"            // camera target x, ground, z
    "uniform vec3 uCam;\n"
    "uniform vec3 uVel;\n"               // units / second
    "uniform vec3 uParams;\n"            // length, min width, pixel width per unit distance
    "varying vec2 vUv;\n"
    "varying float vFade;\n"
    "void main() {\n"
    "    vec3 seed = gl_Vertex.xyz;\n"
    "    vec3 span = vec3(24.0, 8.5, 28.0);\n"
    "    vec3 p = fract(seed + fract((uTime * uVel - uTarget) / span));\n"
    "    vec3 w = vec3(uTarget.x - 12.0, uTarget.y - 0.4, uTarget.z - 20.0) + p * span;\n"
    "    vec3 d = 0.5 - abs(p - 0.5);\n"
    "    float fade = clamp(min(min(d.x * span.x / 3.0, d.z * span.z / 3.0), d.y * span.y / 1.2), 0.0, 1.0);\n"
    "    vec3 toCam = w - uCam;\n"
    "    float dist = length(toCam);\n"
    "    float lens = clamp((dist - 2.0) / 2.5, 0.0, 1.0);\n"
    "    vec3 dir = normalize(uVel);\n"
    "    float len = uParams.x * (0.8 + 0.45 * fract(seed.x * 7.0 + seed.z * 3.0));\n"
    "    vec3 side = normalize(cross(dir, toCam));\n"
    "    float width = max(uParams.y, dist * uParams.z);\n"
    "    vec3 pos = w + dir * (gl_MultiTexCoord0.y * 0.5 * len) + side * (gl_MultiTexCoord0.x * width);\n"
    "    vFade = fade * lens * min(1.0, 1.6 * uParams.y / width);\n"
    "    vUv = gl_MultiTexCoord0.xy;\n"
    "    gl_Position = gl_ModelViewProjectionMatrix * vec4(pos, 1.0);\n"
    "}\n";

static const char *kStreakFs =
    "#version 120\n"
    "uniform vec4 uColor;\n"
    "varying vec2 vUv;\n"
    "varying float vFade;\n"
    "void main() {\n"
    "    float across = 1.0 - vUv.x * vUv.x;\n"
    "    float along = sin((vUv.y * 0.5 + 0.5) * 3.14159265);\n"
    "    float a = uColor.a * vFade * across * (0.3 + 0.7 * along);\n"
    "    if (a < 0.004) discard;\n"
    "    gl_FragColor = vec4(uColor.rgb, a);\n"
    "}\n";

static DeckGlProgram sStreak;
static GLint sStTarget = -1, sStCam = -1, sStVel = -1, sStParams = -1, sStColor = -1;
static GLuint sStreakVbo = 0;

static void StreakInit(void)
{
    if (!DeckGl_BuildProgram(&sStreak, "rain-streaks", kStreakVs, kStreakFs))
    {
        DeckGl_Log("[RAIN] streak shader build failed: legacy rain particles stay\n");
        return;
    }
    sStTarget = DeckGl_Uniform(&sStreak, "uTarget");
    sStCam = DeckGl_Uniform(&sStreak, "uCam");
    sStVel = DeckGl_Uniform(&sStreak, "uVel");
    sStParams = DeckGl_Uniform(&sStreak, "uParams");
    sStColor = DeckGl_Uniform(&sStreak, "uColor");
    sStreakVbo = MakeQuadVbo(STREAK_COUNT, 0x5EED0001u);
    if (sStreakVbo == 0)
        DeckGl_DeleteProgram(&sStreak);
}

bool DeckRain_DrawStreaks(const DeckRainFrame *f)
{
    if (!DeckRain_Enabled() || sStreak.program == 0 || sStreakVbo == 0 || Off("streaks") || f->amount <= 0.005f)
        return false;

    const float amount = Clamp01(f->amount);
    int count = (int)(amount * (float)STREAK_COUNT + 0.5f);
    float alpha = 0.45f * (0.5f + 0.5f * amount) + 0.16f * Clamp01(f->bolt);

    if (count < STREAK_COUNT / 4)
        count = STREAK_COUNT / 4;
    if (alpha > 1.0f)
        alpha = 1.0f;

    DeckGl_UseProgram(&sStreak);
    DeckGl_Set3f(sStTarget, f->target[0], f->target[1], f->target[2]);
    DeckGl_Set3f(sStCam, f->cam[0], f->cam[1], f->cam[2]);
    DeckGl_Set3f(sStVel, -2.2f, -15.0f, 0.6f);
    DeckGl_Set3f(sStParams, 1.05f, 0.035f, 0.0022f);
    // rain colour (0xFFF4E8DE as DrawWeather) x day/night light
    DeckGl_Set4f(sStColor, 0.871f * f->light[0], 0.910f * f->light[1], 0.957f * f->light[2], alpha);

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_ALPHA_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    DrawQuadVbo(sStreakVbo, count);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    DeckGl_UseProgram(NULL);
    return true;
}

// ---- State ---------------------------------------------------------------------------------------------
void DeckRain_Init(void)
{
    const char *e = getenv("DECK_RAIN_OFF");

    sOff[0] = '\0';
    if (e != NULL)
        strncat(sOff, e, sizeof(sOff) - 1);
    sReady = false;
    StreakInit();
    sReady = sStreak.program != 0;
    DeckGl_Log("[RAIN] init: streaks %s, off list '%s'\n", sStreak.program != 0 ? "ready" : "failed", sOff);
}

void DeckRain_Shutdown(void)
{
    if (sStreak.program != 0)
        DeckGl_DeleteProgram(&sStreak);
    if (sStreakVbo != 0)
        glDeleteBuffers(1, &sStreakVbo);
    sStreakVbo = 0;
    sReady = false;
}

bool DeckRain_Enabled(void)
{
    return sReady && VitaExtras_GetFast(FAST_EXTRA_VOXEL_SHADERS) != 0 && VitaExtras_GetFast(FAST_EXTRA_VOXEL_RAIN) != 0;
}

void DeckRain_Update(bool rain, float amount, bool indoor, float seconds)
{
    (void)rain; (void)amount; (void)indoor; (void)seconds;
}
