#include "deck_post.h"
#include "deck_gl_shader.h"
#include "deck_video.h"
#include "deck_levels.h"
#include "deck_sunshadow.h"
#include "platform/vita_extras.h"
#include "../vita/vita_perf.h"
#include <math.h>
#include <stdio.h>
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

// Bloom gain by level (the old preset values, but LOW now glows a little instead of nothing).
static const float kBloomGain[3] = { 1.5f, 3.0f, 5.0f };
// Share of the diorama blur at the band edges by level (MEDIUM and HIGH are the old look).
static const float kBlurMul[3] = { 0.5f, 1.0f, 1.0f };
// Light shaft gain by level (times the frame's sun amount).
static const float kShaftGain[3] = { 0.125f, 0.235f, 0.325f };
// Beam layers by level (item 5), and the colours: warm day, golden dawn and dusk, pale blue moon.
static const float kShaftLayers[3] = { 3.0f, 4.0f, 6.0f };
static const float kShaftDay[3] = { 1.0f, 0.92f, 0.74f };
static const float kShaftGold[3] = { 1.0f, 0.72f, 0.38f };
static const float kShaftMoon[3] = { 0.52f, 0.66f, 1.0f };
// LUT: gentle S-curve strength by time of day (day, dusk, night) and the level factor.
static const float kLutCurve[3] = { 0.20f, 0.32f, 0.14f };
static const float kLutLevel[3] = { 0.5f, 1.0f, 1.3f };
// Tilt-shift by level: largest gather radius (fraction of the picture height at full circle of confusion) and taps.
static const float kTiltRadius[3] = { 0.6f * 0.014f, 0.014f, 1.4f * 0.014f };
static const float kTiltTaps[3] = { 8.0f, 12.0f, 16.0f };
// 3D battle backdrop (voxel_battledim) by level: blur passes at half resolution, their step, and the black overlay opacity.
static const int kBackPasses[3] = { 1, 2, 3 };
static const float kBackStep[3] = { 1.0f, 1.0f, 1.6f };
static const float kBackDim[3] = { 0.25f, 0.40f, 0.55f };

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

// Light shafts (item 5): procedural beams at half resolution, not a march over the scene. Layers of slanted bands along the
// light direction (aspect corrected, the lines run along uDir = the projected sun or moon direction), each with its own density, drift speed and breathing,
// strongest at the top of the picture, with dust sparkles inside the beams. uCol carries the colour (warm by day, golden at
// dawn and dusk, pale blue for the moon); uMotion is 1 for the sun and about 0.45 for the slower moon.
static const char kShaftFs[] =
    "#version 120\n"
    "uniform vec2 uDir;\n"         // unit screen direction toward the light (x right, y up), from the real camera
    "uniform float uLen;\n"        // falloff exponent: small (long beams) for a low sun
    "uniform float uTime;\n"
    "uniform float uAsp;\n"
    "uniform float uCount;\n"      // layers (3, 4 or 6)
    "uniform float uMotion;\n"
    "uniform vec3 uCol;\n"
    "varying vec2 vPos;\n"
    "float hh(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }\n"
    "void main() {\n"
    "    vec2 dir = normalize(uDir);\n"
    "    float a = dot(vec2(vPos.x * uAsp, vPos.y), vec2(dir.y, -dir.x));\n"   // across the beams (x for vertical ones)
    "    float t = uTime * uMotion;\n"
    "    float acc = 0.0;\n"
    "    for (int i = 0; i < 6; i++) {\n"
    "        if (float(i) >= uCount) break;\n"
    "        float fi = float(i);\n"
    "        float dir = mod(fi, 2.0) < 0.5 ? 1.0 : -1.0;\n"
    "        float pos = a * (1.1 + fi * 0.55) + fi * 13.1 + t * dir * (0.020 + 0.013 * fi);\n"
    "        float cell = floor(pos);\n"
    "        float fr = pos - cell;\n"
    "        float h1 = hh(vec2(cell, fi));\n"
    "        float h2 = hh(vec2(cell + 7.0, fi * 3.0));\n"
    "        float width = 0.05 + 0.13 * h2;\n"
    "        float prof = step(0.40, h1) * (1.0 - smoothstep(0.0, width, abs(fr - (0.25 + 0.5 * h1))));\n"
    "        float breath = 0.55 + 0.45 * sin(t * (0.30 + 0.19 * fi) + h1 * 6.2831);\n"    // slow intensity breathing
    "        acc += prof * breath * (1.0 - 0.07 * fi);\n"
    "    }\n"
    "    float fall = pow(clamp(vPos.y, 0.0, 1.0), uLen);\n"                                // strong at the top, gone at the bottom
    "    float beam = clamp(acc * 0.55, 0.0, 1.0) * fall;\n"
    "    float dust = step(0.955, hh(floor(vPos * vec2(190.0, 108.0)) + floor(t * 2.5))) * beam * 1.6;\n"  // shimmering motes
    "    gl_FragColor = vec4(uCol * (beam + dust), 1.0);\n"
    "}\n";

