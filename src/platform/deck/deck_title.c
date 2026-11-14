#include <SDL2/SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "deck_title.h"
#include "deck_gl_shader.h"
#include "deck_levels.h"
#include "global.h"
#include "palette.h"
#include "deck_reveal.h"
#include "deck_title_font.h"
#include "platform/vita_extras.h"

// Written by src/title_screen.c: 0 off the title, 1 logo shine, 2 version banner, 3 PRESS START, 4 leaving.
extern u8 gDeckTitlePhase;

// ====================================================================================================
// "Pokemon Bravely Emerald 2D-HD" title. The original screen stays the composition: the sky gradient, the Rayquaza
// silhouette and the clouds (BG0 / BG1, read from emulated VRAM with the game's own tile data and palette), and in front of
// them the logo, EMERALD VERSION banner, PRESS START and copyright lines, which the game's own renderer draws pixel for pixel
// (BG2 + sprites, backdrop keyed out with magenta in sdl2.c VDraw, so fades, blinking and the shine are the original ones).
// The 2D-HD part is the lighting and depth: a smooth sky, Rayquaza as its own sprite (crisp texels, sub-pixel drift, rim
// light, pulsing yellow markings), two cloud layers with parallax and blur, light rays, motes, a rim light and a moving
// shine on the logo, a soft glow around it, and a small "BRAVELY 2D-HD" badge lettered with the game's own font.
// Layer order, back to front: sky, far clouds, Rayquaza, near clouds, rays, motes, palette fade, badge, front layer glow,
// front layer. All in the logical 960x544 picture (y down); the 240x160 art sits at x 72..888, 3.4 px per texel.
// ====================================================================================================

#define SW 960.0f
#define SH 544.0f
#define ART_X0 72.0f
#define ART_W 816.0f
#define TEX_SCALE 3.4f
#define ART_W_TEXELS 240
#define ART_H_TEXELS 160

