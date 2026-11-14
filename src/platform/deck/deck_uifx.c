#include "deck_uifx.h"
#include "deck_gl_shader.h"
#include "deck_levels.h"
#include "deck_video.h"
#include "platform/vita_daynight.h"
#include <SDL2/SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// ---------- Tuning (by level: low, medium, high) ----------
// Glass: blur passes (each a horizontal + vertical gaussian at half resolution), blur step in half-res texels, how far the glass
// is pulled toward the panel's own colour (readability), strength of the edge highlight, and the alpha of the panel fill drawn
// over it (the lower, the more glass shows).
static const int kGlassPasses[3] = { 1, 1, 2 };
static const float kGlassStep[3] = { 1.0f, 1.8f, 2.2f };
static const float kGlassBaseMix[3] = { 0.42f, 0.36f, 0.30f };
static const float kGlassEdge[3] = { 0.50f, 0.80f, 1.10f };
static const int kGlassFill[3] = { 200, 168, 142 };
// Tint: opacity of the gradient, drift speed (1 = about one slow sweep a minute), breathing, moving sheen band; fill alpha when
// only the tint is on.
static const float kTintAmt[3] = { 0.16f, 0.22f, 0.28f };
static const float kTintSpeed[3] = { 0.25f, 0.80f, 1.20f };
static const float kTintBreath[3] = { 0.0f, 1.0f, 1.0f };
static const float kTintSheen[3] = { 0.0f, 0.0f, 1.0f };
static const int kTintFill[3] = { 232, 215, 196 };

static int sEnvAll = -1, sEnvGl = -1;

static bool EnvSet(const char *name)
{
    const char *e = getenv(name);
    return e != NULL && strcmp(e, "0") != 0;
}

static void ReadEnv(void)
{
    if (sEnvAll >= 0)
        return;
    sEnvAll = EnvSet("DECK_UIFX_OFF");
    sEnvGl = EnvSet("DECK_UIGLASS_OFF");
    if (sEnvAll)
        DeckGl_Log("[UIFX] all off by DECK_UIFX_OFF\n");
    if (sEnvGl)
        DeckGl_Log("[UIFX] glass and tint off by DECK_UIGLASS_OFF\n");
}

int DeckUiFx_Level(FastExtraId id)
{
    ReadEnv();
    if (sEnvAll)
        return -1;
    return DeckLevel(id);
}

static int GlassLevel(void)
{
    ReadEnv();
    if (sEnvGl || VitaExtras_GetFast(FAST_EXTRA_VOXEL_SHADERS) == 0)
        return -1;
    return DeckUiFx_Level(FAST_EXTRA_VOXEL_UIGLASS);
}

static int TintLevel(void)
{
    ReadEnv();
    if (sEnvGl || VitaExtras_GetFast(FAST_EXTRA_VOXEL_SHADERS) == 0)
        return -1;
    return DeckUiFx_Level(FAST_EXTRA_VOXEL_UITINT);
}

bool DeckUiFx_PanelsActive(void)
{
    return GlassLevel() >= 0 || TintLevel() >= 0;
}

int DeckUiFx_FillAlpha(void)
{
    const int g = GlassLevel(), t = TintLevel();
    int a = 255;

    if (g >= 0)
        a = kGlassFill[g];
    if (t >= 0 && kTintFill[t] < a)
        a = kTintFill[t];
    return a;
}

int DeckUiFx_TextMode(void)
{
    const int l = DeckUiFx_Level(FAST_EXTRA_VOXEL_UITEXT);

    return l < 0 ? 0 : l + 1;
}

float DeckUiFx_Seconds(void)
{
    static Uint64 sStart;
    const Uint64 now = SDL_GetPerformanceCounter();

    if (sStart == 0)
        sStart = now;
    // wrap after about 3 hours so the float keeps its resolution; the shaders use sin of the value, a step at the wrap is a
    // few hundred thousand periods away from invisible
    return (float)fmod((double)(now - sStart) / (double)SDL_GetPerformanceFrequency(), 10000.0);
}