// Composite body. Built twice (BuildPrograms): with POST_X defined (tilt-shift and the other phase 7 features)
// and, if that fails to compile, without it (the phase 1 composite).
// uDof: x top band height, y bottom band height (fractions of the frame), z/w blur at the edges.
// Same band shape as Pass_DepthOfField (linear ramp from the edge alpha to 0), smooth blur.
static const char kCompositeBody[] =
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
    "uniform vec2 uBack;   // battle backdrop: x blur amount, y black overlay opacity\n"
    "#ifdef POST_X\n"
    "uniform sampler2D uDepth;\n"
    "uniform vec4 uTilt;   // x on, y focus distance, z max radius (fraction of the height), w taps\n"
    "uniform vec2 uZ;      // near, far\n"
    "uniform sampler2D uShafts;\n"
    "uniform sampler2D uLut;\n"
    "uniform vec2 uLutOn;  // x on, y strength (the fade)\n"
    "uniform vec4 uShaft;  // x amount\n"
    "#endif\n"
    "#ifdef POST_SUN\n"
    "uniform sampler2D uSunMap;\n"
    "uniform mat4 uInvVP;   // camera clip -> world\n"
    "uniform mat4 uSunVP;   // world -> sun clip\n"
    "uniform vec4 uSunA;    // x on (sun amount), y texel (uv), z PCF radius (0: 2x2, 1: 3x3, 2: 5x5), w multiplier in full shadow\n"
    "uniform vec4 uSunB;    // x depth range (world), y texel (world), z tap spacing (texels)\n"
    "uniform vec3 uSunL;    // toward the sun\n"
    "#endif\n"
    "varying vec2 vPos;\n"
    "void main() {\n"
    "    vec3 c = texture2D(uScene, vPos).rgb;\n"
    "#ifdef POST_X\n"
    "    if (uTilt.x > 0.5) {\n"
    "        float d = texture2D(uDepth, vPos).r;\n"
    "        float z = 2.0 * uZ.x * uZ.y / (uZ.y + uZ.x - (2.0 * d - 1.0) * (uZ.y - uZ.x));\n"
    "        float rel = (z - uTilt.y) / uTilt.y;\n"
    "        rel = rel < 0.0 ? -rel * 1.4 : rel;\n"
    "        float coc = d >= 0.9999 ? 0.0 : smoothstep(0.06, 0.40, rel);\n"   // sky (cleared depth) stays sharp
    "        if (coc > 0.01) {\n"
    "            vec2 rad = vec2(1.0 / uAspect, 1.0) * (coc * uTilt.z);\n"
    "            vec3 acc = vec3(0.0);\n"
    "            float wsum = 0.0;\n"
    "            for (int i = 0; i < 16; i++) {\n"
    "                if (float(i) >= uTilt.w) break;\n"
    "                float fi = float(i) + 0.5;\n"
    "                float a = fi * 2.39996323;\n"
    "                vec3 s = texture2D(uBlur, vPos + vec2(cos(a), sin(a)) * sqrt(fi / uTilt.w) * rad).rgb;\n"
    "                float l = dot(s, vec3(0.299, 0.587, 0.114));\n"
    "                float w = 1.0 + 3.0 * l * l;\n"
    "                acc += s * w;\n"
    "                wsum += w;\n"
    "            }\n"
    "            c = mix(c, acc / wsum, coc);\n"
    "        }\n"
    "    }\n"
    "#endif\n"
    "#ifdef POST_SUN\n"
    "    if (uSunA.x > 0.0) {\n"
    "        float sd = texture2D(uDepth, vPos).r;\n"
    "        vec4 wq = uInvVP * vec4(vPos * 2.0 - 1.0, sd * 2.0 - 1.0, 1.0);\n"
    "        vec3 wp = wq.xyz / wq.w;\n"
    "        vec3 nn = cross(dFdx(wp), dFdy(wp));\n"
    "        float nl = length(nn);\n"
    "        nn = nl > 1.0e-12 ? nn / nl : vec3(0.0, 1.0, 0.0);\n"
    "        vec4 cq = uInvVP * vec4(0.0, 0.0, -1.0, 1.0);\n"          // a point on the near plane: the eye side
    "        if (dot(nn, wp - cq.xyz / cq.w) > 0.0) nn = -nn;\n"      // face the viewer whatever the derivative handedness
    "        float ndl = dot(nn, uSunL);\n"
    "        if (sd < 0.9999 && ndl > 0.05) {\n"
    "            float sinT = sqrt(max(1.0 - ndl * ndl, 0.0));\n"
    "            float tw = uSunB.y;\n"
    "            float bias = min(tw * (1.0 + 3.0 * sinT / max(ndl, 0.25)), tw * 10.0);\n"
    "            vec4 sc = uSunVP * vec4(wp + nn * (tw * 1.2 * sinT), 1.0);\n"
    "            vec2 suv = sc.xy * 0.5 + 0.5;\n"
    "            float sz = (sc.z * 0.5 + 0.5) * uSunB.x - bias;\n"
    "            float edge = min(min(suv.x, 1.0 - suv.x), min(suv.y, 1.0 - suv.y));\n"
    "            float acc = 0.0;\n"
    "            float cnt = 0.0;\n"
    "            if (edge > 0.0) {\n"
    "                if (uSunA.z < 0.5) {\n"
    "                    for (int j = 0; j < 2; j++) {\n"
    "                        for (int i = 0; i < 2; i++) {\n"
    "                            vec2 o = (vec2(float(i), float(j)) - 0.5) * uSunA.y * uSunB.z;\n"
    "                            acc += (sz > texture2D(uSunMap, suv + o).r * uSunB.x) ? 1.0 : 0.0;\n"
    "                            cnt += 1.0;\n"
    "                        }\n"
    "                    }\n"
    "                } else {\n"
    "                    for (int j = -2; j <= 2; j++) {\n"
    "                        for (int i = -2; i <= 2; i++) {\n"
    "                            if (abs(float(i)) > uSunA.z || abs(float(j)) > uSunA.z) continue;\n"
    "                            vec2 o = vec2(float(i), float(j)) * uSunA.y * uSunB.z;\n"
    "                            acc += (sz > texture2D(uSunMap, suv + o).r * uSunB.x) ? 1.0 : 0.0;\n"
    "                            cnt += 1.0;\n"
    "                        }\n"
    "                    }\n"
    "                }\n"
    "                c *= mix(1.0, uSunA.w, (acc / cnt) * smoothstep(0.0, 0.08, edge));\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "#endif\n"
    "    if (uBlurOn > 0.5) {\n"
    "        float wTop = uDof.z * clamp((vPos.y - (1.0 - uDof.x)) / uDof.x, 0.0, 1.0);\n"
    "        float wBot = uDof.w * clamp((uDof.y - vPos.y) / uDof.y, 0.0, 1.0);\n"
    "        c = mix(c, texture2D(uBlur, vPos).rgb, max(wTop, wBot));\n"
    "    }\n"
    "    c += texture2D(uBloom, vPos).rgb * uBloomAmt;\n"
    "    if (uBack.x > 0.0) {\n"
    "        c = mix(c, texture2D(uBlur, vPos).rgb, uBack.x);\n"
    "        c *= 1.0 - uBack.y;\n"
    "    }\n"
    "#ifdef POST_X\n"
    "    if (uShaft.x > 0.0)\n"
    "        c += texture2D(uShafts, vPos).rgb * uShaft.x;\n"
    "#endif\n"
    "#ifdef POST_X\n"
    "    if (uLutOn.x > 0.5) {\n"
    "        vec3 q = clamp(c, 0.0, 1.0) * 15.0;\n"
    "        float s0 = floor(q.b);\n"
    "        float s1 = min(s0 + 1.0, 15.0);\n"
    "        float f = q.b - s0;\n"
    "        vec2 uv0 = vec2((s0 * 16.0 + q.r + 0.5) / 256.0, (q.g + 0.5) / 16.0);\n"
    "        vec2 uv1 = vec2((s1 * 16.0 + q.r + 0.5) / 256.0, (q.g + 0.5) / 16.0);\n"
    "        c = mix(c, mix(texture2D(uLut, uv0).rgb, texture2D(uLut, uv1).rgb, f), uLutOn.y);\n"
    "    } else\n"
    "#endif\n"
    "    {\n"
    "        float l = dot(c, vec3(0.299, 0.587, 0.114));\n"
    "        vec3 tint = mix(uShadowTint, uHighTint, smoothstep(0.15, 0.75, l));\n"
    "        vec3 g = mix(vec3(l), c * tint, uSaturation);\n"
    "        c = mix(c, g, uGradeAmt);\n"
    "    }\n"
    "    vec2 d = (vPos - 0.5) * vec2(uAspect, 1.0);\n"
    "    float v = 1.0 - smoothstep(0.35, 0.95, length(d));\n"
    "    c *= mix(1.0, v, uVignette);\n"
    "    gl_FragColor = vec4(clamp(c, 0.0, 1.0), 1.0);\n"
    "}\n";