static float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static float Smooth(float a, float b, float x)
{
    const float t = Clamp01((x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}

// ------------------------------------------------------------------------------------ shaders
static const char kVs[] =
    "#version 120\n"
    "varying vec3 vT;\n"
    "varying vec4 vC;\n"
    "void main() {\n"
    "    vT = gl_MultiTexCoord0.xyz;\n"
    "    vC = gl_Color;\n"
    "    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
    "}\n";

// plain premultiplied texture with mip bias (sky column, clouds, badge)
static const char kLayerFs[] =
    "#version 120\n"
    "uniform sampler2D uTex;\n"
    "uniform float uBias;\n"
    "varying vec3 vT;\n"
    "varying vec4 vC;\n"
    "void main() {\n"
    "    vec4 t = texture2D(uTex, vT.xy, uBias);\n"
    "    t.rgb *= vC.rgb;\n"
    "    gl_FragColor = t * vC.a;\n"
    "}\n";

// Rayquaza: crisp texels with sub-pixel motion ("sharp bilinear": the edge between texels is one output pixel wide), the
// yellow marking (L8 mask) takes the live palette colour, a warm rim on the sides facing the light (upper right), a slight
// body gradient.
static const char kRayFs[] =
    "#version 120\n"
    "uniform sampler2D uTex;\n"
    "uniform sampler2D uMark;\n"
    "uniform vec2 uSize;\n"
    "uniform float uScale;\n"
    "uniform vec3 uMarkCol;\n"
    "uniform float uRim;\n"
    "varying vec3 vT;\n"
    "varying vec4 vC;\n"
    "vec2 sharp(vec2 uv) {\n"
    "    vec2 p = uv * uSize;\n"
    "    vec2 f = fract(p);\n"
    "    p = floor(p) + clamp((f - 0.5) * uScale + 0.5, 0.0, 1.0);\n"
    "    return p / uSize;\n"
    "}\n"
    "void main() {\n"
    "    vec2 uv = sharp(vT.xy);\n"
    "    vec4 t = texture2D(uTex, uv);\n"
    "    float m = texture2D(uMark, uv).r;\n"
    "    vec3 col = t.rgb * mix(1.18, 0.86, vT.y) + m * uMarkCol;\n"
    "    float a = t.a;\n"
    "    vec2 px = 1.0 / uSize;\n"
    "    float up = texture2D(uTex, uv + vec2(px.x, -px.y)).a;\n"
    "    float up2 = texture2D(uTex, uv + vec2(0.0, -px.y)).a;\n"
    "    float rim = a * (1.0 - min(up, up2));\n"
    "    col += rim * vec3(1.0, 0.80, 0.42) * uRim;\n"
    "    gl_FragColor = vec4(col, 1.0) * (a * vC.a);\n"
    "}\n";

// front layer: the game's own picture, the keyed backdrop (magenta) is transparent. uMode 0: glow (a soft halo of the lit
// texels added behind), 1: the picture itself with a rim highlight on the upper left edges.
static const char kFrontFs[] =
    "#version 120\n"
    "uniform sampler2D uTex;\n"
    "uniform vec2 uTexel;\n"       // 1 / texture size
    "uniform vec2 uRad;\n"         // glow radius in uv
    "uniform float uMode;\n"
    "uniform float uAmt;\n"
    "uniform vec4 uBanner;\n"     // uv rect of the EMERALD VERSION sprites
    "uniform float uBMode;\n"     // 0 no banner logic, 1 draw all but the banner, 2 draw only the banner
    "varying vec3 vT;\n"
    "varying vec4 vC;\n"
    "bool isBanner(vec2 uv, vec3 c) {\n"
    "    if (uBMode < 0.5 || uv.x < uBanner.x || uv.x > uBanner.z || uv.y < uBanner.y || uv.y > uBanner.w) return false;\n"
    "    float mx = max(c.r, max(c.g, c.b));\n"
    "    float mn = min(c.r, min(c.g, c.b));\n"
    "    return (mx - mn) < 0.2 * mx + 0.02;\n"      // white, grey and the black outline; the logo is yellow and blue
    "}\n"
    "float opaque(vec2 uv) { vec3 c = texture2D(uTex, uv).rgb; return step(0.04, length(c - vec3(1.0, 0.0, 1.0))); }\n"
    "void main() {\n"
    "    vec2 uv = vT.xy;\n"
    "    if (uMode < 0.5) {\n"
    "        vec3 acc = vec3(0.0);\n"
    "        for (int i = 0; i < 12; i++) {\n"
    "            float a = float(i) * 0.5236;\n"
    "            float r = (mod(float(i), 2.0) < 0.5) ? 0.55 : 1.0;\n"
    "            vec2 o = vec2(cos(a), sin(a)) * uRad * r;\n"
    "            vec3 c = texture2D(uTex, uv + o).rgb;\n"
    "            acc += c * step(0.04, length(c - vec3(1.0, 0.0, 1.0))) * (isBanner(uv + o, c) ? 0.0 : 1.0);\n"
    "        }\n"
    "        gl_FragColor = vec4(acc / 12.0 * uAmt, 1.0);\n"
    "        return;\n"
    "    }\n"
    "    vec3 c = texture2D(uTex, uv).rgb;\n"
    "    if (length(c - vec3(1.0, 0.0, 1.0)) < 0.04) discard;\n"
    "    bool bn = isBanner(uv, c);\n"
    "    if ((uBMode > 1.5 && !bn) || (uBMode > 0.5 && uBMode < 1.5 && bn)) discard;\n"
    "    float edge = 1.0 - opaque(uv + vec2(-uTexel.x, -uTexel.y));\n"
    "    float edge2 = 1.0 - opaque(uv + vec2(0.0, -uTexel.y));\n"
    "    c += vec3(1.0, 0.96, 0.8) * max(edge, edge2) * 0.28;\n"
    "    gl_FragColor = vec4(c, 1.0);\n"
    "}\n";

// the shine as its own pass (the picture is the mask): a slanted bright band, stronger on bright texels
static const char kShineFs[] =
    "#version 120\n"
    "uniform sampler2D uTex;\n"
    "uniform float uPos;\n"
    "uniform float uAmt;\n"
    "varying vec3 vT;\n"
    "varying vec4 vC;\n"
    "void main() {\n"
    "    vec3 c = texture2D(uTex, vT.xy).rgb;\n"
    "    if (length(c - vec3(1.0, 0.0, 1.0)) < 0.04) discard;\n"
    "    float l = dot(c, vec3(0.299, 0.587, 0.114));\n"
    "    float s = vT.z + 0.30 * vT.y;\n"
    "    float b = 1.0 - smoothstep(0.0, 0.06, abs(s - uPos));\n"
    "    gl_FragColor = vec4(vec3(1.0, 0.96, 0.78) * b * (0.30 + 0.70 * l) * uAmt, 1.0);\n"
    "}\n";

static const char kSoftFs[] =
    "#version 120\n"
    "varying vec3 vT;\n"
    "varying vec4 vC;\n"
    "void main() {\n"
    "    float r = length(vT.xy);\n"
    "    float a = (1.0 - smoothstep(mix(0.0, 0.72, vT.z), 1.0, r)) * vC.a;\n"
    "    gl_FragColor = vec4(vC.rgb * a, a);\n"
    "}\n";

// light rays from the upper right, slanted, drifting and breathing; added over the whole picture (stronger at the top)
static const char kRaysFs[] =
    "#version 120\n"
    "uniform float uT;\n"
    "uniform float uAmt;\n"
    "varying vec3 vT;\n"
    "varying vec4 vC;\n"
    "float h21(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }\n"
    "float vn(vec2 p) {\n"
    "    vec2 i = floor(p); vec2 f = fract(p); f = f * f * (3.0 - 2.0 * f);\n"
    "    return mix(mix(h21(i), h21(i + vec2(1.0, 0.0)), f.x), mix(h21(i + vec2(0.0, 1.0)), h21(i + vec2(1.0, 1.0)), f.x), f.y);\n"
    "}\n"
    "void main() {\n"
    "    vec2 c = vec2(vT.x, 1.0 - vT.y);\n"
    "    vec2 d = (c - vec2(1.05, 1.12)) * vec2(1.7647, 1.0);\n"
    "    float r = length(d);\n"
    "    float ang = atan(d.y, d.x);\n"
    "    float a1 = vn(vec2(ang * 7.0 + uT * 0.040, 3.0));\n"
    "    float a2 = vn(vec2(ang * 17.0 - uT * 0.032, 9.0));\n"
    "    float a3 = vn(vec2(ang * 37.0 + uT * 0.055, 21.0));\n"
    "    float ray = pow(a1 * 0.5 + a2 * 0.32 + a3 * 0.18, 3.0);\n"
    "    float breath = 0.60 + 0.40 * sin(uT * 0.50 + ang * 3.0 + a1 * 6.0);\n"
    "    float v = ray * breath * exp(-r * 1.3) * smoothstep(0.05, 0.35, r);\n"
    "    gl_FragColor = vec4(vec3(1.0, 0.93, 0.70) * v * uAmt, 1.0);\n"
    "}\n";

// ---------------------------------------------------------------------------------------- state
static bool sInit, sFailed, sEnvRead, sEnvOrig, sArtOk;
static DeckGlProgram sLayer, sRay, sFront, sShine, sSoft, sRays;
static GLint sLyTex, sLyBias, sRyTex, sRyMark, sRySize, sRyScale, sRyMarkCol, sRyRim;
static GLint sFrBanner, sFrBMode, sFrTex, sFrTexel, sFrRad, sFrMode, sFrAmt, sShTex, sShPos, sShAmt, sRaysT, sRaysAmt;
static GLuint sSkyTex, sRayTex, sMarkTex, sCloudTex, sPlateTex, sCrestTex;
static int sPlateW, sPlateH, sCrestW, sCrestH;
static int sPrevPhase;
static double sStart, sPhase3At = -1.0;
static unsigned sFrames;
static bool sActive;   // this frame is drawn by the title scene (set by DeckTitle_Wanted)

static double Now(void) { return (double)SDL_GetTicks() / 1000.0; }

static GLuint MakeTexture(const uint8_t *data, int w, int h, GLenum fmt, GLenum minF, GLenum magF, GLenum wrap, bool mips)
{
    GLuint t = 0;

    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minF);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magF);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, fmt == GL_LUMINANCE ? GL_LUMINANCE8 : GL_RGBA8, w, h, 0, fmt, GL_UNSIGNED_BYTE, data);
    if (mips)
        glGenerateMipmap(GL_TEXTURE_2D);
    return t;
}

