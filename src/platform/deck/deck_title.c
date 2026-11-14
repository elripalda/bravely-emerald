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
#include "platform/vita_extras.h"

// Written by src/title_screen.c: 0 off the title, 1 logo shine, 2 version banner, 3 PRESS START, 4 leaving.
extern u8 gDeckTitlePhase;

// ====================================================================================================
// Design (also in docs/DECK_NOTES.md, "Title screen"): layers back to front, all in the logical 960x544
// picture (y down). A fullscreen shader paints sky, sun, two cloud layers, island silhouettes and the
// sea; Rayquaza (the original BG0 art, read from VRAM) floats in front with its own drift; the logo is
// drawn from a CPU-built atlas (a 5x7 pixel font turned into bevelled, glowing letters); motes, fireflies
// and out-of-focus foreground grass are small soft quads moved by sines of the clock. The depth blur is
// per layer: wider edges and mip bias far back, soft blades and bokeh discs in front.
// ====================================================================================================

#define SW 960.0f
#define SH 544.0f
#define ATLAS_W 1024
#define ATLAS_H 512

// ---------------------------------------------------------------------------------------------- font
typedef struct { char c; const char *r[7]; } Glyph;

static const Glyph kFont[] = {
    { 'A', { ".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#" } },
    { 'B', { "####.", "#...#", "#...#", "####.", "#...#", "#...#", "####." } },
    { 'C', { ".###.", "#...#", "#....", "#....", "#....", "#...#", ".###." } },
    { 'D', { "####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####." } },
    { 'E', { "#####", "#....", "#....", "####.", "#....", "#....", "#####" } },
    { 'F', { "#####", "#....", "#....", "####.", "#....", "#....", "#...." } },
    { 'G', { ".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".###." } },
    { 'H', { "#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#" } },
    { 'I', { ".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###." } },
    { 'J', { "..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.." } },
    { 'K', { "#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#" } },
    { 'L', { "#....", "#....", "#....", "#....", "#....", "#....", "#####" } },
    { 'M', { "#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#" } },
    { 'N', { "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#", "#...#" } },
    { 'O', { ".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###." } },
    { 'P', { "####.", "#...#", "#...#", "####.", "#....", "#....", "#...." } },
    { 'Q', { ".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#" } },
    { 'R', { "####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#" } },
    { 'S', { ".####", "#....", "#....", ".###.", "....#", "....#", "####." } },
    { 'T', { "#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.." } },
    { 'U', { "#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###." } },
    { 'V', { "#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.." } },
    { 'W', { "#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#" } },
    { 'X', { "#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#" } },
    { 'Y', { "#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.." } },
    { 'Z', { "#####", "....#", "...#.", "..#..", ".#...", "#....", "#####" } },
    { '0', { ".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###." } },
    { '1', { "..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###." } },
    { '2', { ".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####" } },
    { '3', { "####.", "....#", "....#", ".###.", "....#", "....#", "####." } },
    { '4', { "#...#", "#...#", "#...#", "#####", "....#", "....#", "....#" } },
    { '5', { "#####", "#....", "####.", "....#", "....#", "#...#", ".###." } },
    { '6', { ".###.", "#....", "#....", "####.", "#...#", "#...#", ".###." } },
    { '7', { "#####", "....#", "...#.", "..#..", "..#..", "..#..", "..#.." } },
    { '8', { ".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###." } },
    { '9', { ".###.", "#...#", "#...#", ".####", "....#", "....#", ".###." } },
    { '-', { ".....", ".....", ".....", "#####", ".....", ".....", "....." } },
    { '.', { ".....", ".....", ".....", ".....", ".....", ".##..", ".##.." } },
    { '/', { "....#", "....#", "...#.", "..#..", ".#...", "#....", "#...." } },
    { '(', { "..#..", ".#...", "#....", "#....", "#....", ".#...", "..#.." } },
    { ')', { "..#..", "...#.", "....#", "....#", "....#", "...#.", "..#.." } },
};

static const Glyph *FindGlyph(char c)
{
    for (size_t i = 0; i < sizeof(kFont) / sizeof(kFont[0]); i++)
        if (kFont[i].c == c)
            return &kFont[i];
    return NULL;
}

static int GlyphBit(const Glyph *g, int x, int y)
{
    if (g == NULL || x < 0 || x > 4 || y < 0 || y > 6)
        return 0;
    return g->r[y][x] == '#';
}

// ------------------------------------------------------------------------------------- text to atlas
typedef struct
{
    float top[3], bot[3];  // face gradient, top to bottom of the text
    float rim[3];          // thin edge line and highlight colour
    float line[3];         // outline and soft glow colour
    float outline;         // outline width, texels
    float bevel;           // bevel width, texels (0 = flat)
    float glow;            // peak alpha of the soft dark glow outside the outline
} Style;

static const Style kStyleBig = { { 0.66f, 1.00f, 0.74f }, { 0.01f, 0.50f, 0.30f }, { 1.00f, 0.93f, 0.62f }, { 0.01f, 0.09f, 0.07f }, 4.0f, 8.0f, 0.55f };
static const Style kStylePoke = { { 1.00f, 0.98f, 0.86f }, { 0.92f, 0.70f, 0.30f }, { 1.00f, 1.00f, 0.95f }, { 0.10f, 0.07f, 0.02f }, 2.5f, 3.5f, 0.45f };
static const Style kStyleRibbon = { { 0.92f, 0.24f, 0.16f }, { 0.52f, 0.05f, 0.10f }, { 1.00f, 0.84f, 0.40f }, { 0.14f, 0.02f, 0.04f }, 3.0f, 5.0f, 0.40f };
static const Style kStyleRibbonText = { { 1.00f, 0.97f, 0.82f }, { 0.96f, 0.76f, 0.34f }, { 1.00f, 1.00f, 0.92f }, { 0.28f, 0.03f, 0.05f }, 2.0f, 2.5f, 0.0f };
static const Style kStylePress = { { 1.00f, 1.00f, 1.00f }, { 0.80f, 0.94f, 0.90f }, { 1.00f, 1.00f, 1.00f }, { 0.02f, 0.10f, 0.10f }, 2.0f, 1.5f, 0.50f };
static const Style kStyleSmall = { { 0.94f, 0.96f, 0.92f }, { 0.80f, 0.86f, 0.82f }, { 1.00f, 1.00f, 1.00f }, { 0.02f, 0.06f, 0.06f }, 1.2f, 0.0f, 0.55f };

static float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static float Smooth(float a, float b, float x)
{
    const float t = Clamp01((x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}

// Two pass 3-4 chamfer distance to the nearest pixel with target[i] != 0 (0 on a target). Result in texels.
static void Chamfer(const uint8_t *target, int w, int h, float *out)
{
    const int inf = 1 << 20;
    int *d = (int *)malloc((size_t)w * (size_t)h * sizeof(int));

    if (d == NULL)
    {
        for (int i = 0; i < w * h; i++)
            out[i] = 0.0f;
        return;
    }
    for (int i = 0; i < w * h; i++)
        d[i] = target[i] ? 0 : inf;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
        {
            int v = d[y * w + x];
            if (x > 0 && d[y * w + x - 1] + 3 < v) v = d[y * w + x - 1] + 3;
            if (y > 0)
            {
                if (d[(y - 1) * w + x] + 3 < v) v = d[(y - 1) * w + x] + 3;
                if (x > 0 && d[(y - 1) * w + x - 1] + 4 < v) v = d[(y - 1) * w + x - 1] + 4;
                if (x < w - 1 && d[(y - 1) * w + x + 1] + 4 < v) v = d[(y - 1) * w + x + 1] + 4;
            }
            d[y * w + x] = v;
        }
    for (int y = h - 1; y >= 0; y--)
        for (int x = w - 1; x >= 0; x--)
        {
            int v = d[y * w + x];
            if (x < w - 1 && d[y * w + x + 1] + 3 < v) v = d[y * w + x + 1] + 3;
            if (y < h - 1)
            {
                if (d[(y + 1) * w + x] + 3 < v) v = d[(y + 1) * w + x] + 3;
                if (x < w - 1 && d[(y + 1) * w + x + 1] + 4 < v) v = d[(y + 1) * w + x + 1] + 4;
                if (x > 0 && d[(y + 1) * w + x - 1] + 4 < v) v = d[(y + 1) * w + x - 1] + 4;
            }
            d[y * w + x] = v;
        }
    for (int i = 0; i < w * h; i++)
        out[i] = (float)d[i] / 3.0f;
    free(d);
}

// Composites premultiplied (r,g,b,a in 0..1) over the atlas texel.
static void OverTexel(uint8_t *px, float r, float g, float b, float a)
{
    const float ia = 1.0f - a;

    px[0] = (uint8_t)(Clamp01(r + px[0] / 255.0f * ia) * 255.0f + 0.5f);
    px[1] = (uint8_t)(Clamp01(g + px[1] / 255.0f * ia) * 255.0f + 0.5f);
    px[2] = (uint8_t)(Clamp01(b + px[2] / 255.0f * ia) * 255.0f + 0.5f);
    px[3] = (uint8_t)(Clamp01(a + px[3] / 255.0f * ia) * 255.0f + 0.5f);
}

// Shades a coverage mask (w x h, 0..1) with the style and composites it into the atlas at (ox, oy).
static void ShadeMask(uint8_t *atlas, int ox, int oy, int w, int h, const float *mask, const Style *st, int pad)
{
    uint8_t *inside = (uint8_t *)malloc((size_t)w * (size_t)h);
    uint8_t *outside = (uint8_t *)malloc((size_t)w * (size_t)h);
    float *dIn = (float *)malloc((size_t)w * (size_t)h * sizeof(float));
    float *dOut = (float *)malloc((size_t)w * (size_t)h * sizeof(float));
    float *sd = (float *)malloc((size_t)w * (size_t)h * sizeof(float));
    float *sds = (float *)malloc((size_t)w * (size_t)h * sizeof(float));

    if (!inside || !outside || !dIn || !dOut || !sd || !sds)
        goto done;
    for (int i = 0; i < w * h; i++)
    {
        inside[i] = mask[i] > 0.5f;
        outside[i] = !inside[i];
    }
    Chamfer(outside, w, h, dIn);   // inside texels: distance to the nearest outside texel
    Chamfer(inside, w, h, dOut);   // outside texels: distance to the nearest inside texel
    for (int i = 0; i < w * h; i++)
        sd[i] = inside[i] ? dIn[i] - 0.5f : -(dOut[i] - 0.5f);
    // 3x3 box blur: smooth contours for the bevel normals
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
        {
            float s = 0.0f;
            int n = 0;

            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                {
                    const int xx = x + dx, yy = y + dy;

                    if (xx >= 0 && xx < w && yy >= 0 && yy < h)
                    {
                        s += sd[yy * w + xx];
                        n++;
                    }
                }
            sds[y * w + x] = s / (float)n;
        }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
        {
            const int i = y * w + x;
            const float s = sds[i];
            const float covFace = Clamp01(sd[i] + 0.5f);
            const float covOut = Clamp01(sd[i] + st->outline + 0.5f);
            const float ty = Clamp01(((float)y - (float)pad) / (float)(h - 2 * pad));
            uint8_t *px = &atlas[((size_t)(oy + y) * ATLAS_W + (size_t)(ox + x)) * 4];
            float gx = 0.0f, gy = 0.0f, nx = 0.0f, ny = 0.0f, len;

            if (x > 0 && x < w - 1)
                gx = (sds[i + 1] - sds[i - 1]) * 0.5f;
            if (y > 0 && y < h - 1)
                gy = (sds[i + w] - sds[i - w]) * 0.5f;
            len = sqrtf(gx * gx + gy * gy);
            if (len > 1e-4f)
            {
                nx = -gx / len;   // outward: the signed distance grows inward
                ny = -gy / len;
            }
            // soft dark glow outside the outline (legibility over the bright sea and sky)
            if (st->glow > 0.0f && covOut < 1.0f)
            {
                const float outD = fmaxf(0.0f, -s - st->outline);
                const float a = st->glow * expf(-outD / 7.0f) * (1.0f - covOut);

                OverTexel(px, st->line[0] * 0.6f * a, st->line[1] * 0.6f * a, st->line[2] * 0.6f * a, a);
            }
            if (covOut > 0.0f)
                OverTexel(px, st->line[0] * covOut, st->line[1] * covOut, st->line[2] * covOut, covOut);
            if (covFace > 0.0f)
            {
                const float bevT = st->bevel > 0.0f ? Clamp01(1.0f - s / st->bevel) : 0.0f;
                const float lit = fmaxf(0.0f, nx * -0.55f + ny * -0.83f);
                const float shd = fmaxf(0.0f, nx * 0.55f + ny * 0.83f);
                const float gl = Smooth(0.46f, 0.54f, ty);
                const float gloss = 1.12f - 0.22f * gl;
                const float edge = 1.0f - Smooth(0.0f, 1.3f, s);
                float c[3];

                for (int k = 0; k < 3; k++)
                {
                    float base = (st->top[k] + (st->bot[k] - st->top[k]) * ty) * gloss;

                    base = base * (1.0f - 0.40f * shd * bevT) + st->rim[k] * (0.70f * lit * bevT);
                    base = base + (st->rim[k] - base) * (0.55f * edge);
                    c[k] = Clamp01(base);
                }
                OverTexel(px, c[0] * covFace, c[1] * covFace, c[2] * covFace, covFace);
            }
        }
done:
    free(inside);
    free(outside);
    free(dIn);
    free(dOut);
    free(sd);
    free(sds);
}

static int TextWidth(const char *s, int scale, int extra, int pad)
{
    return (int)strlen(s) * (6 * scale + extra) - extra + 2 * pad;
}

// Builds the coverage mask of a string: bilinear samples of the 5x7 glyph bitmaps, thresholded smoothly, so the pixel
// font keeps its blocky character but loses the staircase on diagonals.
static float *TextMask(const char *s, int scale, int extra, int pad, int *outW, int *outH)
{
    const int w = TextWidth(s, scale, extra, pad), h = 7 * scale + 2 * pad;
    const int pitch = 6 * scale + extra;
    float *m = (float *)calloc((size_t)w * (size_t)h, sizeof(float));

    if (m == NULL)
        return NULL;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
        {
            const int px = x - pad, py = y - pad;
            int ci;
            float lx, ly, fx, fy, v;
            int ix, iy;
            const Glyph *g;

            if (px < 0 || py < 0 || py >= 7 * scale)
                continue;
            ci = px / pitch;
            if (ci >= (int)strlen(s))
                continue;
            lx = ((float)(px - ci * pitch) + 0.5f) / (float)scale - 0.5f;
            ly = ((float)py + 0.5f) / (float)scale - 0.5f;
            if (px - ci * pitch >= 5 * scale + scale / 2)
                continue;
            g = FindGlyph(s[ci]);
            ix = (int)floorf(lx);
            iy = (int)floorf(ly);
            fx = lx - (float)ix;
            fy = ly - (float)iy;
            v = (float)GlyphBit(g, ix, iy) * (1.0f - fx) * (1.0f - fy) + (float)GlyphBit(g, ix + 1, iy) * fx * (1.0f - fy)
              + (float)GlyphBit(g, ix, iy + 1) * (1.0f - fx) * fy + (float)GlyphBit(g, ix + 1, iy + 1) * fx * fy;
            m[y * w + x] = Smooth(0.38f, 0.62f, v);
        }
    *outW = w;
    *outH = h;
    return m;
}

typedef struct { int x, y, w, h; } Rect;
static Rect sRectBig, sRectPoke, sRectRibbon, sRectPress, sRectCopy1, sRectCopy2;

static bool PutText(uint8_t *atlas, int ox, int oy, const char *s, int scale, int extra, int pad, const Style *st, Rect *rect)
{
    int w, h;
    float *m = TextMask(s, scale, extra, pad, &w, &h);

    if (m == NULL)
        return false;
    ShadeMask(atlas, ox, oy, w, h, m, st, pad);
    free(m);
    rect->x = ox;
    rect->y = oy;
    rect->w = w;
    rect->h = h;
    return true;
}

// Ribbon: a band with swallowtail ends, red with a gold edge, "2D-HD" in cream.
static bool PutRibbon(uint8_t *atlas, int ox, int oy, Rect *rect)
{
    const int w = 360, h = 84, pad = 14;
    float *m = (float *)calloc((size_t)w * (size_t)h, sizeof(float));
    int tw, th;
    float *tm;

    if (m == NULL)
        return false;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
        {
            const float fx = (float)x, fy = (float)y;
            int in = 0;

            if (fx >= 46.0f && fx <= 314.0f && fy >= 14.0f && fy <= 56.0f)
                in = 1;   // the band
            if (fx >= 14.0f && fx <= 70.0f && fy >= 26.0f && fy <= 70.0f)
            {
                const float notch = 22.0f * (1.0f - fabsf(fy - 48.0f) / 22.0f);   // swallowtail cut

                in = in || (fabsf(fy - 48.0f) >= 22.0f || fx >= 14.0f + notch);
            }
            if (fx >= 290.0f && fx <= 346.0f && fy >= 26.0f && fy <= 70.0f)
            {
                const float notch = 22.0f * (1.0f - fabsf(fy - 48.0f) / 22.0f);

                in = in || (fabsf(fy - 48.0f) >= 22.0f || fx <= 346.0f - notch);
            }
            m[y * w + x] = (float)in;
        }
    ShadeMask(atlas, ox, oy, w, h, m, &kStyleRibbon, pad);
    free(m);
    tm = TextMask("2D-HD", 5, 5, 6, &tw, &th);
    if (tm == NULL)
        return false;
    {
        // the text sits on the band, centred on x, over the upper part
        float *full = (float *)calloc((size_t)w * (size_t)h, sizeof(float));
        const int tx = (w - tw) / 2, ty = 10;

        if (full == NULL)
        {
            free(tm);
            return false;
        }
        for (int y = 0; y < th && y + ty < h; y++)
            for (int x = 0; x < tw && x + tx < w; x++)
                full[(y + ty) * w + x + tx] = tm[y * tw + x];
        ShadeMask(atlas, ox, oy, w, h, full, &kStyleRibbonText, pad);
        free(full);
    }
    free(tm);
    rect->x = ox;
    rect->y = oy;
    rect->w = w;
    rect->h = h;
    return true;
}

// ------------------------------------------------------------------------------------ shaders
static const char kSpriteVs[] =
    "#version 120\n"
    "varying vec3 vT;\n"
    "varying vec4 vC;\n"
    "void main() {\n"
    "    vT = gl_MultiTexCoord0.xyz;\n"
    "    vC = gl_Color;\n"
    "    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
    "}\n";

// premultiplied texture, mip bias for the depth blur, a diagonal shine sweep, vertical fade, tint in the vertex colour
static const char kSpriteFs[] =
    "#version 120\n"
    "uniform sampler2D uTex;\n"
    "uniform float uBias;\n"
    "uniform vec4 uRect;\n"      // u0, v0, u1, v1 of the quad in the atlas
    "uniform vec4 uShine;\n"     // centre (0..1 across, slanted), width, strength
    "uniform vec2 uVFade;\n"     // local v where alpha starts to fall, local v where it is 0 (1,1 = none)
    "varying vec3 vT;\n"
    "varying vec4 vC;\n"
    "void main() {\n"
    "    vec4 t = texture2D(uTex, vT.xy, uBias);\n"
    "    vec2 l = (vT.xy - uRect.xy) / (uRect.zw - uRect.xy);\n"
    "    float s = l.x + 0.25 * l.y;\n"
    "    float sh = uShine.w * (1.0 - smoothstep(0.0, uShine.z, abs(s - uShine.x)));\n"
    "    t.rgb *= vC.rgb;\n"
    "    t.rgb += vec3(1.0, 0.97, 0.82) * sh * t.a;\n"
    "    float f = 1.0 - smoothstep(uVFade.x, uVFade.y, l.y);\n"
    "    gl_FragColor = t * (vC.a * f);\n"
    "}\n";

// soft discs (bokeh) and blades: vT.xy in -1..1, vT.z = hardness (disc) or edge exponent (blade)
static const char kSoftFs[] =
    "#version 120\n"
    "uniform float uShape;\n"
    "varying vec3 vT;\n"
    "varying vec4 vC;\n"
    "void main() {\n"
    "    float a;\n"
    "    if (uShape < 0.5) {\n"
    "        float r = length(vT.xy);\n"
    "        a = 1.0 - smoothstep(mix(0.0, 0.72, vT.z), 1.0, r);\n"
    "    } else {\n"
    "        a = pow(clamp(1.0 - abs(vT.x), 0.0, 1.0), vT.z);\n"
    "    }\n"
    "    a *= vC.a;\n"
    "    gl_FragColor = vec4(vC.rgb * a, a);\n"
    "}\n";

static const char kSceneVs[] =
    "#version 120\n"
    "varying vec2 vUv;\n"
    "void main() {\n"
    "    vUv = vec2(gl_Vertex.x / 960.0, 1.0 - gl_Vertex.y / 544.0);\n"
    "    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
    "}\n";

// Sky, sun, rays, two cloud layers, island silhouettes and sea, all analytic in uT. uMode 1 draws only the rays (added later in front).
static const char kSceneFs[] =
    "#version 120\n"
    "uniform float uT;\n"
    "uniform vec2 uCam;\n"
    "uniform float uAsp;\n"
    "uniform vec2 uSun;\n"
    "uniform float uZoom;\n"
    "uniform float uMode;\n"
    "varying vec2 vUv;\n"
    "float h21(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }\n"
    "float vn(vec2 p) {\n"
    "    vec2 i = floor(p); vec2 f = fract(p); f = f * f * (3.0 - 2.0 * f);\n"
    "    return mix(mix(h21(i), h21(i + vec2(1.0, 0.0)), f.x), mix(h21(i + vec2(0.0, 1.0)), h21(i + vec2(1.0, 1.0)), f.x), f.y);\n"
    "}\n"
    "float fbm(vec2 p) {\n"
    "    float a = 0.5; float s = 0.0;\n"
    "    for (int i = 0; i < 5; i++) { s += a * vn(p); p = p * 2.03 + vec2(17.1, 9.7); a *= 0.5; }\n"
    "    return s;\n"
    "}\n"
    "const float HOR = 0.40;\n"
    "vec3 skyAt(vec2 c) {\n"
    "    float sy = clamp((c.y - HOR) / (1.0 - HOR), 0.0, 1.0);\n"
    "    vec3 zen = vec3(0.06, 0.12, 0.30);\n"
    "    vec3 mid = vec3(0.40, 0.34, 0.54);\n"
    "    vec3 low = vec3(1.00, 0.62, 0.30);\n"
    "    vec3 hz = vec3(1.00, 0.80, 0.50);\n"
    "    vec3 s = mix(hz, low, smoothstep(0.0, 0.14, sy));\n"
    "    s = mix(s, mid, smoothstep(0.06, 0.46, sy));\n"
    "    return mix(s, zen, smoothstep(0.36, 1.0, sy));\n"
    "}\n"
    "float rays(vec2 c, float t) {\n"
    "    vec2 d = (c - uSun) * vec2(uAsp, 1.0);\n"
    "    float r = length(d);\n"
    "    float ang = atan(d.y, d.x);\n"
    "    float a1 = vn(vec2(ang * 6.0 + t * 0.045, 3.0));\n"
    "    float a2 = vn(vec2(ang * 15.0 - t * 0.035, 9.0));\n"
    "    float a3 = vn(vec2(ang * 33.0 + t * 0.06, 21.0));\n"
    "    float ray = pow(a1 * 0.5 + a2 * 0.32 + a3 * 0.18, 3.2);\n"
    "    float breath = 0.65 + 0.35 * sin(t * 0.55 + ang * 3.0 + a1 * 6.0);\n"
    "    return ray * breath * exp(-r * 1.8) * smoothstep(0.02, 0.2, r);\n"
    "}\n"
    "float cloudD(vec2 uv, float scale, float speed, float yoff) {\n"
    "    vec2 p = vec2(uv.x * scale * uAsp * 0.55 + uT * speed, (uv.y - yoff) * scale * 2.4);\n"
    "    return fbm(p);\n"
    "}\n"
    "vec4 cloudLayer(vec2 uv, float scale, float speed, float yoff, float cover, float bandLo, float bandHi) {\n"
    "    float d = cloudD(uv, scale, speed, yoff);\n"
    "    float dl = cloudD(uv + (uSun - uv) * 0.07, scale, speed, yoff);\n"
    "    float band = smoothstep(HOR + bandLo, HOR + bandLo + 0.10, uv.y) * (1.0 - smoothstep(bandHi - 0.25, bandHi, uv.y));\n"
    "    float a = smoothstep(cover, cover + 0.24, d) * band;\n"
    "    float rim = clamp((d - dl) * 3.6 + 0.35, 0.0, 1.0);\n"
    "    float lowSun = 1.0 - smoothstep(0.0, 0.5, uv.y - HOR);\n"
    "    vec3 shade = mix(vec3(0.30, 0.22, 0.42), vec3(0.62, 0.38, 0.40), lowSun);\n"
    "    vec3 lit = mix(vec3(1.00, 0.70, 0.40), vec3(1.00, 0.86, 0.60), rim);\n"
    "    vec3 col = mix(shade, lit, rim * (0.55 + 0.45 * lowSun));\n"
    "    return vec4(col, a);\n"
    "}\n"
    "float island(float x, float cx, float w, float h, float seed) {\n"
    "    float k = 1.0 - pow(abs(x - cx) / w, 2.0);\n"
    "    return h * max(k, 0.0) * (0.70 + 0.55 * fbm(vec2(x * 11.0 + seed, seed)));\n"
    "}\n"
    "void main() {\n"
    "    vec2 uv = vUv;\n"
    "    vec2 c = (uv - 0.5) / uZoom + 0.5;\n"
    "    if (uMode > 0.5) {\n"
    "        float r = rays(c + uCam * 0.2, uT);\n"
    "        float fall = smoothstep(0.30, 0.95, c.y);\n"
    "        gl_FragColor = vec4(vec3(1.0, 0.78, 0.46) * r * (0.35 + 0.65 * fall) * 0.55, 1.0);\n"
    "        return;\n"
    "    }\n"
    "    vec2 cs = c + uCam * 0.1;\n"
    "    vec3 col;\n"
    "    vec2 sp = uSun;\n"
    "    vec2 dd = (cs - sp) * vec2(uAsp, 1.0);\n"
    "    float d = length(dd);\n"
    "    vec3 sky = skyAt(cs);\n"
    "    sky += vec3(1.0, 0.60, 0.26) * exp(-d * 4.2) * 0.60 + vec3(1.0, 0.84, 0.54) * exp(-d * 13.0) * 0.75;\n"
    "    sky += vec3(1.0, 0.95, 0.80) * (1.0 - smoothstep(0.040, 0.052, d)) * 1.5;\n"
    "    sky += vec3(1.0, 0.70, 0.38) * rays(cs, uT) * 0.55;\n"
    "    col = sky;\n"
    "    vec2 c1 = c + uCam * 0.3;\n"
    "    vec4 cl1 = cloudLayer(c1, 1.7, 0.010, 0.0, 0.50, 0.03, 0.95);\n"
    "    col = mix(col, cl1.rgb, cl1.a * 0.85);\n"
    "    vec2 c2 = c + uCam * 0.55;\n"
    "    vec4 cl2 = cloudLayer(c2, 3.4, 0.022, 0.07, 0.56, 0.0, 0.70);\n"
    "    col = mix(col, cl2.rgb * 0.92, cl2.a * 0.9);\n"
    "    vec3 hz = vec3(1.0, 0.80, 0.50);\n"
    "    vec3 farC = mix(hz, vec3(0.34, 0.30, 0.50), 0.72);\n"
    "    float x1 = c.x + uCam.x * 0.45;\n"
    "    float topFar = HOR + max(island(x1, 0.17, 0.16, 0.05, 3.0), island(x1, 0.41, 0.11, 0.03, 8.0));\n"
    "    float mFar = 1.0 - smoothstep(0.0, 0.004, c.y - topFar);\n"
    "    col = mix(col, farC, mFar * step(HOR - 0.002, c.y));\n"
    "    float x2 = c.x + uCam.x * 0.8;\n"
    "    float topNear = HOR + island(x2, 0.86, 0.26, 0.105, 14.0);\n"
    "    float mNear = 1.0 - smoothstep(0.0, 0.003, c.y - topNear);\n"
    "    vec3 nearC = mix(vec3(0.10, 0.12, 0.20), vec3(0.30, 0.22, 0.30), smoothstep(0.0, 0.10, topNear - c.y + 0.02));\n"
    "    nearC += vec3(1.0, 0.65, 0.35) * 0.35 * (1.0 - smoothstep(0.0, 0.012, topNear - c.y)) * smoothstep(0.6, 0.85, c.x);\n"
    "    col = mix(col, nearC, mNear * step(HOR - 0.002, c.y));\n"
    "    if (c.y < HOR) {\n"
    "        float dep = clamp((HOR - c.y) / HOR, 0.0, 1.0);\n"
    "        float persp = 1.0 / (dep * 1.7 + 0.07);\n"
    "        vec2 sw = vec2((c.x - 0.5) * uAsp * persp * 0.8 + uCam.x * 4.0, persp * 1.3 - uT * 0.20);\n"
    "        float w1 = vn(sw * vec2(2.2, 3.2));\n"
    "        float w2 = vn(sw * vec2(5.5, 8.5) + vec2(uT * 0.12, 0.0));\n"
    "        float wave = w1 * 0.6 + w2 * 0.4;\n"
    "        vec3 deep = vec3(0.02, 0.15, 0.24);\n"
    "        vec3 shallow = vec3(0.10, 0.40, 0.44);\n"
    "        vec3 water = mix(shallow, deep, smoothstep(0.1, 0.95, dep));\n"
    "        vec3 refl = mix(hz * 0.95, vec3(0.62, 0.40, 0.45), smoothstep(0.0, 0.5, dep));\n"
    "        vec3 sea = mix(refl, water, smoothstep(0.0, 0.42, dep));\n"
    "        sea += (wave - 0.5) * 0.14;\n"
    "        float gx = abs(c.x - sp.x) * uAsp;\n"
    "        float colMask = exp(-gx * gx * (30.0 / (0.30 + dep * 2.2))) * (1.0 - 0.55 * dep);\n"
    "        float spark = smoothstep(0.72, 0.93, vn(sw * vec2(13.0, 21.0) + vec2(uT * 0.9, uT * 0.4)));\n"
    "        float glit = smoothstep(0.62, 0.9, vn(sw * vec2(9.0, 15.0) - vec2(uT * 0.5, 0.0)));\n"
    "        sea += vec3(1.0, 0.72, 0.38) * colMask * (0.28 + 0.9 * spark + 0.4 * glit);\n"
    "        sea += vec3(1.0, 0.80, 0.52) * 0.10 * exp(-dep * 9.0);\n"
    "        float edge = smoothstep(HOR - 0.003, HOR, c.y);\n"
    "        col = mix(sea, col, edge);\n"
    "    }\n"
    "    col += (h21(gl_FragCoord.xy) - 0.5) / 255.0;\n"
    "    gl_FragColor = vec4(col, 1.0);\n"
    "}\n";

// ---------------------------------------------------------------------------------------- state
static bool sInit, sFailed, sEnvRead, sEnvOrig;
static DeckGlProgram sSprite, sSoft, sScene;
static GLint sSpTex, sSpBias, sSpRect, sSpShine, sSpVFade, sSoShape;
static GLint sScT, sScCam, sScAsp, sScSun, sScZoom, sScMode;
static GLuint sAtlas, sRayTex;
static bool sRayOk;
static int sPrevPhase;
static double sStart, sPhase3At = -1.0;
static unsigned sFrames;

static double Now(void) { return (double)SDL_GetTicks() / 1000.0; }

static GLuint MakeTexture(const uint8_t *rgba, int w, int h, GLint magFilter)
{
    GLuint t = 0;

    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glGenerateMipmap(GL_TEXTURE_2D);
    return t;
}

static bool BuildAtlas(void)
{
    uint8_t *a = (uint8_t *)calloc((size_t)ATLAS_W * ATLAS_H * 4, 1);
    bool ok;

    if (a == NULL)
        return false;
    ok = PutText(a, 0, 0, "BRAVELY EMERALD", 8, 0, 18, &kStyleBig, &sRectBig)
      && PutText(a, 0, 100, "POKEMON", 5, 9, 16, &kStylePoke, &sRectPoke)
      && PutRibbon(a, 330, 100, &sRectRibbon)
      && PutText(a, 0, 196, "PRESS START", 4, 6, 14, &kStylePress, &sRectPress)
      && PutText(a, 0, 260, "(C) 2005 POKEMON  (C) 1995-2005 NINTENDO", 2, 1, 8, &kStyleSmall, &sRectCopy1)
      && PutText(a, 0, 292, "CREATURES INC.  GAME FREAK INC.", 2, 1, 8, &kStyleSmall, &sRectCopy2);
    if (ok)
        sAtlas = MakeTexture(a, ATLAS_W, ATLAS_H, GL_LINEAR);
    free(a);
    return ok && sAtlas != 0;
}

// Rayquaza: the original BG0 layer (4bpp tiles at charbase 2, 32x32 map at screenbase 26, palette banks of the title) read
// from emulated VRAM once the title has loaded it, index 0 clear. Returns false when the data does not look loaded.
static bool GrabRayquaza(void)
{
    const uint8_t *chars = (const uint8_t *)(uintptr_t)BG_CHAR_ADDR(2);
    const uint16_t *map = (const uint16_t *)(uintptr_t)BG_SCREEN_ADDR(26);
    uint8_t *px = (uint8_t *)calloc(256 * 256 * 4, 1);
    int solid = 0;

    if (px == NULL)
        return false;
    for (int ty = 0; ty < 32; ty++)
        for (int tx = 0; tx < 32; tx++)
        {
            const uint16_t e = map[ty * 32 + tx];
            const int tile = e & 0x3FF, hf = (e >> 10) & 1, vf = (e >> 11) & 1, bank = (e >> 12) & 15;

            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++)
                {
                    const int sx = hf ? 7 - x : x, sy = vf ? 7 - y : y;
                    const uint8_t b = chars[tile * 32 + sy * 4 + sx / 2];
                    const int idx = (sx & 1) ? (b >> 4) : (b & 15);
                    uint8_t *o = &px[((ty * 8 + y) * 256 + tx * 8 + x) * 4];

                    if (idx == 0)
                        continue;
                    {
                        const uint16_t c = gPlttBufferUnfaded[bank * 16 + idx];
                        const int r = c & 31, g = (c >> 5) & 31, bl = (c >> 10) & 31;

                        o[0] = (uint8_t)(r * 255 / 31);
                        o[1] = (uint8_t)(g * 255 / 31);
                        o[2] = (uint8_t)(bl * 255 / 31);
                        o[3] = 255;
                        solid++;
                    }
                }
        }
    if (solid < 200)
    {
        free(px);
        return false;
    }
    if (sRayTex != 0)
        glDeleteTextures(1, &sRayTex);
    sRayTex = MakeTexture(px, 256, 256, GL_LINEAR);
    free(px);
    DeckGl_Log("[TITLE] Rayquaza layer grabbed from VRAM: %d solid texels\n", solid);
    return sRayTex != 0;
}

static bool Build(void)
{
    if (!DeckGl_Init())
        return false;
    if (!DeckGl_BuildProgram(&sSprite, "title_sprite", kSpriteVs, kSpriteFs)
        || !DeckGl_BuildProgram(&sSoft, "title_soft", kSpriteVs, kSoftFs)
        || !DeckGl_BuildProgram(&sScene, "title_scene", kSceneVs, kSceneFs))
        return false;
    sSpTex = DeckGl_Uniform(&sSprite, "uTex");
    sSpBias = DeckGl_Uniform(&sSprite, "uBias");
    sSpRect = DeckGl_Uniform(&sSprite, "uRect");
    sSpShine = DeckGl_Uniform(&sSprite, "uShine");
    sSpVFade = DeckGl_Uniform(&sSprite, "uVFade");
    sSoShape = DeckGl_Uniform(&sSoft, "uShape");
    sScT = DeckGl_Uniform(&sScene, "uT");
    sScCam = DeckGl_Uniform(&sScene, "uCam");
    sScAsp = DeckGl_Uniform(&sScene, "uAsp");
    sScSun = DeckGl_Uniform(&sScene, "uSun");
    sScZoom = DeckGl_Uniform(&sScene, "uZoom");
    sScMode = DeckGl_Uniform(&sScene, "uMode");
    if (!BuildAtlas())
    {
        DeckGl_Log("[TITLE] logo atlas failed\n");
        return false;
    }
    return true;
}

static void Fail(const char *why)
{
    DeckGl_Log("[TITLE] %s: the original title screen is drawn (fallback)\n", why);
    sFailed = true;
}

bool DeckTitle_Wanted(void)
{
    const int phase = gDeckTitlePhase;

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
    if (!sInit)
    {
        sInit = true;
        if (!Build())
        {
            Fail("build failed");
            return false;
        }
        DeckGl_Log("[TITLE] ready (BRAVELY EMERALD 2D-HD)\n");
    }
    if (sPrevPhase == 0)
    {
        // the title just started: restart the clock, take Rayquaza from VRAM (the state machine loaded it before phase 1)
        sStart = Now();
        sFrames = 0;
        sRayOk = GrabRayquaza();
        if (!sRayOk)
            DeckGl_Log("[TITLE] no Rayquaza layer in VRAM: scene without it\n");
    }
    if (phase == 3 && sPhase3At < 0.0)
        sPhase3At = Now();
    sPrevPhase = phase;
    return true;
}

// ------------------------------------------------------------------------------- drawing helpers
static void Quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, float r, float g, float b, float a)
{
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glTexCoord3f(u0, v0, 0.0f); glVertex2f(x0, y0);
    glTexCoord3f(u1, v0, 0.0f); glVertex2f(x1, y0);
    glTexCoord3f(u1, v1, 0.0f); glVertex2f(x1, y1);
    glTexCoord3f(u0, v1, 0.0f); glVertex2f(x0, y1);
    glEnd();
}

// An atlas rect drawn centred at (cx, cy) scaled by s, texture texels 1:1 with logical pixels at s = 1.
static void DrawAtlasRect(const Rect *r, float cx, float cy, float s, float rr, float gg, float bb, float a)
{
    const float hw = (float)r->w * 0.5f * s, hh = (float)r->h * 0.5f * s;
    const float u0 = (float)r->x / ATLAS_W, v0 = (float)r->y / ATLAS_H, u1 = (float)(r->x + r->w) / ATLAS_W, v1 = (float)(r->y + r->h) / ATLAS_H;

    DeckGl_Set4f(sSpRect, u0, v0, u1, v1);
    Quad(cx - hw, cy - hh, cx + hw, cy + hh, u0, v0, u1, v1, rr, gg, bb, a);
}

static float Hash(unsigned n)
{
    n = (n << 13) ^ n;
    return (float)(((n * (n * n * 15731u + 789221u) + 1376312589u) & 0x7fffffffu)) / 2147483648.0f;
}

// soft disc (hardness 0..1): additive when add is set, else premultiplied alpha
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

typedef struct { float x, y, h, w, ph, tone; } Blade;
#define N_BLADES 84
static Blade sBlades[N_BLADES];
static bool sBladesInit;

static void InitBlades(void)
{
    for (int i = 0; i < N_BLADES; i++)
    {
        const bool left = i < N_BLADES / 2;
        const float k = Hash((unsigned)i * 7u + 1u);

        sBlades[i].x = left ? -30.0f + k * 330.0f : 650.0f + k * 340.0f;
        sBlades[i].y = SH + 8.0f;
        sBlades[i].h = 70.0f + Hash((unsigned)i * 13u + 5u) * 150.0f;
        sBlades[i].w = 7.0f + Hash((unsigned)i * 17u + 9u) * 12.0f;
        sBlades[i].ph = Hash((unsigned)i * 23u + 3u) * 6.2831f;
        sBlades[i].tone = Hash((unsigned)i * 29u + 11u);
    }
    sBladesInit = true;
}

static void DrawBlades(float t, float camX)
{
    glColor4f(1, 1, 1, 1);
    glBegin(GL_TRIANGLES);
    for (int i = 0; i < N_BLADES; i++)
    {
        const Blade *b = &sBlades[i];
        const float gust = sinf(t * 0.45f + b->x * 0.004f) * 0.6f + sinf(t * 0.9f + b->ph) * 0.4f;
        const float tip = (gust * 0.22f + sinf(t * 1.7f + b->ph * 1.3f) * 0.05f) * b->h;
        const float x = b->x - camX * 26.0f;
        const float lightK = 0.10f + 0.50f * b->tone;
        const float r = 0.015f + 0.20f * lightK, g = 0.05f + 0.34f * lightK, bl = 0.03f + 0.10f * lightK;

        // base dark, tip warmer (sun from the right)
        glColor4f(0.012f, 0.045f, 0.035f, 0.95f);
        glTexCoord3f(-1.0f, 0.0f, 0.55f); glVertex2f(x - b->w, b->y);
        glTexCoord3f(1.0f, 0.0f, 0.55f); glVertex2f(x + b->w, b->y);
        glColor4f(r, g, bl, 0.9f);
        glTexCoord3f(0.0f, 0.0f, 0.55f); glVertex2f(x + tip, b->y - b->h);
    }
    glEnd();
}

typedef struct { float x, y, sp, amp, ph, size, depth; } Mote;
#define N_MOTES 70
#define N_FLY 22
static Mote sMotes[N_MOTES], sFly[N_FLY];
static bool sMotesInit;

static void InitMotes(void)
{
    for (int i = 0; i < N_MOTES; i++)
    {
        Mote *m = &sMotes[i];

        m->x = Hash((unsigned)i * 3u + 100u) * SW;
        m->y = 90.0f + Hash((unsigned)i * 5u + 101u) * 400.0f;
        m->sp = 5.0f + Hash((unsigned)i * 7u + 102u) * 14.0f;
        m->amp = 8.0f + Hash((unsigned)i * 11u + 103u) * 26.0f;
        m->ph = Hash((unsigned)i * 13u + 104u) * 6.2831f;
        m->depth = Hash((unsigned)i * 17u + 105u);
        m->size = m->depth < 0.7f ? 1.2f + 1.6f * Hash((unsigned)i * 19u + 106u) : 9.0f + 14.0f * Hash((unsigned)i * 19u + 106u);
    }
    for (int i = 0; i < N_FLY; i++)
    {
        Mote *m = &sFly[i];

        m->x = 60.0f + Hash((unsigned)i * 3u + 300u) * 840.0f;
        m->y = 250.0f + Hash((unsigned)i * 5u + 301u) * 250.0f;
        m->sp = 0.25f + Hash((unsigned)i * 7u + 302u) * 0.5f;
        m->amp = 20.0f + Hash((unsigned)i * 11u + 303u) * 40.0f;
        m->ph = Hash((unsigned)i * 13u + 304u) * 6.2831f;
        m->size = 4.0f + 5.0f * Hash((unsigned)i * 19u + 305u);
        m->depth = Hash((unsigned)i * 17u + 306u);
    }
    sMotesInit = true;
}

static float EdgeFade(float x, float lo, float hi, float ramp)
{
    return Smooth(lo, lo + ramp, x) * (1.0f - Smooth(hi - ramp, hi, x));
}

void DeckTitle_Draw(void)
{
    const double now = Now();
    const float t = (float)(now - sStart);
    const int phase = gDeckTitlePhase;
    const float camX = sinf(t * 0.10f) * 0.9f + sinf(t * 0.043f + 1.7f) * 0.5f;
    const float camY = sinf(t * 0.07f + 1.0f) * 0.7f;
    const float zoom = 1.0f + 0.018f * sinf(t * 0.055f) + 0.012f;
    const float asp = SW / SH;
    const float intro = Smooth(0.0f, 1.8f, t);
    const float p3 = sPhase3At >= 0.0 ? Smooth(0.0f, 1.6f, (float)(now - sPhase3At)) : 0.0f;
    const int fadeY = gPaletteFade.y;
    float fade = (float)fadeY / 16.0f;

    if (!sMotesInit)
        InitMotes();
    if (!sBladesInit)
        InitBlades();
    if (++sFrames == 2 || sFrames % 600 == 0)
        DeckGl_Log("[TITLE] frame %u phase %d t=%.1f fade y=%d ray=%d\n", sFrames, phase, (double)t, fadeY, (int)sRayOk);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    DeckGl_ActiveTexture(GL_TEXTURE0);

    // ---- 1. sky, sun, clouds, islands, sea
    DeckGl_UseProgram(&sScene);
    DeckGl_Set1f(sScT, t);
    DeckGl_Set2f(sScCam, camX * 0.012f, camY * 0.006f);
    DeckGl_Set1f(sScAsp, asp);
    DeckGl_Set2f(sScSun, 0.665f, 0.455f);
    DeckGl_Set1f(sScZoom, zoom);
    DeckGl_Set1f(sScMode, 0.0f);
    Quad(0.0f, 0.0f, SW, SH, 0, 0, 1, 1, 1, 1, 1, 1);

    // ---- 2. far motes (dust specks in the sky and over the sea), behind Rayquaza
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    DeckGl_UseProgram(&sSoft);
    DeckGl_Set1f(sSoShape, 0.0f);
    for (int i = 0; i < N_MOTES; i++)
    {
        const Mote *m = &sMotes[i];

        if (m->depth >= 0.5f)
            continue;
        {
            const float x = fmodf(m->x + t * m->sp - camX * 12.0f * (0.4f + m->depth) + 2.0f * SW, SW + 80.0f) - 40.0f;
            const float y = m->y + sinf(t * 0.5f + m->ph) * m->amp + sinf(t * 0.23f + m->ph * 2.1f) * m->amp * 0.6f;
            const float tw = 0.55f + 0.45f * sinf(t * 1.3f + m->ph * 3.0f);
            const float a = EdgeFade(x, -40.0f, SW + 40.0f, 80.0f) * 0.55f * tw * intro;

            Disc(x, y, m->size * 1.8f, 1.0f, 0.85f, 0.55f, a, 0.25f);
        }
    }

    // ---- 3. Rayquaza (original art), floating, soft, fading into the sea haze
    if (sRayOk && p3 > 0.0f)
    {
        const float cx = 500.0f - camX * 20.0f, cy = 292.0f + sinf(t * 0.42f) * 8.0f - camY * 6.0f;
        const float sc = 760.0f * (1.0f + 0.01f * sinf(t * 0.31f));

        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        DeckGl_UseProgram(&sSprite);
        glBindTexture(GL_TEXTURE_2D, sRayTex);
        DeckGl_Set1i(sSpTex, 0);
        DeckGl_Set1f(sSpBias, 0.7f);
        DeckGl_Set4f(sSpRect, 0.0f, 0.0f, 1.0f, 1.0f);
        DeckGl_Set4f(sSpShine, 0.0f, 0.0f, 1.0f, 0.0f);
        DeckGl_Set2f(sSpVFade, 0.62f, 0.92f);
        Quad(cx - sc * 0.5f, cy - sc * 0.5f, cx + sc * 0.5f, cy + sc * 0.5f, 0, 0, 1, 1, 1.0f, 0.95f, 0.9f, 0.92f * p3);
        DeckGl_Set2f(sSpVFade, 1.0f, 1.0f);
    }

    // ---- 4. sun rays in front of the far layers
    glBlendFunc(GL_ONE, GL_ONE);
    DeckGl_UseProgram(&sScene);
    DeckGl_Set1f(sScMode, 1.0f);
    Quad(0.0f, 0.0f, SW, SH, 0, 0, 1, 1, 1, 1, 1, 1);

    // ---- 5. soft dark glow behind the logo for legibility, then the logo
    {
        const float lx = 480.0f - camX * 5.0f, slide = (1.0f - intro) * 14.0f;
        const float s = (1.0f + 0.10f * (1.0f - intro)) * (1.0f + 0.006f * sinf(t * 0.8f));
        const float shine = fmodf(t, 6.0f) / 1.5f;   // sweep (0..1) once every 6 s, 1.5 s long
        const float sweepPos = shine * 1.5f - 0.25f;

        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        DeckGl_UseProgram(&sSoft);
        DeckGl_Set1f(sSoShape, 0.0f);
        Disc(lx, 170.0f, 430.0f, 0.01f, 0.05f, 0.05f, 0.40f * intro, 0.0f);
        DeckGl_UseProgram(&sSprite);
        glBindTexture(GL_TEXTURE_2D, sAtlas);
        DeckGl_Set1i(sSpTex, 0);
        DeckGl_Set1f(sSpBias, 0.0f);
        DeckGl_Set2f(sSpVFade, 1.0f, 1.0f);
        DeckGl_Set4f(sSpShine, 0.0f, 0.0f, 1.0f, 0.0f);
        DrawAtlasRect(&sRectPoke, lx, 92.0f + slide, s, 1.0f, 1.0f, 1.0f, intro);
        DeckGl_Set4f(sSpShine, sweepPos, 0.16f, 0.16f, shine < 1.0f ? 0.9f : 0.0f);
        DrawAtlasRect(&sRectBig, lx, 164.0f + slide, s, 1.0f, 1.0f, 1.0f, intro);
        {
            const float rib = Smooth(1.6f, 2.6f, t);

            DeckGl_Set4f(sSpShine, 0.0f, 0.0f, 1.0f, 0.0f);
            DrawAtlasRect(&sRectRibbon, lx, 240.0f + (1.0f - rib) * 10.0f, 1.0f, 1.0f, 1.0f, 1.0f, rib);
        }
    }

    // ---- 6. a green glow around the logo (pulses slowly), added
    glBlendFunc(GL_ONE, GL_ONE);
    DeckGl_UseProgram(&sSoft);
    DeckGl_Set1f(sSoShape, 0.0f);
    Disc(480.0f, 164.0f, 380.0f, 0.05f, 0.55f, 0.30f, (0.16f + 0.05f * sinf(t * 1.1f)) * intro, 0.0f);

    // ---- 7. fireflies (warm green-gold, pulsing) and near bokeh discs
    for (int i = 0; i < N_FLY; i++)
    {
        const Mote *m = &sFly[i];
        const float x = m->x + sinf(t * m->sp + m->ph) * m->amp + sinf(t * m->sp * 0.37f + m->ph * 2.3f) * m->amp * 0.7f - camX * 22.0f * (0.5f + m->depth);
        const float y = m->y + sinf(t * m->sp * 0.8f + m->ph * 1.7f) * m->amp * 0.6f + sinf(t * 0.31f + m->ph) * 12.0f;
        const float pulse = powf(0.5f + 0.5f * sinf(t * (0.9f + m->sp) + m->ph * 5.0f), 2.0f);
        const float a = (0.15f + 0.85f * pulse) * intro;

        Disc(x, y, m->size * 3.2f, 0.55f, 0.95f, 0.35f, a * 0.35f, 0.0f);
        Disc(x, y, m->size * 0.9f, 1.0f, 1.0f, 0.65f, a, 0.5f);
    }
    for (int i = 0; i < N_MOTES; i++)
    {
        const Mote *m = &sMotes[i];

        if (m->depth < 0.5f)
            continue;
        {
            const float parallax = 0.5f + 1.5f * m->depth;
            const float x = fmodf(m->x + t * m->sp * 0.7f - camX * 26.0f * parallax + 2.0f * SW, SW + 120.0f) - 60.0f;
            const float y = m->y + sinf(t * 0.4f + m->ph) * m->amp;
            const float tw = 0.6f + 0.4f * sinf(t * 0.9f + m->ph * 3.0f);

            if (m->depth >= 0.7f)   // near bokeh: big, faint, out of focus
                Disc(x, y, m->size, 1.0f, 0.80f, 0.50f, 0.075f * tw * intro * EdgeFade(x, -60.0f, SW + 60.0f, 120.0f), 0.8f);
            else
                Disc(x, y, m->size * 2.0f, 1.0f, 0.90f, 0.62f, 0.50f * tw * intro * EdgeFade(x, -40.0f, SW + 40.0f, 80.0f), 0.45f);
        }
    }

    // ---- 8. PRESS START and the copyright line
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    DeckGl_UseProgram(&sSprite);
    glBindTexture(GL_TEXTURE_2D, sAtlas);
    DeckGl_Set1i(sSpTex, 0);
    DeckGl_Set1f(sSpBias, 0.0f);
    DeckGl_Set4f(sSpShine, 0.0f, 0.0f, 1.0f, 0.0f);
    DeckGl_Set2f(sSpVFade, 1.0f, 1.0f);
    if (phase >= 3)
    {
        const float pulse = 0.62f + 0.38f * sinf(t * 3.1f);
        const float sc = 1.0f + 0.03f * sinf(t * 3.1f);

        DrawAtlasRect(&sRectPress, 480.0f, 432.0f, sc, 1.0f, 1.0f, 1.0f, p3 * pulse);
        DrawAtlasRect(&sRectCopy1, 480.0f, 496.0f, 1.0f, 1.0f, 1.0f, 1.0f, p3 * 0.85f);
        DrawAtlasRect(&sRectCopy2, 480.0f, 516.0f, 1.0f, 1.0f, 1.0f, 1.0f, p3 * 0.85f);
    }

    // ---- 9. foreground: out-of-focus grass blades (soft edges), a darker foot, then the fade
    DeckGl_UseProgram(&sSoft);
    DeckGl_Set1f(sSoShape, 1.0f);
    DrawBlades(t, camX);
    DeckGl_Set1f(sSoShape, 0.0f);

    // bottom shade: the sea fades into the dark, and a very soft vignette is drawn here so it also holds with the post pass off
    glDisable(GL_TEXTURE_2D);
    DeckGl_UseProgram(NULL);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBegin(GL_QUADS);
    glColor4f(0.0f, 0.02f, 0.03f, 0.0f); glVertex2f(0.0f, SH - 150.0f);
    glColor4f(0.0f, 0.02f, 0.03f, 0.0f); glVertex2f(SW, SH - 150.0f);
    glColor4f(0.0f, 0.02f, 0.03f, 0.55f); glVertex2f(SW, SH);
    glColor4f(0.0f, 0.02f, 0.03f, 0.55f); glVertex2f(0.0f, SH);
    glEnd();

    // the game's own palette fade (white in, white or black out): same coefficient and colour as the GBA picture
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

    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBindTexture(GL_TEXTURE_2D, 0);
}