#define DECK_BASE_W 960   // the logical picture and the OFF/LOW scene width
#define DECK_BASE_H 544

static bool sReady;
static void DeleteTargets(void);
static bool CreateLut(void);
static bool sXOk;      // the composite with POST_X built
static bool sSunOk;    // the composite has the sun shadow resolve (POST_SUN)
static GLint sCBack;
static GLint sCSunMap, sCInvVP, sCSunVP, sCSunA, sCSunB, sCSunL;
static bool sShaftOk;  // the shaft program built and the composite has POST_X
static DeckGlProgram sShaft;
static GLint sShaftSun, sShaftLen, sShaftTime, sShaftAsp, sShaftCount, sShaftMotion, sShaftCol, sCShafts, sCShaft;
static bool sLutOk;    // the LUT texture exists and the composite has POST_X
static GLuint sLutTex;
static float sLutDay = -1.0f, sLutGlow = -1.0f;   // the parameters the table was last built for
static int sLutGradeLvl = -2, sLutLvl = -2;
static bool sTiltOk;   // tilt-shift can run: POST_X composite and a depth texture on the scene target
static bool sSizeWarned;
static int sFailW;     // a scene width that could not be created (not tried again)
static DeckGlTarget sScene;
static DeckGlTarget sHalf[4]; // 0 bloom, 1 blur scratch, 2 DoF / tilt-shift copy, 3 light shafts
static DeckGlProgram sBright, sCopy, sBlur, sComposite;
static GLint sBrightScene, sBrightThreshold, sCopyScene, sBlurSrc, sBlurStep;
static GLint sCScene, sCBloom, sCBlur, sCBloomAmt, sCBlurOn, sCDof, sCGradeAmt, sCShadow, sCHigh, sCSat, sCVignette, sCAspect, sCDepth, sCTilt, sCZ, sCLut, sCLutOn;

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
    c.gradeLvl = DeckLevel(FAST_EXTRA_VOXEL_GRADE);
    c.bloomLvl = DeckLevel(FAST_EXTRA_VOXEL_BLOOM);
    c.blurLvl = DeckLevel(FAST_EXTRA_VOXEL_BLUR);
    c.tiltLvl = DeckLevel(FAST_EXTRA_VOXEL_TILTSHIFT);
    c.shaftLvl = DeckLevel(FAST_EXTRA_VOXEL_SHAFTS);
    c.lutLvl = DeckLevel(FAST_EXTRA_VOXEL_LUT);
    c.sunLvl = DeckSunShadow_Level();
    c.dimLvl = DeckLevel(FAST_EXTRA_VOXEL_BATTLEDIM);
    c.grade = c.gradeLvl >= 0;
    c.bloom = c.bloomLvl >= 0;
    c.blur = c.blurLvl >= 0;
    c.tilt = c.tiltLvl >= 0;
    c.shafts = c.shaftLvl >= 0;
    c.lut = c.lutLvl >= 0;
    c.sun = c.sunLvl >= 0;
    return c;
}

