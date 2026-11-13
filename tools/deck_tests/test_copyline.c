#include <stdint.h>
#include <stdio.h>
#include <emmintrin.h>

static uint16_t ref(uint16_t c)
{
    uint16_t r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    uint16_t g6 = (g << 1) | (g >> 4);
    return (uint16_t)((r << 11) | (g6 << 5) | b);
}

static void convert8(const uint16_t *src, uint16_t *dst)
{
    const __m128i mask5 = _mm_set1_epi16(0x1F);
    __m128i c  = _mm_loadu_si128((const __m128i *)src);
    __m128i r  = _mm_and_si128(c, mask5);
    __m128i g  = _mm_and_si128(_mm_srli_epi16(c, 5), mask5);
    __m128i b  = _mm_and_si128(_mm_srli_epi16(c, 10), mask5);
    __m128i g6 = _mm_or_si128(_mm_slli_epi16(g, 1), _mm_srli_epi16(g, 4));
    __m128i out = _mm_or_si128(_mm_or_si128(_mm_slli_epi16(r, 11), _mm_slli_epi16(g6, 5)), b);
    _mm_storeu_si128((__m128i *)dst, out);
}

int main(void)
{
    uint16_t in[8], out[8];
    for (uint32_t base = 0; base < 0x8000; base += 8)
    {
        for (int i = 0; i < 8; i++) in[i] = (uint16_t)(base + i);
        convert8(in, out);
        for (int i = 0; i < 8; i++)
            if (out[i] != ref(in[i])) { printf("FAIL %04x: %04x != %04x\n", in[i], out[i], ref(in[i])); return 1; }
    }
    puts("PASS all 32768 colours");
    return 0;
}
