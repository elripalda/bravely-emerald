#include "deck_flatfx.h"
#include "deck_gl_shader.h"
#include "deck_video.h"
#include "deck_levels.h"
#include "platform/vita_extras.h"
#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- Levels (LOW, MEDIUM, HIGH). Starting values, to tune by eye.
static const float kBloomAmt[3]   = { 0.18f, 0.28f, 0.40f };  // added bloom, times the blurred bright part
static const float kBloomThr[3]   = { 0.80f, 0.76f, 0.72f };  // luminance where the bright pass starts
static const float kGlowAmt[3]    = { 0.00f, 0.12f, 0.20f };  // wide soft halo around bright, saturated shapes (sprites, logo)
static const float kVignette[3]   = { 0.14f, 0.22f, 0.32f };
static const float kGrade[3]      = { 0.50f, 0.75f, 1.00f };  // split-tone strength
static const float kSaturation[3] = { 1.04f, 1.08f, 1.12f };
static const float kContrast[3]   = { 0.15f, 0.22f, 0.30f };  // S-curve mix
static const float kGrain[3]      = { 0.000f, 0.010f, 0.018f };
static const float kPulse[3]      = { 0.000f, 0.010f, 0.018f }; // exposure swing of the light pulse (x20 of it on the bloom)
static const float kShadowTint[3] = { 0.94f, 0.98f, 1.06f };
static const float kHighTint[3]   = { 1.05f, 1.01f, 0.94f };

static const char kVs[] =
    "#version 120\n"
    "varying vec2 vPos;\n"
    "void main() {\n"
    "    vPos = gl_Vertex.xy;\n"
    "    gl_Position = vec4(gl_Vertex.xy * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

// bright pass: bright or saturated texels
static const char kBrightFs[] =
    "#version 120\n"
    "uniform sampler2D uScene;\n"
    "uniform float uThreshold;\n"
    "varying vec2 vPos;\n"
    "void main() {\n"
    "    vec3 c = texture2D(uScene, vPos).rgb;\n"
    "    float l = dot(c, vec3(0.299, 0.587, 0.114));\n"
    "    float mx = max(c.r, max(c.g, c.b));\n"
    "    float k = smoothstep(uThreshold, 1.0, max(l, mx * 0.8));\n"
    "    gl_FragColor = vec4(c * k, 1.0);\n"
    "}\n";

static const char kCopyFs[] =
    "#version 120\n"
    "uniform sampler2D uScene;\n"
    "varying vec2 vPos;\n"
    "void main() { gl_FragColor = vec4(texture2D(uScene, vPos).rgb, 1.0); }\n";

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

static const char kCompositeFs[] =
    "#version 120\n"
    "uniform sampler2D uScene;\n"
    "uniform sampler2D uBloom;\n"
    "uniform sampler2D uGlow;\n"
    "uniform vec4 uA;\n"        // bloom amount, glow amount, vignette, grade strength
    "uniform vec4 uB;\n"        // saturation, contrast, grain, exposure (light pulse)
    "uniform vec3 uShadow;\n"
    "uniform vec3 uHigh;\n"
    "uniform vec3 uC;\n"        // aspect, clock seconds, window height in texels
    "varying vec2 vPos;\n"
    "float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }\n"
    "void main() {\n"
    "    vec3 c = texture2D(uScene, vPos).rgb;\n"
    "    float l = dot(c, vec3(0.299, 0.587, 0.114));\n"
    "    vec3 tint = mix(uShadow, uHigh, smoothstep(0.15, 0.85, l));\n"
    "    c = mix(c, c * tint, uA.w);\n"
    "    c = mix(vec3(l), c, uB.x);\n"
    "    c = mix(c, c * c * (3.0 - 2.0 * c), uB.y);\n"
    "    vec3 bl = texture2D(uBloom, vPos).rgb * uA.x + texture2D(uGlow, vPos).rgb * uA.y;\n"
    "    c += max(bl - c, 0.0);\n"
    "    c *= uB.w;\n"
    "    vec2 d = (vPos - 0.5) * vec2(uC.x, 1.0);\n"
    "    c *= 1.0 - uA.z * smoothstep(0.30, 1.05, length(d));\n"
    "    float n = hash(floor(vPos * vec2(uC.x * uC.z, uC.z)) + fract(uC.y * 0.37) * 91.7) - 0.5;\n"
    "    c += n * uB.z;\n"
    "    gl_FragColor = vec4(c, 1.0);\n"
    "}\n";