static bool BuildPrograms(void)
{
    static char src[12288];
    const char *noX = getenv("DECK_POST_NO_X");

    if (!DeckGl_BuildProgram(&sBright, "post_bright", kFullscreenVs, kBrightFs)
        || !DeckGl_BuildProgram(&sCopy, "post_copy", kFullscreenVs, kCopyFs)
        || !DeckGl_BuildProgram(&sBlur, "post_blur", kFullscreenVs, kBlurFs))
        return false;
    // The extended composite first; if it does not build (or DECK_POST_NO_X), the phase 1 composite.
    sXOk = false;
    sSunOk = false;
    if (noX == NULL || strcmp(noX, "0") == 0)
    {
        // POST_X with the sun shadow resolve first, then POST_X alone, then the phase 1 composite
        for (int withSun = 1; withSun >= 0 && !sXOk; withSun--)
        {
            if (snprintf(src, sizeof(src), "#version 120\n#define POST_X 1\n%s%s", withSun ? "#define POST_SUN 1\n" : "", kCompositeBody) >= (int)sizeof(src))
            {
                DeckGl_Log("[POST] composite source truncated (%d bytes): not built\n", (int)sizeof(src));
                return false;
            }
            sXOk = DeckGl_BuildProgram(&sComposite, withSun ? "post_composite_xs" : "post_composite_x", kFullscreenVs, src);
            sSunOk = sXOk && withSun;
            if (!sXOk)
                DeckGl_Log("[POST] composite %s failed\n", withSun ? "with the sun shadow (POST_SUN): trying without" : "extended: plain composite");
        }
    }
    if (!sXOk)
    {
        if (snprintf(src, sizeof(src), "#version 120\n%s", kCompositeBody) >= (int)sizeof(src))
        {
            DeckGl_Log("[POST] composite source truncated (%d bytes): not built\n", (int)sizeof(src));
            return false;
        }
        if (!DeckGl_BuildProgram(&sComposite, "post_composite", kFullscreenVs, src))
            return false;
    }
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
    sCBack = DeckGl_Uniform(&sComposite, "uBack");
    sCDepth = DeckGl_Uniform(&sComposite, "uDepth");   // -1 in the plain composite (setters ignore it)
    sCTilt = DeckGl_Uniform(&sComposite, "uTilt");
    sCZ = DeckGl_Uniform(&sComposite, "uZ");
    sCShafts = DeckGl_Uniform(&sComposite, "uShafts");
    sCShaft = DeckGl_Uniform(&sComposite, "uShaft");
    sCLut = DeckGl_Uniform(&sComposite, "uLut");
    sCLutOn = DeckGl_Uniform(&sComposite, "uLutOn");
    sCSunMap = DeckGl_Uniform(&sComposite, "uSunMap");
    sCInvVP = DeckGl_Uniform(&sComposite, "uInvVP");
    sCSunVP = DeckGl_Uniform(&sComposite, "uSunVP");
    sCSunA = DeckGl_Uniform(&sComposite, "uSunA");
    sCSunB = DeckGl_Uniform(&sComposite, "uSunB");
    sCSunL = DeckGl_Uniform(&sComposite, "uSunL");
    // The shaft pass is optional: a failed build only switches the shafts off.
    sShaftOk = sXOk && DeckGl_BuildProgram(&sShaft, "post_shaft", kFullscreenVs, kShaftFs);
    if (sShaftOk)
    {
        sShaftSun = DeckGl_Uniform(&sShaft, "uDir");
        sShaftLen = DeckGl_Uniform(&sShaft, "uLen");
        sShaftTime = DeckGl_Uniform(&sShaft, "uTime");
        sShaftAsp = DeckGl_Uniform(&sShaft, "uAsp");
        sShaftCount = DeckGl_Uniform(&sShaft, "uCount");
        sShaftMotion = DeckGl_Uniform(&sShaft, "uMotion");
        sShaftCol = DeckGl_Uniform(&sShaft, "uCol");
    }
    return true;
}