static void Rgb555(uint16_t c, uint8_t *o)
{
    o[0] = (uint8_t)((c & 31) * 255 / 31);
    o[1] = (uint8_t)(((c >> 5) & 31) * 255 / 31);
    o[2] = (uint8_t)(((c >> 10) & 31) * 255 / 31);
}

// ------------------------------------------------------------------ the original layers from VRAM
// BG0 (Rayquaza and the sky): 4bpp tiles at charbase 2, 32x32 map at screenbase 26. Palette bank 14 holds the sky bands
// (indexes 3..10), the silhouette (11), the white eyes (2) and the animated yellow markings (15).
// BG1 (clouds): charbase 3, screenbase 27. Palette entries come from gPlttBufferUnfaded (the fade is the game's, applied later).
static uint8_t ReadIdx(const uint8_t *chars, uint16_t e, int x, int y)
{
    const int tile = e & 0x3FF, hf = (e >> 10) & 1, vf = (e >> 11) & 1;
    const int sx = hf ? 7 - x : x, sy = vf ? 7 - y : y;
    const uint8_t b = chars[tile * 32 + sy * 4 + sx / 2];

    return (sx & 1) ? (b >> 4) : (b & 15);
}

static bool GrabArt(void)
{
    const uint8_t *chars0 = (const uint8_t *)(uintptr_t)BG_CHAR_ADDR(2);
    const uint16_t *map0 = (const uint16_t *)(uintptr_t)BG_SCREEN_ADDR(26);
    const uint8_t *chars1 = (const uint8_t *)(uintptr_t)BG_CHAR_ADDR(3);
    const uint16_t *map1 = (const uint16_t *)(uintptr_t)BG_SCREEN_ADDR(27);
    uint8_t *ray = (uint8_t *)calloc((size_t)ART_W_TEXELS * ART_H_TEXELS * 4, 1);
    uint8_t *mark = (uint8_t *)calloc((size_t)ART_W_TEXELS * ART_H_TEXELS, 1);
    uint8_t *clouds = (uint8_t *)calloc(256 * 256 * 4, 1);
    uint8_t sky[ART_H_TEXELS][3];
    uint8_t skyCol[ART_H_TEXELS * 4];
    int nRay = 0, nSky = 0, nCloud = 0;

    if (!ray || !mark || !clouds)
        goto fail;
    // Rayquaza texels, and the sky colour of each row
    for (int y = 0; y < ART_H_TEXELS; y++)
    {
        bool have = false;

        for (int x = 0; x < ART_W_TEXELS; x++)
        {
            const uint16_t e = map0[(y / 8) * 32 + x / 8];
            const int bank = e >> 12;
            const int idx = ReadIdx(chars0, e, x & 7, y & 7);
            uint8_t col[3];

            if (idx == 0)
                continue;
            Rgb555(gPlttBufferUnfaded[bank * 16 + idx], col);
            if (bank == 14 && (idx == 11 || idx == 15 || idx == 2))
            {
                uint8_t *o = &ray[((size_t)y * ART_W_TEXELS + x) * 4];

                nRay++;
                if (idx == 15)
                    mark[y * ART_W_TEXELS + x] = 255;   // the colour comes from the live palette in the shader
                else
                {
                    o[0] = col[0];
                    o[1] = col[1];
                    o[2] = col[2];
                }
                o[3] = 255;
            }
            else if (!have)
            {
                memcpy(sky[y], col, 3);
                have = true;
                nSky++;
            }
        }
        if (!have)
        {
            if (y > 0)
                memcpy(sky[y], sky[y - 1], 3);
            else
            {
                sky[0][0] = 0;
                sky[0][1] = 74;
                sky[0][2] = 164;
            }
        }
    }
    if (nRay < 200 || nSky < 80)
        goto fail;
    // the sky: one column of row colours, smoothed vertically (the original has hard bands; the HD one is a gradient)
    for (int y = 0; y < ART_H_TEXELS; y++)
    {
        float acc[3] = { 0, 0, 0 };
        int n = 0;

        for (int k = -9; k <= 9; k++)
        {
            const int yy = y + k < 0 ? 0 : (y + k >= ART_H_TEXELS ? ART_H_TEXELS - 1 : y + k);

            for (int c = 0; c < 3; c++)
                acc[c] += sky[yy][c];
            n++;
        }
        for (int c = 0; c < 3; c++)
            skyCol[y * 4 + c] = (uint8_t)(acc[c] / (float)n);
        skyCol[y * 4 + 3] = 255;
    }
    // clouds: RGBA 256x256 (opaque colours, alpha 1), index 0 clear
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++)
        {
            const uint16_t e = map1[(y / 8) * 32 + x / 8];
            const int bank = e >> 12;
            const int idx = ReadIdx(chars1, e, x & 7, y & 7);
            uint8_t *o = &clouds[((size_t)y * 256 + x) * 4];

            if (idx == 0)
                continue;
            Rgb555(gPlttBufferUnfaded[bank * 16 + idx], o);
            o[3] = 255;
            nCloud++;
        }
    if (sSkyTex) glDeleteTextures(1, &sSkyTex);
    if (sRayTex) glDeleteTextures(1, &sRayTex);
    if (sMarkTex) glDeleteTextures(1, &sMarkTex);
    if (sCloudTex) glDeleteTextures(1, &sCloudTex);
    sSkyTex = MakeTexture(skyCol, 1, ART_H_TEXELS, GL_RGBA, GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, false);
    sRayTex = MakeTexture(ray, ART_W_TEXELS, ART_H_TEXELS, GL_RGBA, GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, false);
    sMarkTex = MakeTexture(mark, ART_W_TEXELS, ART_H_TEXELS, GL_LUMINANCE, GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, false);
    sCloudTex = MakeTexture(clouds, 256, 256, GL_RGBA, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, GL_REPEAT, true);
    DeckGl_Log("[TITLE] art from VRAM: Rayquaza %d texels, sky rows %d, cloud texels %d\n", nRay, nSky, nCloud);
    free(ray);
    free(mark);
    free(clouds);
    return sSkyTex && sRayTex && sMarkTex && sCloudTex;
fail:
    free(ray);
    free(mark);
    free(clouds);
    return false;
}