static float Smooth(float a, float b, float x)
{
    float t = (x - a) / (b - a);

    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

static uint32_t Shift(uint32_t c, float night, float dusk)
{
    float r = (float)(c & 0xFF), g = (float)((c >> 8) & 0xFF), b = (float)((c >> 16) & 0xFF);

    // dusk: warmer (more red, less blue); night: toward a deep blue
    r = r * (1.0f + 0.10f * dusk) * (1.0f - 0.35f * night) + 20.0f * night * 0.0f;
    g = g * (1.0f - 0.10f * dusk) * (1.0f - 0.20f * night);
    b = b * (1.0f - 0.25f * dusk) + (110.0f - b) * 0.40f * night;
    r = r > 255.0f ? 255.0f : (r < 0.0f ? 0.0f : r);
    g = g > 255.0f ? 255.0f : (g < 0.0f ? 0.0f : g);
    b = b > 255.0f ? 255.0f : (b < 0.0f ? 0.0f : b);
    return (uint32_t)(r + 0.5f) | ((uint32_t)(g + 0.5f) << 8) | ((uint32_t)(b + 0.5f) << 16) | 0xFF000000u;
}

void DeckUiFx_TintColours(uint32_t themeA, uint32_t themeB, uint32_t *outA, uint32_t *outB)
{
    const VitaDayNightLight *l = VitaDayNight_Get();
    float night = 0.0f, dusk = 0.0f;

    if (l != NULL)
    {
        night = 1.0f - Smooth(0.10f, 0.60f, l->daylight);
        dusk = l->glow > 1.0f ? 1.0f : (l->glow < 0.0f ? 0.0f : l->glow);
    }
    *outA = Shift(themeA, night, dusk);
    *outB = Shift(themeB, night, dusk);
}

// ---------- GL pass ----------

static const char *kVs =
    "#version 120\n"
    "varying vec2 vUv;\n"
    "varying vec2 vPos;\n"
    "void main() {\n"
    "    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
    "    vUv = gl_MultiTexCoord0.xy;\n"
    "    vPos = gl_Vertex.xy;\n"
    "}\n";

static const char *kBlurFs =
    "#version 120\n"
    "uniform sampler2D uTex;\n"
    "uniform vec2 uDir;\n"
    "varying vec2 vUv;\n"
    "void main() {\n"
    "    vec3 c = texture2D(uTex, vUv).rgb * 0.2270270270;\n"
    "    c += (texture2D(uTex, vUv + uDir * 1.3846153846).rgb + texture2D(uTex, vUv - uDir * 1.3846153846).rgb) * 0.3162162162;\n"
    "    c += (texture2D(uTex, vUv + uDir * 3.2307692308).rgb + texture2D(uTex, vUv - uDir * 3.2307692308).rgb) * 0.0702702703;\n"
    "    gl_FragColor = vec4(c, 1.0);\n"
    "}\n";

// Glass: the blurred picture behind the panel, pulled toward the panel colour, with the tint gradient and an edge highlight,
// cut to the panel shape (rounded or chamfered). Mode 1 (tint only) draws just the gradient over what is there.
static const char *kGlassFs =
    "#version 120\n"
    "uniform sampler2D uBlur;\n"
    "uniform vec4 uRect;\n"     // x, y, w, h in logical px
    "uniform vec4 uShape;\n"    // radius, chamfer, fade, mode
    "uniform vec4 uBase;\n"     // rgb of the panel, mix toward it
    "uniform vec3 uColA;\n"
    "uniform vec3 uColB;\n"
    "uniform vec4 uAnim;\n"     // time, drift speed, breathing, sheen
    "uniform vec2 uTint;\n"     // tint opacity, edge highlight
    "varying vec2 vPos;\n"
    "float sdRound(vec2 p, vec2 b, float r) {\n"
    "    vec2 q = abs(p) - b + vec2(r);\n"
    "    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;\n"
    "}\n"
    "float sdCham(vec2 p, vec2 b, float r) {\n"
    "    vec2 q = abs(p) - b;\n"
    "    return max(max(q.x, q.y), (q.x + q.y + r) * 0.70710678);\n"
    "}\n"
    "void main() {\n"
    "    vec2 half_ = uRect.zw * 0.5;\n"
    "    vec2 p = vPos - (uRect.xy + half_);\n"
    "    float r = min(uShape.x, min(half_.x, half_.y));\n"
    "    float d = uShape.y > 0.5 ? sdCham(p, half_, r) : sdRound(p, half_, r);\n"
    "    float inside = 1.0 - smoothstep(-0.6, 0.4, d);\n"
    "    if (inside <= 0.0) discard;\n"
    "    vec2 rel = (vPos - uRect.xy) / uRect.zw;\n"
    "    float t = uAnim.x * uAnim.y;\n"
    "    float ang = 0.60 + 0.45 * sin(t * 0.21);\n"
    "    vec2 dir = vec2(cos(ang), sin(ang));\n"
    "    float g = dot(rel - 0.5, dir) * 1.15 + 0.5 + 0.20 * sin(t * 0.37 + rel.x * 2.3 + rel.y * 1.7);\n"
    "    g = clamp(g, 0.0, 1.0);\n"
    "    g = g * g * (3.0 - 2.0 * g);\n"
    "    vec3 tint = mix(uColA, uColB, g);\n"
    "    float amt = uTint.x * (1.0 + uAnim.z * 0.28 * sin(uAnim.x * 0.9));\n"
    "    if (uShape.w > 0.5) {\n"
    "        float band = exp(-pow((fract(rel.x * 0.7 + rel.y * 0.35 - uAnim.x * 0.045) - 0.5) * 5.0, 2.0));\n"
    "        gl_FragColor = vec4(tint + vec3(band * uAnim.w * 0.25), (amt + band * uAnim.w * 0.05) * inside * uShape.z);\n"
    "        return;\n"
    "    }\n"
    "    vec2 uv = vec2(vPos.x / 960.0, 1.0 - vPos.y / 544.0);\n"
    "    vec3 bg = texture2D(uBlur, uv).rgb;\n"
    "    float l = dot(bg, vec3(0.299, 0.587, 0.114));\n"
    "    bg = mix(vec3(l), bg, 1.08) * 1.03;\n"
    "    vec3 col = mix(bg, uBase.rgb, uBase.a);\n"
    "    col = mix(col, tint, amt);\n"
    "    float band = exp(-pow((fract(rel.x * 0.7 + rel.y * 0.35 - uAnim.x * 0.045) - 0.5) * 5.0, 2.0));\n"
    "    col += vec3(band * uAnim.w * 0.06);\n"
    "    float edge = smoothstep(-3.0, -0.6, d);\n"
    "    float k = clamp(dot(p / max(half_, vec2(1.0)), vec2(0.7, 0.7)), -1.0, 1.0);\n"
    "    col += vec3(uTint.y * edge * (0.12 - 0.12 * k));\n"
    "    gl_FragColor = vec4(col, inside * uShape.z);\n"
    "}\n";

static struct
{
    bool tried, ok;
    DeckGlProgram blur, glass;
    GLint uBlurTex, uBlurDir;
    GLint uBlurSamp, uRect, uShape, uBase, uColA, uColB, uAnim, uTint;
    GLuint cap;
    int capW, capH;
    DeckGlTarget a, b;
    // frame state
    bool began, glassOn, haveBlur;
    int glassLvl, tintLvl;
    GLint vp[4];
    GLboolean scissor;
    GLint scBox[4];
    int logged;
} s;

static void DestroyTargets(void)
{
    if (s.cap != 0)
        glDeleteTextures(1, &s.cap);
    s.cap = 0;
    DeckGl_DeleteTarget(&s.a);
    DeckGl_DeleteTarget(&s.b);
    s.capW = s.capH = 0;
}

static bool EnsureProgram(void)
{
    if (s.tried)
        return s.ok;
    s.tried = true;
    if (!DeckGl_BuildProgram(&s.blur, "uiblur", kVs, kBlurFs) || !DeckGl_BuildProgram(&s.glass, "uiglass", kVs, kGlassFs))
    {
        DeckGl_Log("[UIFX] shader build failed: glass and tint stay off (the UI is drawn as before)\n");
        return false;
    }
    s.uBlurSamp = DeckGl_Uniform(&s.blur, "uTex");
    s.uBlurDir = DeckGl_Uniform(&s.blur, "uDir");
    s.uBlurTex = DeckGl_Uniform(&s.glass, "uBlur");
    s.uRect = DeckGl_Uniform(&s.glass, "uRect");
    s.uShape = DeckGl_Uniform(&s.glass, "uShape");
    s.uBase = DeckGl_Uniform(&s.glass, "uBase");
    s.uColA = DeckGl_Uniform(&s.glass, "uColA");
    s.uColB = DeckGl_Uniform(&s.glass, "uColB");
    s.uAnim = DeckGl_Uniform(&s.glass, "uAnim");
    s.uTint = DeckGl_Uniform(&s.glass, "uTint");
    s.ok = true;
    DeckGl_Log("[UIFX] ready\n");
    return true;
}

static bool EnsureTargets(int vw, int vh)
{
    if (s.cap != 0 && s.capW == vw && s.capH == vh)
        return s.a.complete && s.b.complete;
    DestroyTargets();
    glGenTextures(1, &s.cap);
    glBindTexture(GL_TEXTURE_2D, s.cap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, vw, vh, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    s.capW = vw;
    s.capH = vh;
    const bool a = DeckGl_CreateTarget(&s.a, vw / 2, vh / 2, DECK_GL_DEPTH_NONE, GL_LINEAR);
    const bool b = DeckGl_CreateTarget(&s.b, vw / 2, vh / 2, DECK_GL_DEPTH_NONE, GL_LINEAR);
    DeckGl_Log("[UIFX] targets %dx%d (blur %dx%d): %s\n", vw, vh, vw / 2, vh / 2, (a && b) ? "ok" : "FAILED");
    return a && b;
}

static void Quad01(void)
{
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, 0.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(1.0f, 0.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(1.0f, 1.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, 1.0f);
    glEnd();
}

// One blur pass over the target: src texture -> dst target, direction in texels of the source.
static void BlurPass(GLuint srcTex, const DeckGlTarget *dst, float dx, float dy)
{
    glBindFramebuffer(GL_FRAMEBUFFER, dst->fbo);
    glViewport(0, 0, dst->w, dst->h);
    glBindTexture(GL_TEXTURE_2D, srcTex);
    DeckGl_Set2f(s.uBlurDir, dx, dy);
    Quad01();
}

bool DeckUiFx_Begin(const DeckUiPanel *panels, int count)
{
    (void)panels;
    s.began = false;
    if (count <= 0 || !DeckUiFx_PanelsActive() || !EnsureProgram())
        return false;
    s.glassLvl = GlassLevel();
    s.tintLvl = TintLevel();

    GLint fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
    if (fbo != 0)
        return false;   // only the window framebuffer is copied

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
    glGetIntegerv(GL_VIEWPORT, s.vp);
    s.scissor = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_SCISSOR_BOX, s.scBox);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();

    s.glassOn = false;
    s.haveBlur = false;
    if (s.glassLvl >= 0)
    {
        int vx, vy, vw, vh, winW, winH;

        DeckVideo_GetViewport(&vx, &vy, &vw, &vh);
        SDL_GL_GetDrawableSize(DeckVideo_GetWindow(), &winW, &winH);
        if (vw >= 64 && vh >= 36 && EnsureTargets(vw, vh))
        {
            const int glY = winH - (vy + vh);
            const int lvl = s.glassLvl;

            glDisable(GL_SCISSOR_TEST);
            glDisable(GL_BLEND);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_ALPHA_TEST);
            glDisable(GL_CULL_FACE);
            DeckGl_ActiveTexture(GL_TEXTURE0);
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, s.cap);
            glReadBuffer(GL_BACK);
            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vx, glY, vw, vh);

            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glOrtho(0.0, 1.0, 0.0, 1.0, -1.0, 1.0);
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            DeckGl_UseProgram(&s.blur);
            DeckGl_Set1i(s.uBlurSamp, 0);
            const float st = kGlassStep[lvl];
            // down to half resolution (bilinear, no offset), then gaussian passes
            BlurPass(s.cap, &s.a, 0.0f, 0.0f);
            for (int i = 0; i < kGlassPasses[lvl]; i++)
            {
                const float k = st * (1.0f + 0.8f * (float)i);

                BlurPass(s.a.color, &s.b, k / (float)s.a.w, 0.0f);
                BlurPass(s.b.color, &s.a, 0.0f, k / (float)s.a.h);
            }
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            DeckGl_UseProgram(NULL);
            s.glassOn = true;
            s.haveBlur = true;
        }
    }

    // back to the caller's state; each panel sets what it needs itself
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glPopClientAttrib();
    glPopAttrib();
    glViewport(s.vp[0], s.vp[1], s.vp[2], s.vp[3]);
    s.began = true;
    if (s.logged < 3)
    {
        DeckGl_Log("[UIFX] panels %d glass level %d tint level %d\n", count, s.glassLvl, s.tintLvl);
        s.logged++;
    }
    return true;
}