void DeckPost_Shutdown(void)
{
    DeckGl_DeleteProgram(&sBright);
    DeckGl_DeleteProgram(&sCopy);
    DeckGl_DeleteProgram(&sBlur);
    DeckGl_DeleteProgram(&sComposite);
    DeckGl_DeleteProgram(&sShaft);
    sShaftOk = false;
    if (sLutTex)
        glDeleteTextures(1, &sLutTex);
    sLutTex = 0;
    sLutOk = false;
    sLutDay = sLutGlow = -1.0f;
    sLutGradeLvl = sLutLvl = -2;
    DeleteTargets();
    DeckGl_TimerShutdown();
    sReady = false;
    sXOk = false;
    sTiltOk = false;
    sSunOk = false;
    DeckSunShadow_Shutdown();
}

// Scene width by voxel_res: OFF and LOW 960, MEDIUM 1120, HIGH 1280 (AUTO follows voxel_quality); height keeps 960:544, even.
static void WantedSize(int *w, int *h)
{
    static const int kWidth[3] = { 960, 1120, 1280 };
    const int lvl = DeckLevel(FAST_EXTRA_VOXEL_RES);

    *w = lvl < 0 ? DECK_BASE_W : kWidth[lvl];
    *h = (((*w * DECK_BASE_H + DECK_BASE_W - 1) / DECK_BASE_W) + 1) & ~1;
}

static bool CreateTargets(int w, int h)
{
    bool ok = DeckGl_CreateTarget(&sScene, w, h, DECK_GL_DEPTH_TEXTURE24, GL_NEAREST);

    if (!ok)
    {
        DeckGl_DeleteTarget(&sScene);
        ok = DeckGl_CreateTarget(&sScene, w, h, DECK_GL_DEPTH_RENDERBUFFER24, GL_NEAREST);
    }
    for (int i = 0; i < 4 && ok; i++)
        ok = DeckGl_CreateTarget(&sHalf[i], w / 2, h / 2, DECK_GL_DEPTH_NONE, GL_LINEAR);
    return ok;
}

static void DeleteTargets(void)
{
    DeckGl_DeleteTarget(&sScene);
    for (int i = 0; i < 4; i++)
        DeckGl_DeleteTarget(&sHalf[i]);
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
    sFailW = 0;
    WantedSize(&sceneW, &sceneH);
    ok = CreateTargets(sceneW, sceneH);
    if (!ok && sceneW != DECK_BASE_W)
    {
        DeckGl_Log("[POST] %dx%d target failed at init: %dx%d\n", sceneW, sceneH, DECK_BASE_W, DECK_BASE_H);
        sFailW = sceneW; // not tried again by ApplyWantedSize
        DeleteTargets();
        ok = CreateTargets(DECK_BASE_W, DECK_BASE_H);
    }
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
    sTiltOk = sXOk && sScene.depth == DECK_GL_DEPTH_TEXTURE24;
    sLutOk = sXOk && CreateLut();
    const DeckPostConfig c = DeckPost_ReadConfig();
    DeckGl_Log("[POST] ready: scene %dx%d RGBA8 depth=%s extended=%d tilt=%d shafts=%d lut=%d | config quality=%d shaders=%d grade=%d bloom=%d blur=%d tiltlvl=%d\n",
               sScene.w, sScene.h, sScene.depth == DECK_GL_DEPTH_TEXTURE24 ? "texture24" : "renderbuffer24",
               (int)sXOk, (int)sTiltOk, (int)sShaftOk, (int)sLutOk, c.quality, (int)c.shaders, (int)c.grade, (int)c.bloom, (int)c.blur, (int)c.tilt);
}

// A changed voxel_res: delete and create the scene and half-res targets at the new size (GL thread, between frames).
// A failed create goes back to the old size, remembers the width, and does not try it again.
static void ApplyWantedSize(void)
{
    int w, h;

    WantedSize(&w, &h);
    if (w == sScene.w || w == sFailW)
        return;
    const int oldW = sScene.w, oldH = sScene.h;
    DeleteTargets();
    if (CreateTargets(w, h))
    {
        DeckGl_Log("[POST] scene target %dx%d -> %dx%d (voxel_res)\n", oldW, oldH, sScene.w, sScene.h);
        sFailW = 0;
    }
    else
    {
        DeckGl_Log("[POST] scene target %dx%d failed: back to %dx%d\n", w, h, oldW, oldH);
        sFailW = w;
        DeleteTargets();
        if (!CreateTargets(oldW, oldH))
        {
            DeckPost_Shutdown();
            DeckGl_Log("[POST] fallback: legacy diorama FBO and Pass_DepthOfField in use\n");
            return;
        }
    }
    sTiltOk = sXOk && sScene.depth == DECK_GL_DEPTH_TEXTURE24;
}