// ------------------------------------------------------------------------ serif plate and crest
// The two words come from deck_title_font.h (Cinzel masks rendered once offline by tools/deck/gen_title_font.py).
static unsigned char *DecodeRle(const unsigned char *rle, size_t n, int w, int h)
{
    unsigned char *m = (unsigned char *)malloc((size_t)w * (size_t)h);
    size_t o = 0;

    if (m == NULL)
        return NULL;
    for (size_t i = 0; i + 1 < n && o < (size_t)w * (size_t)h; i += 2)
        for (int k = 0; k < rle[i + 1] && o < (size_t)w * (size_t)h; k++)
            m[o++] = rle[i];
    while (o < (size_t)w * (size_t)h)
        m[o++] = 0;
    return m;
}

// bilinear sample of a mask, coordinates in mask texels
static float MaskAt(const unsigned char *m, int w, int h, float x, float y)
{
    const int x0 = (int)floorf(x), y0 = (int)floorf(y);
    const float fx = x - (float)x0, fy = y - (float)y0;
    float v[4];

    for (int k = 0; k < 4; k++)
    {
        const int xx = x0 + (k & 1), yy = y0 + (k >> 1);

        v[k] = (xx < 0 || yy < 0 || xx >= w || yy >= h) ? 0.0f : (float)m[yy * w + xx] / 255.0f;
    }
    return (v[0] * (1.0f - fx) + v[1] * fx) * (1.0f - fy) + (v[2] * (1.0f - fx) + v[3] * fx) * fy;
}

static void OverPx(float *px, float r, float g, float b, float a)   // premultiplied float RGBA
{
    const float ia = 1.0f - a;

    px[0] = r * a + px[0] * ia;
    px[1] = g * a + px[1] * ia;
    px[2] = b * a + px[2] * ia;
    px[3] = a + px[3] * ia;
}

static float SdRoundRect(float px, float py, float hx, float hy, float r)
{
    const float qx = fabsf(px) - hx + r, qy = fabsf(py) - hy + r;
    const float ox = qx > 0.0f ? qx : 0.0f, oy = qy > 0.0f ? qy : 0.0f;

    return sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0.0f) - r;
}

static GLuint FloatToTexture(const float *f, int w, int h)
{
    uint8_t *b = (uint8_t *)malloc((size_t)w * h * 4);
    GLuint t;

    if (b == NULL)
        return 0;
    for (int i = 0; i < w * h * 4; i++)
        b[i] = (uint8_t)(Clamp01(f[i]) * 255.0f + 0.5f);
    t = MakeTexture(b, w, h, GL_RGBA, GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, false);
    free(b);
    return t;
}

// BRAVELY: dark ink in Trajan-like capitals on a soft light translucent plate (pale grey-white, slight gradient, rim, shadow).
static bool BuildPlate(void)
{
    const int w = 560, h = 168;
    unsigned char *m = DecodeRle(kBravelyRle, sizeof(kBravelyRle), KBRAVELYRLE_W, KBRAVELYRLE_H);
    float *f = (float *)calloc((size_t)w * h * 4, sizeof(float));
    const float tw = 430.0f, sc = tw / (float)KBRAVELYRLE_W, th = (float)KBRAVELYRLE_H * sc;

    if (m == NULL || f == NULL)
    {
        free(m);
        free(f);
        return false;
    }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
        {
            float *px = &f[((size_t)y * w + x) * 4];
            const float d = SdRoundRect((float)x + 0.5f - w * 0.5f, (float)y + 0.5f - h * 0.5f, w * 0.5f - 14.0f, h * 0.5f - 14.0f, 14.0f);
            const float v = (float)y / (float)h;

            if (d > 0.0f && d < 12.0f)
            {
                const float a = 0.30f * expf(-d / 4.5f);
                OverPx(px, 0.02f, 0.05f, 0.08f, a);   // soft shadow outside
            }
            if (d < 1.0f)
            {
                const float cov = Clamp01(0.5f - d);
                const float a = (0.90f - 0.14f * v) * cov;
                const float lum = 0.985f - 0.17f * v;

                OverPx(px, lum, lum + 0.01f, lum + 0.03f, a);
                if (d > -3.0f)
                    OverPx(px, 1.0f, 1.0f, 1.0f, 0.55f * cov * Clamp01((d + 3.0f) / 3.0f));   // bright rim
                if (d < -3.0f && d > -5.0f)
                    OverPx(px, 0.55f, 0.58f, 0.66f, 0.18f * cov);                        // inner fine line
            }
        }
    // text: embossed (white above-left under the ink, soft shadow below-right), ink on top
    for (int pass = 0; pass < 3; pass++)
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
            {
                const float ox = (float)w * 0.5f - tw * 0.5f, oy = (float)h * 0.5f - th * 0.5f;
                const float off = pass == 0 ? -1.2f : (pass == 1 ? 1.4f : 0.0f);
                const float a = MaskAt(m, KBRAVELYRLE_W, KBRAVELYRLE_H, ((float)x - ox - off) / sc, ((float)y - oy - off) / sc);
                float *px = &f[((size_t)y * w + x) * 4];

                if (a <= 0.0f)
                    continue;
                if (pass == 0)
                    OverPx(px, 1.0f, 1.0f, 1.0f, 0.75f * a);
                else if (pass == 1)
                    OverPx(px, 0.35f, 0.38f, 0.48f, 0.35f * a);
                else
                    OverPx(px, 0.08f, 0.09f, 0.15f, 0.96f * a);
            }
    sPlateTex = FloatToTexture(f, w, h);
    sPlateW = w;
    sPlateH = h;
    free(m);
    free(f);
    return sPlateTex != 0;
}

static float H2(int a, int b, int c)
{
    unsigned n = (unsigned)(a * 73856093) ^ (unsigned)(b * 19349663) ^ (unsigned)(c * 83492791);

    n = (n << 13) ^ n;
    return (float)((n * (n * n * 15731u + 789221u) + 1376312589u) & 0x7fffffffu) / 2147483648.0f;
}