static bool sInit, sFailed, sEnvOff, sEnvRead;
static DeckGlTarget sCap, sHalf[2], sQuarter[2];
static DeckGlProgram sBright, sCopy, sBlur, sComp;
static GLint sBrightScene, sBrightThr, sCopyScene, sBlurSrc, sBlurStep;
static GLint sCScene, sCBloom, sCGlow, sCA, sCB, sCShadow, sCHigh, sCC;
static bool sActive;            // between Begin and End
static int sLvl;                // level of the active frame
static int sLoggedLvl = -9;
static const float kQuad[8] = { 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f };

static void DeleteTargets(void)
{
    DeckGl_DeleteTarget(&sCap);
    DeckGl_DeleteTarget(&sHalf[0]);
    DeckGl_DeleteTarget(&sHalf[1]);
    DeckGl_DeleteTarget(&sQuarter[0]);
    DeckGl_DeleteTarget(&sQuarter[1]);
}

static bool CreateTargets(int w, int h)
{
    const int hw = (w + 1) / 2, hh = (h + 1) / 2, qw = (w + 3) / 4, qh = (h + 3) / 4;

    return DeckGl_CreateTarget(&sCap, w, h, DECK_GL_DEPTH_NONE, GL_NEAREST)
        && DeckGl_CreateTarget(&sHalf[0], hw, hh, DECK_GL_DEPTH_NONE, GL_LINEAR)
        && DeckGl_CreateTarget(&sHalf[1], hw, hh, DECK_GL_DEPTH_NONE, GL_LINEAR)
        && DeckGl_CreateTarget(&sQuarter[0], qw, qh, DECK_GL_DEPTH_NONE, GL_LINEAR)
        && DeckGl_CreateTarget(&sQuarter[1], qw, qh, DECK_GL_DEPTH_NONE, GL_LINEAR);
}

static bool Build(void)
{
    if (!DeckGl_Init())
        return false;
    if (!DeckGl_BuildProgram(&sBright, "flat_bright", kVs, kBrightFs)
        || !DeckGl_BuildProgram(&sCopy, "flat_copy", kVs, kCopyFs)
        || !DeckGl_BuildProgram(&sBlur, "flat_blur", kVs, kBlurFs)
        || !DeckGl_BuildProgram(&sComp, "flat_composite", kVs, kCompositeFs))
        return false;
    sBrightScene = DeckGl_Uniform(&sBright, "uScene");
    sBrightThr = DeckGl_Uniform(&sBright, "uThreshold");
    sCopyScene = DeckGl_Uniform(&sCopy, "uScene");
    sBlurSrc = DeckGl_Uniform(&sBlur, "uSrc");
    sBlurStep = DeckGl_Uniform(&sBlur, "uStep");
    sCScene = DeckGl_Uniform(&sComp, "uScene");
    sCBloom = DeckGl_Uniform(&sComp, "uBloom");
    sCGlow = DeckGl_Uniform(&sComp, "uGlow");
    sCA = DeckGl_Uniform(&sComp, "uA");
    sCB = DeckGl_Uniform(&sComp, "uB");
    sCShadow = DeckGl_Uniform(&sComp, "uShadow");
    sCHigh = DeckGl_Uniform(&sComp, "uHigh");
    sCC = DeckGl_Uniform(&sComp, "uC");
    return true;
}

static void Fail(const char *why)
{
    DeckGl_Log("[FLATFX] %s: the 2D picture goes straight to the window (fallback)\n", why);
    sFailed = true;
    DeleteTargets();
}