int DeckPost_SceneWidth(void) { return sScene.w; }
int DeckPost_SceneHeight(void) { return sScene.h; }

bool DeckPost_WantScene(int renderW, int renderH, bool indoor, bool battle)
{
    if (!sReady)
        return false;
    if (renderW != DECK_BASE_W || renderH != DECK_BASE_H)
    {
        if (!sSizeWarned)
            DeckGl_Log("[POST] render size %dx%d is not the logical %dx%d: legacy path\n",
                       renderW, renderH, DECK_BASE_W, DECK_BASE_H);
        sSizeWarned = true;
        return false;
    }
    ApplyWantedSize();
    if (!sReady)
        return false;
    const DeckPostConfig c = DeckPost_ReadConfig();
    if (!c.shaders)
        return false;
    if (battle)
        return c.dimLvl >= 0; // the battle only gets the backdrop blur and dim
    return c.grade || c.bloom || (c.blur && !indoor) || (c.tilt && sTiltOk && !indoor) || (c.shafts && sShaftOk) || (c.lut && sLutOk) || (c.sun && sSunOk);
}

bool DeckPost_SunReady(void)
{
    return sReady && sSunOk && sScene.depth == DECK_GL_DEPTH_TEXTURE24;
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

// The step grows with the scene width so the blur keeps its size on the picture at 1120 and 1280.
static void Blur(const DeckGlTarget *t, const DeckGlTarget *tmp, int passes, float stepMul)
{
    const float k = stepMul * (float)sScene.w / (float)DECK_BASE_W;

    for (int i = 0; i < passes; i++)
    {
        BlurPass(tmp, t, k / (float)t->w, 0.0f);
        BlurPass(t, tmp, 0.0f, k / (float)t->h);
    }
}

static void GradeTints(float daylight, float glow, float shadow[3], float high[3], float w[3])
{
    const float dusk = Clamp01(glow * 4.0f) * Clamp01(daylight / 0.35f);
    const float night = Clamp01(1.0f - daylight / 0.35f);
    const float day = Clamp01(1.0f - dusk - night);
    const float sum = day + dusk + night; // >= 1 by construction
    w[0] = day / sum;
    w[1] = dusk / sum;
    w[2] = night / sum;
    for (int i = 0; i < 3; i++)
    {
        shadow[i] = (kShadowTint[0][i] * day + kShadowTint[1][i] * dusk + kShadowTint[2][i] * night) / sum;
        high[i] = (kHighTint[0][i] * day + kHighTint[1][i] * dusk + kHighTint[2][i] * night) / sum;
    }
}

static bool CreateLut(void)
{
    GLint prevTex = 0;

    DeckGl_ActiveTexture(GL_TEXTURE0 + 4);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    glGenTextures(1, &sLutTex);
    glBindTexture(GL_TEXTURE_2D, sLutTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 256, 16, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
    DeckGl_ActiveTexture(GL_TEXTURE0);
    return sLutTex != 0;
}

static float Smooth01(float x)
{
    x = Clamp01(x);
    return x * x * (3.0f - 2.0f * x);
}

// 16x16x16 strip, 16 slices of blue side by side (x = slice * 16 + red, y = green). Each texel is the procedural grade
// (tint by luminance, saturation, at the preset strength; none when the grade is off) plus a gentle S-curve.
static void BuildLut(float daylight, float glow, const DeckPostPreset *pr, bool gradeOn, int lutLvl)
{
    static unsigned char px[16 * 256 * 3];
    float shadow[3], high[3], w[3];
    GLint prevTex = 0;
    float amt;

    GradeTints(daylight, glow, shadow, high, w);
    amt = (kLutCurve[0] * w[0] + kLutCurve[1] * w[1] + kLutCurve[2] * w[2]) * kLutLevel[lutLvl];
    for (int b = 0; b < 16; b++)
    {
        for (int g = 0; g < 16; g++)
        {
            for (int r = 0; r < 16; r++)
            {
                const float c[3] = { (float)r / 15.0f, (float)g / 15.0f, (float)b / 15.0f };
                const float l = 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
                const float t = Smooth01((l - 0.15f) / 0.60f);
                unsigned char *const o = &px[(g * 256 + b * 16 + r) * 3];

                for (int k = 0; k < 3; k++)
                {
                    const float tint = shadow[k] + (high[k] - shadow[k]) * t;
                    const float graded = l + (c[k] * tint - l) * pr->saturation;
                    float v = gradeOn ? c[k] + (graded - c[k]) * pr->grade : c[k];

                    v = Clamp01(v);
                    v += amt * (Smooth01(v) - v);
                    o[k] = (unsigned char)(Clamp01(v) * 255.0f + 0.5f);
                }
            }
        }
    }
    DeckGl_ActiveTexture(GL_TEXTURE0 + 4);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    glBindTexture(GL_TEXTURE_2D, sLutTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 16, GL_RGB, GL_UNSIGNED_BYTE, px);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
    DeckGl_ActiveTexture(GL_TEXTURE0);
}

void DeckPost_Run(const DeckPostFrame *in)
{
    DeckPostConfig cfg = DeckPost_ReadConfig();
    const int backLvl = in->battle ? cfg.dimLvl : -1;
    const bool doBack = backLvl >= 0;
    if (in->battle)
    {
        // a battle gets the backdrop blur and dim only; the scene stays crisp and ungraded
        cfg.grade = cfg.bloom = cfg.blur = cfg.tilt = cfg.shafts = cfg.lut = cfg.sun = false;
        cfg.gradeLvl = cfg.bloomLvl = cfg.blurLvl = cfg.tiltLvl = cfg.shaftLvl = cfg.lutLvl = cfg.sunLvl = -1;
    }
    const DeckPostPreset *const pr = &kPresets[cfg.gradeLvl >= 0 ? cfg.gradeLvl : cfg.quality];      // grade, saturation, vignette
    const DeckPostPreset *const pb = &kPresets[cfg.bloomLvl >= 0 ? cfg.bloomLvl : cfg.quality];      // bloom threshold and blur passes
    const float bloomGain = cfg.bloomLvl >= 0 ? kBloomGain[cfg.bloomLvl] : 0.0f;
    const float blurMul = cfg.blurLvl >= 0 ? kBlurMul[cfg.blurLvl] : 0.0f;
    const float keep = 1.0f - Clamp01(in->fade);
    const bool doBloom = cfg.bloom && in->bloom > 0.0f && bloomGain > 0.0f;
    const bool doTilt = cfg.tilt && sTiltOk && !in->indoor && in->focusDist > 0.0f; // replaces the band blur
    const bool doLut = cfg.lut && sLutOk;
    const float shaftAmt = in->sunAmount + in->moonAmount;
    const bool doShafts = cfg.shafts && sShaftOk && shaftAmt > 0.01f;
    const bool doBlur = cfg.blur && !in->indoor && !doTilt;
    const DeckSunResolve *const sun = (cfg.sun && sSunOk && in->sun != NULL && in->sun->active) ? in->sun : NULL;
    float shadow[3], high[3];

    if (doLut)
    {
        // Rebuilt only when the time of day, the grade level or the LUT level moved (4096 texels, microseconds).
        if (sLutDay < 0.0f || fabsf(in->daylight - sLutDay) > 0.004f || fabsf(in->glow - sLutGlow) > 0.004f
            || cfg.gradeLvl != sLutGradeLvl || cfg.lutLvl != sLutLvl)
        {
            BuildLut(in->daylight, in->glow, pr, cfg.grade, cfg.lutLvl);
            sLutDay = in->daylight;
            sLutGlow = in->glow;
            sLutGradeLvl = cfg.gradeLvl;
            sLutLvl = cfg.lutLvl;
        }
    }
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
        DeckGl_Set1f(sBrightThreshold, pb->threshold);
        DrawQuad();
        Blur(&sHalf[0], &sHalf[1], pb->bloomPasses, 1.0f);
    }
    if (doShafts)
    {
        const float moonK = in->moonAmount / shaftAmt;
        const float goldK = Clamp01(in->glow * 4.0f) * (1.0f - moonK);
        float col[3];

        for (int k = 0; k < 3; k++)
        {
            const float sun = kShaftDay[k] + (kShaftGold[k] - kShaftDay[k]) * goldK;

            col[k] = sun + (kShaftMoon[k] - sun) * moonK;
        }
        BeginTarget(&sHalf[3]);
        DeckGl_UseProgram(&sShaft);
        DeckGl_Set2f(sShaftSun, in->beamX, in->beamY);
        DeckGl_Set1f(sShaftLen, Clamp01((in->beamElev - 0.45f) / 0.5f) * 0.5f + 0.85f); // elevation 0.59 (dawn) -> 0.92, 0.88 (noon) -> 1.34
        DeckGl_Set1f(sShaftTime, in->time);
        DeckGl_Set1f(sShaftAsp, (float)in->renderW / (float)in->renderH);
        DeckGl_Set1f(sShaftCount, kShaftLayers[cfg.shaftLvl]);
        DeckGl_Set1f(sShaftMotion, 1.0f - 0.55f * moonK);
        DeckGl_Set3f(sShaftCol, col[0], col[1], col[2]);
        DrawQuad();
    }
    if (doBlur || doTilt || doBack)
    {
        BindTex(0, sScene.color);
        BeginTarget(&sHalf[2]);
        DeckGl_UseProgram(&sCopy);
        DeckGl_Set1i(sCopyScene, 0);
        DrawQuad();
        if (doBlur)
            Blur(&sHalf[2], &sHalf[1], 1, 1.0f);
        else if (doBack)
            Blur(&sHalf[2], &sHalf[1], kBackPasses[backLvl], kBackStep[backLvl]); // the tilt-shift gathers from the plain half-res copy
    }

    // Composite into the letterbox: same viewport call as the legacy present (DeckGlViewport maps
    // (0,0,960,544) on framebuffer 0 to the letterboxed rect and enables the scissor).
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    DeckGlViewport(0, 0, in->renderW, in->renderH);
    BindTex(5, doShafts ? sHalf[3].color : 0);
    BindTex(4, doLut ? sLutTex : 0);
    BindTex(6, sun != NULL ? sun->depthTex : 0);
    BindTex(3, (doTilt || sun != NULL) ? sScene.depthTex : 0);
    BindTex(2, (doBlur || doTilt || doBack) ? sHalf[2].color : 0);
    BindTex(1, doBloom ? sHalf[0].color : 0);
    BindTex(0, sScene.color);
    // 960: one texel per logical pixel, NEAREST keeps the pixel art sharp. 1120 and 1280 are not integer multiples
    // of the letterbox rect, NEAREST would shimmer on diagonal edges: LINEAR (the target is near 1:1 on the Deck).
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, sScene.w == DECK_BASE_W ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, sScene.w == DECK_BASE_W ? GL_NEAREST : GL_LINEAR);
    float wgt[3];
    GradeTints(in->daylight, in->glow, shadow, high, wgt);
    DeckGl_UseProgram(&sComposite);
    DeckGl_Set1i(sCScene, 0);
    DeckGl_Set1i(sCBloom, 1);
    DeckGl_Set1i(sCBlur, 2);
    DeckGl_Set1i(sCDepth, 3);
    DeckGl_Set1i(sCShafts, 5);
    DeckGl_Set1i(sCLut, 4);
    DeckGl_Set2f(sCLutOn, doLut ? 1.0f : 0.0f, keep);
    DeckGl_Set1i(sCSunMap, 6);
    if (sun != NULL)
    {
        DeckGl_SetMat4(sCInvVP, sun->invVP);
        DeckGl_SetMat4(sCSunVP, sun->sunVP);
        DeckGl_Set4f(sCSunA, sun->amount, sun->texelUv, (float)sun->radius, sun->mul);
        DeckGl_Set4f(sCSunB, sun->range, sun->texelWorld, sun->spacing, 0.0f);
        DeckGl_Set3f(sCSunL, sun->toSun[0], sun->toSun[1], sun->toSun[2]);
    }
    else
        DeckGl_Set4f(sCSunA, 0.0f, 0.0f, 0.0f, 1.0f);
    {
        static float sGainDbg = -1.0f; // debug aid: env DECK_SHAFT_GAIN=<multiplier> (to see the beam angles in a screenshot)

        if (sGainDbg < 0.0f)
        {
            const char *e = getenv("DECK_SHAFT_GAIN");

            sGainDbg = e != NULL ? (float)atof(e) : 1.0f;
        }
        DeckGl_Set4f(sCShaft, doShafts ? shaftAmt * kShaftGain[cfg.shaftLvl] * keep * sGainDbg : 0.0f, 0.0f, 0.0f, 0.0f);
    }
    {
        const int tl = doTilt ? cfg.tiltLvl : 0;
        DeckGl_Set4f(sCTilt, doTilt ? 1.0f : 0.0f, in->focusDist, kTiltRadius[tl], kTiltTaps[tl]);
        DeckGl_Set2f(sCZ, in->zNear, in->zFar);
    }
    DeckGl_Set1f(sCBloomAmt, doBloom ? in->bloom * bloomGain * keep : 0.0f);
    DeckGl_Set1f(sCBlurOn, doBlur ? 1.0f : 0.0f);
    DeckGl_Set2f(sCBack, doBack ? keep : 0.0f, doBack ? kBackDim[backLvl] * keep : 0.0f);
    // Legacy band height: VOXEL_DOF_*_ROWS * (renderH / 544) / renderH = rows / 544.
    DeckGl_Set4f(sCDof, in->dofTopRows / 544.0f, in->dofBottomRows / 544.0f, in->dofTopAlpha * blurMul, in->dofBottomAlpha * blurMul);
    DeckGl_Set1f(sCGradeAmt, cfg.grade ? pr->grade * keep : 0.0f);
    DeckGl_Set3f(sCShadow, shadow[0], shadow[1], shadow[2]);
    DeckGl_Set3f(sCHigh, high[0], high[1], high[2]);
    DeckGl_Set1f(sCSat, pr->saturation);
    DeckGl_Set1f(sCVignette, cfg.grade ? pr->vignette * keep : 0.0f);
    DeckGl_Set1f(sCAspect, (float)in->renderW / (float)in->renderH);
    DrawQuad();

    // Leave the state Pass_DepthOfField leaves (see deck_post.h).
    DeckGl_UseProgram(NULL);
    BindTex(6, 0);
    BindTex(5, 0);
    BindTex(4, 0);
    BindTex(3, 0);
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
