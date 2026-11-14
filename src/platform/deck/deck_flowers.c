#include "deck_flowers.h"

// 8x12 flower: head of five-by-four petals round a centre, a stem with two leaves. P petal, C centre, S stem, L leaf.
static const char *const kArt[DECK_FLOWER_H] = {
    "..PPP...",
    ".PPCPP..",
    ".PPPPP..",
    "..PPP...",
    "...S....",
    "..LS....",
    "..LSL...",
    "...SL...",
    "...S....",
    "...S....",
    "...S....",
    "..SSS...",
};

static uint16_t Rgb(uint32_t rgb)
{
    return (uint16_t)((((rgb >> 19) & 31u) << 11) | (((rgb >> 11) & 31u) << 6) | (((rgb >> 3) & 31u) << 1) | 1u);
}

void DeckFlowers_Sprites(uint16_t *out)
{
    // petal, centre: red, yellow, pink, white
    static const uint32_t kPetal[DECK_FLOWER_VARIANTS] = { 0xD83830, 0xF0D038, 0xF080B0, 0xF8F8F0 };
    static const uint32_t kCentre[DECK_FLOWER_VARIANTS] = { 0xF8E040, 0x906020, 0xF8E040, 0xF0C030 };
    const uint32_t stem = 0x308838, leaf = 0x58B048;
    const int width = DECK_FLOWER_W * DECK_FLOWER_VARIANTS;

    for (int v = 0; v < DECK_FLOWER_VARIANTS; ++v)
        for (int y = 0; y < DECK_FLOWER_H; ++y)
            for (int x = 0; x < DECK_FLOWER_W; ++x)
            {
                uint16_t texel = 0; // clear
                switch (kArt[y][x])
                {
                case 'P': texel = Rgb(kPetal[v]); break;
                case 'C': texel = Rgb(kCentre[v]); break;
                case 'S': texel = Rgb(stem); break;
                case 'L': texel = Rgb(leaf); break;
                default: break;
                }
                out[y * width + v * DECK_FLOWER_W + x] = texel;
            }
}