static void Rgb(uint32_t c, float *r, float *g, float *b)
{
    *r = (float)(c & 0xFF) / 255.0f;
    *g = (float)((c >> 8) & 0xFF) / 255.0f;
    *b = (float)((c >> 16) & 0xFF) / 255.0f;
}

void DeckUiFx_Panel(const DeckUiPanel *p, float fade, float slideY)
{
    if (!s.began || fade <= 0.0f)
        return;
    const int g = s.glassLvl, t = s.tintLvl;
    const bool glass = s.glassOn && g >= 0;
    const int lv = t >= 0 ? t : (g >= 0 ? g : 0);
    float r, gr, b, ar, ag, ab, br, bg_, bb;

    if (!glass && t < 0)
        return;
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, 960.0, 544.0, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glTranslatef(0.0f, slideY, 0.0f);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    DeckGl_ActiveTexture(GL_TEXTURE0);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, s.haveBlur ? s.a.color : 0);
    DeckGl_UseProgram(&s.glass);
    DeckGl_Set1i(s.uBlurTex, 0);
    // the shader works in the panel's own (untranslated) coordinates: the glass samples the picture where the panel was
    DeckGl_Set4f(s.uRect, p->x, p->y, p->w, p->h);
    DeckGl_Set4f(s.uShape, p->radius, p->chamfer ? 1.0f : 0.0f, fade, glass ? 0.0f : 1.0f);
    Rgb(p->base, &r, &gr, &b);
    DeckGl_Set4f(s.uBase, r, gr, b, glass ? kGlassBaseMix[g] : 0.0f);
    Rgb(p->colA, &ar, &ag, &ab);
    Rgb(p->colB, &br, &bg_, &bb);
    DeckGl_Set3f(s.uColA, ar, ag, ab);
    DeckGl_Set3f(s.uColB, br, bg_, bb);
    DeckGl_Set4f(s.uAnim, DeckUiFx_Seconds(), t >= 0 ? kTintSpeed[t] : 0.25f, t >= 0 ? kTintBreath[t] : 0.0f,
                 t >= 0 ? kTintSheen[t] : 0.0f);
    DeckGl_Set2f(s.uTint, t >= 0 ? kTintAmt[t] : 0.0f, glass ? kGlassEdge[g] : 0.0f);
    glBegin(GL_QUADS);
    glVertex2f(p->x - 1.0f, p->y - 1.0f);
    glVertex2f(p->x + p->w + 1.0f, p->y - 1.0f);
    glVertex2f(p->x + p->w + 1.0f, p->y + p->h + 1.0f);
    glVertex2f(p->x - 1.0f, p->y + p->h + 1.0f);
    glEnd();
    DeckGl_UseProgram(NULL);
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glPopAttrib();
}

void DeckUiFx_End(void)
{
    s.began = false;
}