// HD-2D crest: a rounded shield, white with pale blue triangular facets like a cut crystal, a white rim, "HD-2D" in dark serif caps.
static bool BuildCrest(void)
{
    const int w = 240, h = 280;
    unsigned char *m = DecodeRle(kHd2dRle, sizeof(kHd2dRle), KHD2DRLE_W, KHD2DRLE_H);
    float *f = (float *)calloc((size_t)w * h * 4, sizeof(float));
    unsigned char *in = (unsigned char *)calloc((size_t)w * h, 1);
    const float tw = 150.0f, sc = tw / (float)KHD2DRLE_W, th = (float)KHD2DRLE_H * sc;

    if (m == NULL || f == NULL || in == NULL)
    {
        free(m);
        free(f);
        free(in);
        return false;
    }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
        {
            const float nx = ((float)x + 0.5f - w * 0.5f) / 108.0f, ny = ((float)y + 0.5f - h * 0.5f) / 128.0f;
            const float ax = fabsf(nx);
            const float top = -0.93f + 0.13f * nx * nx + (ax < 0.12f ? 0.05f : 0.0f);
            const float bot = 0.12f + 0.88f * (1.0f - powf(fminf(ax, 1.0f), 1.7f));

            in[y * w + x] = (ax <= 1.0f && ny >= top && ny <= bot) ? 1 : 0;
        }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
        {
            float *px = &f[((size_t)y * w + x) * 4];
            int near = 99;

            if (!in[y * w + x])
            {
                // soft grey-blue shadow just outside the rim
                for (int dy = -3; dy <= 3 && near > 3; dy++)
                    for (int dx = -3; dx <= 3; dx++)
                        if (x + dx >= 0 && x + dx < w && y + dy >= 0 && y + dy < h && in[(y + dy) * w + x + dx])
                        {
                            near = 3;
                            break;
                        }
                if (near == 3)
                    OverPx(px, 0.25f, 0.35f, 0.55f, 0.35f);
                continue;
            }
            for (int r = 1; r <= 7 && near == 99; r++)
                for (int dy = -r; dy <= r && near == 99; dy++)
                    for (int dx = -r; dx <= r; dx++)
                    {
                        const int xx = x + dx, yy = y + dy;

                        if (xx < 0 || xx >= w || yy < 0 || yy >= h || !in[yy * w + xx])
                        {
                            near = r;
                            break;
                        }
                    }
            {
                // triangular facets: a lattice of cells, each split into two triangles, a shade per triangle
                const float cs = 30.0f, gx = (float)x / cs, gy = (float)y / cs;
                const int cx = (int)floorf(gx), cy = (int)floorf(gy);
                const float fx = gx - (float)cx, fy = gy - (float)cy;
                const int tri = (fx + fy > 1.0f) ? 1 : 0;
                const float hv = H2(cx, cy, tri);
                const float edgeD = fminf(fminf(fx, fy), fminf(1.0f - fx, 1.0f - fy));
                const float diag = fabsf(fx + fy - 1.0f) * 0.7071f;
                const float lineD = fminf(edgeD, diag) * cs;
                float r, g, b;

                if (hv < 0.40f)
                {
                    const float k = hv / 0.40f;

                    r = 0.60f + 0.22f * k;
                    g = 0.80f + 0.12f * k;
                    b = 0.97f;
                }
                else
                {
                    const float k = (hv - 0.40f) / 0.60f;

                    r = 0.90f + 0.10f * k;
                    g = 0.94f + 0.06f * k;
                    b = 1.0f;
                }
                if (lineD < 1.1f)
                {
                    r *= 0.84f;
                    g *= 0.90f;
                    b *= 0.97f;
                }
                OverPx(px, r, g, b, 1.0f);
                if (near <= 5)
                    OverPx(px, 1.0f, 1.0f, 1.0f, 0.95f * (1.0f - (float)(near - 1) / 5.0f * 0.4f));   // white rim
            }
        }
    // "HD-2D": a pale halo, then dark ink
    for (int pass = 0; pass < 2; pass++)
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
            {
                const float ox = (float)w * 0.5f - tw * 0.5f, oy = (float)h * 0.52f - th * 0.5f;
                float a = 0.0f;
                float *px = &f[((size_t)y * w + x) * 4];

                if (pass == 0)
                {
                    for (int k = 0; k < 8; k++)
                    {
                        const float ang = (float)k * 0.7854f;

                        a = fmaxf(a, MaskAt(m, KHD2DRLE_W, KHD2DRLE_H, ((float)x - ox + 2.6f * cosf(ang)) / sc, ((float)y - oy + 2.6f * sinf(ang)) / sc));
                    }
                    if (a > 0.0f)
                        OverPx(px, 1.0f, 1.0f, 1.0f, 0.85f * a);
                }
                else
                {
                    a = MaskAt(m, KHD2DRLE_W, KHD2DRLE_H, ((float)x - ox) / sc, ((float)y - oy) / sc);
                    if (a > 0.0f)
                        OverPx(px, 0.10f, 0.12f, 0.22f, 0.97f * a);
                }
            }
    sCrestTex = FloatToTexture(f, w, h);
    sCrestW = w;
    sCrestH = h;
    free(m);
    free(f);
    free(in);
    return sCrestTex != 0;
}

static bool BuildBadge(void)
{
    return BuildPlate() && BuildCrest();
}

static bool Build(void)
{
    if (!DeckGl_Init())
        return false;
    if (!DeckGl_BuildProgram(&sLayer, "title_layer", kVs, kLayerFs) || !DeckGl_BuildProgram(&sRay, "title_ray", kVs, kRayFs)
        || !DeckGl_BuildProgram(&sFront, "title_front", kVs, kFrontFs) || !DeckGl_BuildProgram(&sShine, "title_shine", kVs, kShineFs)
        || !DeckGl_BuildProgram(&sSoft, "title_soft", kVs, kSoftFs) || !DeckGl_BuildProgram(&sRays, "title_rays", kVs, kRaysFs))
        return false;
    sLyTex = DeckGl_Uniform(&sLayer, "uTex");
    sLyBias = DeckGl_Uniform(&sLayer, "uBias");
    sRyTex = DeckGl_Uniform(&sRay, "uTex");
    sRyMark = DeckGl_Uniform(&sRay, "uMark");
    sRySize = DeckGl_Uniform(&sRay, "uSize");
    sRyScale = DeckGl_Uniform(&sRay, "uScale");
    sRyMarkCol = DeckGl_Uniform(&sRay, "uMarkCol");
    sRyRim = DeckGl_Uniform(&sRay, "uRim");
    sFrTex = DeckGl_Uniform(&sFront, "uTex");
    sFrTexel = DeckGl_Uniform(&sFront, "uTexel");
    sFrRad = DeckGl_Uniform(&sFront, "uRad");
    sFrMode = DeckGl_Uniform(&sFront, "uMode");
    sFrAmt = DeckGl_Uniform(&sFront, "uAmt");
    sFrBanner = DeckGl_Uniform(&sFront, "uBanner");
    sFrBMode = DeckGl_Uniform(&sFront, "uBMode");
    sShTex = DeckGl_Uniform(&sShine, "uTex");
    sShPos = DeckGl_Uniform(&sShine, "uPos");
    sShAmt = DeckGl_Uniform(&sShine, "uAmt");
    sRaysT = DeckGl_Uniform(&sRays, "uT");
    sRaysAmt = DeckGl_Uniform(&sRays, "uAmt");
    if (!BuildBadge())
    {
        DeckGl_Log("[TITLE] badge texture failed\n");
        return false;
    }
    return true;
}