int DeckFlat_Level(void)
{
    if (!sEnvRead)
    {
        const char *e = getenv("DECK_FLATFX_OFF");

        sEnvRead = true;
        sEnvOff = e != NULL && strcmp(e, "0") != 0;
        if (sEnvOff)
            DeckGl_Log("[FLATFX] off by DECK_FLATFX_OFF\n");
    }
    if (sEnvOff || VitaExtras_GetFast(FAST_EXTRA_VOXEL_SHADERS) == 0)
        return -1;
    return DeckLevel(FAST_EXTRA_VOXEL_FLATFX);
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

// Separable blur in place on t (tmp is the scratch). k scales the step (1 = one texel of t per tap pair); callers pass
// the window width / 960 so the radius on the picture does not depend on the window size.
static void Blur(const DeckGlTarget *t, const DeckGlTarget *tmp, int passes, float k)
{
    for (int i = 0; i < passes; i++)
    {
        BlurPass(tmp, t, k / (float)t->w, 0.0f);
        BlurPass(t, tmp, 0.0f, k / (float)t->h);
    }
}

bool DeckFlat_Begin(void)
{
    int vx, vy, vw, vh;
    GLfloat cc[4];

    sActive = false;
    if (sFailed)
        return false;
    sLvl = DeckFlat_Level();
    if (sLvl != sLoggedLvl)
    {
        DeckGl_Log("[FLATFX] level %d (-1 off, 0 low .. 2 high)\n", sLvl);
        sLoggedLvl = sLvl;
    }
    if (sLvl < 0)
        return false;
    if (!sInit)
    {
        sInit = true;
        if (!Build())
        {
            Fail("shader build failed");
            return false;
        }
        DeckGl_Log("[FLATFX] ready\n");
    }
    DeckVideo_GetViewport(&vx, &vy, &vw, &vh);
    if (vw < 64 || vh < 64)
        return false;
    if (sCap.fbo == 0 || sCap.w != vw || sCap.h != vh)
    {
        DeleteTargets();
        if (!CreateTargets(vw, vh))
        {
            Fail("capture target failed");
            return false;
        }
        DeckGl_Log("[FLATFX] capture target %dx%d (the letterboxed picture in window pixels)\n", vw, vh);
    }
    glGetFloatv(GL_COLOR_CLEAR_VALUE, cc);
    glDisable(GL_SCISSOR_TEST);
    BeginTarget(&sCap);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glClearColor(cc[0], cc[1], cc[2], cc[3]);
    sActive = true;
    return true;
}

void DeckFlat_End(void)
{
    const int l = sLvl;
    const float t = (float)SDL_GetTicks() / 1000.0f;
    const float k = (float)sCap.w / 960.0f;
    const float pulse = sinf(t * 0.9f);

    if (!sActive)
        return;
    sActive = false;
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);

    // bright pass at half resolution (bilinear read of the capture), then the bloom blur
    BindTex(0, sCap.color);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    BeginTarget(&sHalf[0]);
    DeckGl_UseProgram(&sBright);
    DeckGl_Set1i(sBrightScene, 0);
    DeckGl_Set1f(sBrightThr, kBloomThr[l]);
    DrawQuad();
    // the wide glow comes from the same bright part at quarter resolution, taken before the half-res blur
    if (kGlowAmt[l] > 0.0f)
    {
        BeginTarget(&sQuarter[0]);
        BindTex(0, sHalf[0].color);
        DeckGl_UseProgram(&sCopy);
        DeckGl_Set1i(sCopyScene, 0);
        DrawQuad();
        Blur(&sQuarter[0], &sQuarter[1], 2, 1.6f * k);
    }
    Blur(&sHalf[0], &sHalf[1], 2, 1.0f * k);

    // composite: the base copy is 1:1 and NEAREST, the effects ride on top
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    DeckGlViewport(0, 0, DECK_LOGICAL_WIDTH, DECK_LOGICAL_HEIGHT);
    BindTex(2, kGlowAmt[l] > 0.0f ? sQuarter[0].color : 0);
    BindTex(1, sHalf[0].color);
    BindTex(0, sCap.color);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    DeckGl_UseProgram(&sComp);
    DeckGl_Set1i(sCScene, 0);
    DeckGl_Set1i(sCBloom, 1);
    DeckGl_Set1i(sCGlow, 2);
    DeckGl_Set4f(sCA, kBloomAmt[l] * (1.0f + kPulse[l] * 20.0f * pulse), kGlowAmt[l], kVignette[l], kGrade[l]);
    DeckGl_Set4f(sCB, kSaturation[l], kContrast[l], kGrain[l], 1.0f + kPulse[l] * pulse);
    DeckGl_Set3f(sCShadow, kShadowTint[0], kShadowTint[1], kShadowTint[2]);
    DeckGl_Set3f(sCHigh, kHighTint[0], kHighTint[1], kHighTint[2]);
    DeckGl_Set3f(sCC, (float)sCap.w / (float)sCap.h, t, (float)sCap.h);
    DrawQuad();

    // the state RenderGameTexture leaves: no program, unit 0 with GL_TEXTURE_2D, ortho 960x544
    DeckGl_UseProgram(NULL);
    BindTex(2, 0);
    BindTex(1, 0);
    BindTex(0, 0);
    glEnable(GL_TEXTURE_2D);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 960.0, 544.0, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_BLEND);
}
