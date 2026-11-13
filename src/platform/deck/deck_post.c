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