static void Fail(const char *why)
{
    DeckGl_Log("[TITLE] %s: the original title screen is drawn (fallback)\n", why);
    sFailed = true;
    sActive = false;
}

bool DeckTitle_Wanted(void)
{
    const int phase = gDeckTitlePhase;

    sActive = false;
    if (!sEnvRead)
    {
        const char *e = getenv("DECK_TITLE_ORIGINAL");

        sEnvRead = true;
        sEnvOrig = e != NULL && strcmp(e, "0") != 0;
        if (sEnvOrig)
            DeckGl_Log("[TITLE] original title by DECK_TITLE_ORIGINAL\n");
    }
    if (phase == 0)
    {
        sPrevPhase = 0;
        sPhase3At = -1.0;
        return false;
    }
    if (sEnvOrig || sFailed || VitaExtras_GetFast(FAST_EXTRA_VOXEL_SHADERS) == 0 || VitaExtras_GetFast(FAST_EXTRA_VOXEL_TITLE) == 0)
        return false;
    if (!DeckReveal_Hd())
        return false;   // the reveal has not flashed yet: the plain original title is drawn
    if (!sInit)
    {
        sInit = true;
        if (!Build())
        {
            Fail("build failed");
            return false;
        }
        DeckGl_Log("[TITLE] ready (HD-2D title)\n");
    }
    if (sPrevPhase == 0)
    {
        // the title just started: restart the clock and read the art the state machine loaded into VRAM and palette RAM
        sStart = Now();
        sFrames = 0;
        sArtOk = GrabArt();
        if (!sArtOk)
        {
            Fail("art not in VRAM");
            return false;
        }
    }
    if (!sArtOk)
        return false;
    if (phase == 3 && sPhase3At < 0.0)
        sPhase3At = Now();
    sPrevPhase = phase;
    sActive = true;
    return true;
}

bool DeckTitle_FrameActive(void)
{
    return sActive;
}

// ------------------------------------------------------------------------------- drawing helpers
// textured quad: (x0,y0)-(x1,y1) on screen, uv rect, vertex colour; the third texcoord carries a value for the shaders
static void QuadT(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, float z0, float z1, float r, float g, float b, float a)
{
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glTexCoord3f(u0, v0, z0); glVertex2f(x0, y0);
    glTexCoord3f(u1, v0, z1); glVertex2f(x1, y0);
    glTexCoord3f(u1, v1, z1); glVertex2f(x1, y1);
    glTexCoord3f(u0, v1, z0); glVertex2f(x0, y1);
    glEnd();
}

static float Hash(unsigned n)
{
    n = (n << 13) ^ n;
    return (float)(((n * (n * n * 15731u + 789221u) + 1376312589u) & 0x7fffffffu)) / 2147483648.0f;
}

static void Disc(float x, float y, float rad, float r, float g, float b, float a, float hard)
{
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glTexCoord3f(-1.0f, -1.0f, hard); glVertex2f(x - rad, y - rad);
    glTexCoord3f(1.0f, -1.0f, hard); glVertex2f(x + rad, y - rad);
    glTexCoord3f(1.0f, 1.0f, hard); glVertex2f(x + rad, y + rad);
    glTexCoord3f(-1.0f, 1.0f, hard); glVertex2f(x - rad, y + rad);
    glEnd();
}

static void DiscXY(float x, float y, float rx, float ry, float r, float g, float b, float a, float hard)
{
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glTexCoord3f(-1.0f, -1.0f, hard); glVertex2f(x - rx, y - ry);
    glTexCoord3f(1.0f, -1.0f, hard); glVertex2f(x + rx, y - ry);
    glTexCoord3f(1.0f, 1.0f, hard); glVertex2f(x + rx, y + ry);
    glTexCoord3f(-1.0f, 1.0f, hard); glVertex2f(x - rx, y + ry);
    glEnd();
}

typedef struct { float x, y, sp, amp, ph, size, depth; } Mote;
#define N_MOTES 64
static Mote sMotes[N_MOTES];
static bool sMotesInit;

static void InitMotes(void)
{
    for (int i = 0; i < N_MOTES; i++)
    {
        Mote *m = &sMotes[i];

        m->x = Hash((unsigned)i * 3u + 100u) * SW;
        m->y = 40.0f + Hash((unsigned)i * 5u + 101u) * 460.0f;
        m->sp = 4.0f + Hash((unsigned)i * 7u + 102u) * 12.0f;
        m->amp = 8.0f + Hash((unsigned)i * 11u + 103u) * 24.0f;
        m->ph = Hash((unsigned)i * 13u + 104u) * 6.2831f;
        m->depth = Hash((unsigned)i * 17u + 105u);
        m->size = m->depth < 0.75f ? 1.3f + 1.7f * Hash((unsigned)i * 19u + 106u) : 10.0f + 14.0f * Hash((unsigned)i * 19u + 106u);
    }
    sMotesInit = true;
}

static float EdgeFade(float x, float lo, float hi, float ramp)
{
    return Smooth(lo, lo + ramp, x) * (1.0f - Smooth(hi - ramp, hi, x));
}

void DeckTitle_Draw(GLuint frontTex, float vx0, float vx1, float u0, float u1)
{
    const double now = Now();
    const float t = (float)(now - sStart);
    const int phase = gDeckTitlePhase;
    const float camX = sinf(t * 0.10f) * 0.9f + sinf(t * 0.043f + 1.7f) * 0.5f;
    const float camY = sinf(t * 0.07f + 1.0f) * 0.7f;
    const float intro = DeckReveal_On() ? DeckReveal_Amount() : Smooth(0.0f, 1.8f, t);
    const float tf = DeckReveal_SinceFlip(), tb = DeckReveal_SinceBadge();
    const float dropU = Clamp01((tf - 0.12f) / 0.95f), bannerU = Clamp01((tf - 0.05f) / 1.0f);
    const float bannerShift = 52.0f * (bannerU * bannerU * (3.0f - 2.0f * bannerU));
    const float p3 = sPhase3At >= 0.0 ? Smooth(0.0f, 1.6f, (float)(now - sPhase3At)) : 0.0f;
    const int fadeY = gPaletteFade.y;
    float fade = (float)fadeY / 16.0f;
    GLint vp[4];
    float winScale;

    glGetIntegerv(GL_VIEWPORT, vp);
    winScale = (float)vp[2] / SW;
    if (!sMotesInit)
        InitMotes();
    if (++sFrames == 2 || sFrames % 600 == 0)
        DeckGl_Log("[TITLE] frame %u phase %d t=%.1f fade y=%d\n", sFrames, phase, (double)t, fadeY);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_TEXTURE_2D);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    DeckGl_ActiveTexture(GL_TEXTURE0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    // ---- sky: the smoothed gradient of the original bands, a touch larger than the picture so the drift never shows an edge
    DeckGl_UseProgram(&sLayer);
    DeckGl_Set1i(sLyTex, 0);
    DeckGl_Set1f(sLyBias, 0.0f);
    glBindTexture(GL_TEXTURE_2D, sSkyTex);
    QuadT(-20.0f, -20.0f, SW + 20.0f, SH + 20.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0, 0, 1, 1, 1, 1);

    // ---- far clouds: the same cloud art larger-grained, blurred by mip bias, slower; behind Rayquaza
    {
        const float s = 2.6f, size = 256.0f * s;
        const float ox = -camX * 10.0f + sinf(t * 0.11f) * 14.0f, oy = -t * 9.0f - camY * 6.0f;

        glBindTexture(GL_TEXTURE_2D, sCloudTex);
        DeckGl_Set1f(sLyBias, 1.6f);
        QuadT(0.0f, 0.0f, SW, SH, (0.0f - ox) / size, (0.0f - oy) / size, (SW - ox) / size, (SH - oy) / size, 0, 0, 0.85f, 0.95f, 1.0f, 0.34f * intro);
    }

    // ---- Rayquaza: the silhouette as its own sprite; appears with PRESS START as in the original
    if (p3 > 0.0f)
    {
        const float bob = sinf(t * 0.42f) * 6.0f - camY * 5.0f, breathe = 1.0f + 0.008f * sinf(t * 0.7f);
        const float cx = ART_X0 + ART_W * 0.5f - camX * 9.0f, bottom = SH + 18.0f + bob;
        const float w = ART_W * 1.035f * breathe, h = SH * 1.035f * breathe;
        uint8_t mrgb[3];

        Rgb555(gPlttBufferUnfaded[14 * 16 + 15], mrgb);
        DeckGl_UseProgram(&sRay);
        DeckGl_ActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, sMarkTex);
        DeckGl_ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sRayTex);
        DeckGl_Set1i(sRyTex, 0);
        DeckGl_Set1i(sRyMark, 1);
        DeckGl_Set2f(sRySize, (float)ART_W_TEXELS, (float)ART_H_TEXELS);
        DeckGl_Set1f(sRyScale, TEX_SCALE * winScale * 1.035f);
        DeckGl_Set3f(sRyMarkCol, (float)mrgb[0] / 255.0f, (float)mrgb[1] / 255.0f, (float)mrgb[2] / 255.0f);
        DeckGl_Set1f(sRyRim, 0.55f);
        QuadT(cx - w * 0.5f, bottom - h, cx + w * 0.5f, bottom, 0.0f, 0.0f, 1.0f, 1.0f, 0, 0, 1, 1, 1, p3);
        DeckGl_ActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        DeckGl_ActiveTexture(GL_TEXTURE0);
    }

    // ---- near clouds: the original layer as it is drawn, drifting up as the game scrolls it, over Rayquaza
    {
        const float s = 3.4f, size = 256.0f * s;
        const float ox = -camX * 22.0f + sinf(t * 0.17f) * 10.0f, oy = -t * 17.0f - camY * 10.0f;

        DeckGl_UseProgram(&sLayer);
        DeckGl_Set1i(sLyTex, 0);
        glBindTexture(GL_TEXTURE_2D, sCloudTex);
        DeckGl_Set1f(sLyBias, 0.0f);
        QuadT(0.0f, 0.0f, SW, SH, (0.0f - ox) / size, (0.0f - oy) / size, (SW - ox) / size, (SH - oy) / size, 0, 0, 1, 1, 1, 0.40f * p3);
    }

    // ---- light rays and motes (added)
    glBlendFunc(GL_ONE, GL_ONE);
    DeckGl_UseProgram(&sRays);
    DeckGl_Set1f(sRaysT, t);
    DeckGl_Set1f(sRaysAmt, 0.42f * intro);
    QuadT(0.0f, 0.0f, SW, SH, 0.0f, 0.0f, 1.0f, 1.0f, 0, 0, 1, 1, 1, 1);
    DeckGl_UseProgram(&sSoft);
    for (int i = 0; i < N_MOTES; i++)
    {
        const Mote *m = &sMotes[i];
        const float parallax = 0.4f + 1.4f * m->depth;
        const float x = fmodf(m->x + t * m->sp * (m->depth < 0.75f ? 1.0f : 0.7f) - camX * 24.0f * parallax + 2.0f * SW, SW + 120.0f) - 60.0f;
        const float y = m->y + sinf(t * 0.5f + m->ph) * m->amp + sinf(t * 0.23f + m->ph * 2.1f) * m->amp * 0.6f;
        const float tw = 0.55f + 0.45f * sinf(t * 1.2f + m->ph * 3.0f);
        const float fx = EdgeFade(x, -60.0f, SW + 60.0f, 120.0f);

        if (m->depth >= 0.75f)
            Disc(x, y, m->size, 1.0f, 0.95f, 0.70f, 0.07f * tw * intro * fx, 0.8f);   // near bokeh, out of focus
        else
            Disc(x, y, m->size * 1.8f, 1.0f, 0.95f, 0.72f, 0.55f * tw * intro * fx, 0.3f);
    }

    // ---- the game's palette fade over everything behind the front layer (the front layer carries its own)
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    DeckGl_UseProgram(NULL);
    if (fade > 0.0f)
    {
        const uint16_t bc = (uint16_t)gPaletteFade.blendColor;
        const float r = (float)(bc & 31) / 31.0f, g = (float)((bc >> 5) & 31) / 31.0f, b = (float)((bc >> 10) & 31) / 31.0f;

        if (fade > 1.0f)
            fade = 1.0f;
        glColor4f(r, g, b, fade);
        glBegin(GL_QUADS);
        glVertex2f(0.0f, 0.0f); glVertex2f(SW, 0.0f); glVertex2f(SW, SH); glVertex2f(0.0f, SH);
        glEnd();
    }

    // ---- front layer: soft glow behind, the picture (rim lit), a shine sweep
    {
        const float fw = vx1 - vx0, fu = u1 - u0;
        const float shinePhase = fmodf(t, 7.0f) / 1.8f;

        DeckGl_UseProgram(&sFront);
        glBindTexture(GL_TEXTURE_2D, frontTex);
        DeckGl_Set1i(sFrTex, 0);
        DeckGl_Set2f(sFrTexel, 1.0f / 288.0f, 1.0f / 160.0f);
        DeckGl_Set2f(sFrRad, 7.0f / fw * fu, 7.0f / SH);
        // EMERALD VERSION (low saturation texels inside this uv rect) is drawn separately so it can be pushed down
        DeckGl_Set4f(sFrBanner, (24.0f + 96.0f) / 288.0f, 0.0f, (24.0f + 228.0f) / 288.0f, 92.0f / 160.0f);
        DeckGl_Set1f(sFrBMode, phase >= 2 ? 1.0f : 0.0f);
        glBlendFunc(GL_ONE, GL_ONE);
        DeckGl_Set1f(sFrMode, 0.0f);
        DeckGl_Set1f(sFrAmt, 0.35f * intro);
        QuadT(vx0, 0.0f, vx1, SH, u0, 0.0f, u1, 1.0f, 0, 0, 1, 1, 1, 1);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        DeckGl_Set1f(sFrMode, 1.0f);
        DeckGl_Set1f(sFrAmt, 1.0f);
        QuadT(vx0, 0.0f, vx1, SH, u0, 0.0f, u1, 1.0f, 0, 0, 1, 1, 1, 1);
        if (phase >= 2)
        {
            DeckGl_Set1f(sFrBMode, 2.0f);
            QuadT(vx0, bannerShift, vx1, SH + bannerShift, u0, 0.0f, u1, 1.0f, 0, 0, 1, 1, 1, 1);
            DeckGl_Set1f(sFrBMode, 1.0f);
        }
        if (shinePhase < 1.0f)
        {
            glBlendFunc(GL_ONE, GL_ONE);
            DeckGl_UseProgram(&sShine);
            DeckGl_Set1i(sShTex, 0);
            DeckGl_Set1f(sShPos, shinePhase * 1.5f - 0.3f);
            DeckGl_Set1f(sShAmt, 0.7f * sinf(shinePhase * 3.1416f));
            QuadT(vx0, 0.0f, vx1, SH, u0, 0.0f, u1, 1.0f, 0.0f, 1.0f, 1, 1, 1, 1);
        }
    }

    // ---- BRAVELY plate: drops in from above over the logo's lower edge (the banner has moved down for it); then the
    // HD-2D crest in the top right with a sparkle. Both after the front layer, both fade with the game's palette fade.
    if (phase >= 2 && tf > 0.0f)
    {
        const float u = dropU, back = 1.2f, v = u - 1.0f;
        const float ease = 1.0f + (back + 1.0f) * v * v * v + back * v * v;
        const float pw = (float)sPlateW * 0.46f, ph = (float)sPlateH * 0.46f;
        const float cx = 480.0f - camX * 3.0f, cy = 199.0f - (1.0f - ease) * 120.0f;

        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        DeckGl_UseProgram(&sLayer);
        DeckGl_Set1i(sLyTex, 0);
        DeckGl_Set1f(sLyBias, 0.0f);
        glBindTexture(GL_TEXTURE_2D, sPlateTex);
        QuadT(cx - pw * 0.5f, cy - ph * 0.5f, cx + pw * 0.5f, cy + ph * 0.5f, 0.0f, 0.0f, 1.0f, 1.0f, 0, 0, 1, 1, 1, Smooth(0.0f, 0.45f, u) * (1.0f - fade));
        if (tb > 0.0f)
        {
            const float bu = Clamp01(tb / 0.6f), bv = bu - 1.0f;
            const float be = 1.0f + 2.4f * bv * bv * bv + 1.4f * bv * bv;
            const float k = 0.5f + 0.5f * be;
            const float bw = (float)sCrestW * 0.40f * k, bh = (float)sCrestH * 0.40f * k;
            const float bx = 888.0f, by = 100.0f + sinf(t * 0.8f) * 1.2f;
            const float sp = sinf(3.1416f * Clamp01(tb / 1.2f));

            glBindTexture(GL_TEXTURE_2D, sCrestTex);
            QuadT(bx - bw * 0.5f, by - bh * 0.5f, bx + bw * 0.5f, by + bh * 0.5f, 0.0f, 0.0f, 1.0f, 1.0f, 0, 0, 1, 1, 1, Smooth(0.0f, 0.3f, tb) * (1.0f - fade));
            if (sp > 0.01f)
            {
                // four-point sparkle on the crest's upper left facet
                const float sx = bx - 22.0f, sy = by - 30.0f;

                glBlendFunc(GL_ONE, GL_ONE);
                DeckGl_UseProgram(&sSoft);
                DiscXY(sx, sy, 40.0f * sp, 3.5f * sp, 1.0f, 0.97f, 0.85f, 0.9f * sp, 0.0f);
                DiscXY(sx, sy, 3.5f * sp, 40.0f * sp, 1.0f, 0.97f, 0.85f, 0.9f * sp, 0.0f);
                DiscXY(sx, sy, 11.0f * sp, 11.0f * sp, 1.0f, 1.0f, 0.95f, 0.8f * sp, 0.3f);
            }
        }
    }

    glDisable(GL_BLEND);
    DeckGl_UseProgram(NULL);
    glEnable(GL_TEXTURE_2D);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBindTexture(GL_TEXTURE_2D, 0);
}
