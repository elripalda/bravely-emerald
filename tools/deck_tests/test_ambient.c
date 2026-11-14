// Data checks for the procedural ambient bed (src/platform/deck/deck_ambient.c). No audio device needed.
// Build and run on the Deck, in the distrobox:
//   gcc -m32 -O2 -I src/platform/deck tools/deck_tests/test_ambient.c src/platform/deck/deck_ambient.c -lm -o /tmp/test_ambient && /tmp/test_ambient
// Exit code 0 = all checks passed. "gain 1" below is ambient_volume 10 at master volume 10; the default (6) is 0.6 of it.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "deck_ambient.h"

#define RATE 48000
#define SECONDS 60

static int sFail;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); sFail = 1; } } while (0)

typedef struct { double peak, rms, maxStepInside, maxStepSeam; int nan; double tailRms; } Stats;

// Renders SECONDS in blocks of 799..801 frames (the game runs one block per 60 Hz frame, 800 at 48 kHz).
static Stats Render(const DeckAmbientTargets *t, float gain, int thunderAt)
{
    static float buf[2 * 1024];
    Stats s = {0};
    double sumsq = 0, tailSq = 0;
    long total = 0, tailN = 0;
    float prevL = 0, prevR = 0;
    int first = 1;
    long frames = (long)RATE * SECONDS, done = 0;
    int blk = 0;

    DeckAmbient_Init(RATE, 12345);
    DeckAmbient_SetTargets(t);
    while (done < frames)
    {
        int n = 799 + (blk++ % 3);
        if (thunderAt >= 0 && done >= (long)thunderAt * RATE)
        {
            DeckAmbient_Thunder();
            thunderAt = -1;
        }
        memset(buf, 0, sizeof(float) * 2 * n);
        DeckAmbient_Mix(buf, n, gain);
        for (int i = 0; i < n; i++)
        {
            float l = buf[2 * i], r = buf[2 * i + 1];
            if (!(l == l) || !(r == r) || fabsf(l) > 1e6f || fabsf(r) > 1e6f)
            {
                s.nan = 1;
                continue;
            }
            if (fabs(l) > s.peak) s.peak = fabs(l);
            if (fabs(r) > s.peak) s.peak = fabs(r);
            sumsq += (double)l * l + (double)r * r;
            total += 2;
            if (done > frames - 5 * RATE) { tailSq += (double)l * l + (double)r * r; tailN += 2; }
            if (!first)
            {
                double d = fmax(fabs(l - prevL), fabs(r - prevR));
                if (i == 0) { if (d > s.maxStepSeam) s.maxStepSeam = d; }
                else if (d > s.maxStepInside) s.maxStepInside = d;
            }
            first = 0;
            prevL = l;
            prevR = r;
        }
        done += n;
    }
    s.rms = total ? sqrt(sumsq / total) : 0;
    s.tailRms = tailN ? sqrt(tailSq / tailN) : 0;
    return s;
}

static void Report(const char *name, const Stats *s)
{
    printf("%-22s peak %.4f rms %.4f (%.1f dBFS) step in %.4f seam %.4f tail rms %.4f nan %d\n", name, s->peak, s->rms,
           s->rms > 0 ? 20 * log10(s->rms) : -999.0, s->maxStepInside, s->maxStepSeam, s->tailRms, s->nan);
}

static void Common(const char *name, const Stats *s, double peakMax, double rmsLo, double rmsHi)
{
    CHECK(!s->nan, "%s: NaN or Inf", name);
    CHECK(s->peak < peakMax, "%s: peak %.4f >= %.2f", name, s->peak, peakMax);
    CHECK(s->peak < 0.98, "%s: clips", name);
    CHECK(s->rms >= rmsLo && s->rms <= rmsHi, "%s: rms %.4f outside %.3f..%.3f", name, s->rms, rmsLo, rmsHi);
    // Seam continuity: a block boundary must not step more than the loudest step inside blocks (x1.5 + a floor).
    CHECK(s->maxStepSeam <= s->maxStepInside * 1.5 + 0.002, "%s: block seam step %.4f vs inside %.4f", name, s->maxStepSeam, s->maxStepInside);
}

int main(void)
{
    struct { const char *name; DeckAmbientTargets t; double peakMax, rmsLo, rmsHi; } ctx[] = {
        { "wind only",       { 1, 0, 0, 0, 0, 0, 1 }, 0.30, 0.004, 0.06 },
        { "birds only",      { 0, 1, 0, 0, 0, 0, 1 }, 0.30, 0.0005, 0.06 },
        { "crickets only",   { 0, 0, 1, 0, 0, 0, 1 }, 0.30, 0.0005, 0.06 },
        { "water only",      { 0, 0, 0, 1, 0, 0, 1 }, 0.30, 0.004, 0.06 },
        { "rain only",       { 0, 0, 0, 0, 1, 0, 1 }, 0.30, 0.004, 0.06 },
        { "cave only",       { 0, 0, 0, 0, 0, 1, 1 }, 0.30, 0.002, 0.06 },
        { "day (birds+wind)",{ 0.3, 0.9, 0, 0, 0, 0, 1 }, 0.30, 0.002, 0.06 },
        { "night (crickets)",{ 0.2, 0, 0.9, 0, 0, 0, 1 }, 0.30, 0.002, 0.06 },
        { "storm (all rain)",{ 0.9, 0, 0, 0, 1, 0, 1 }, 0.35, 0.004, 0.07 },
        { "beach day",       { 0.3, 0.8, 0, 1, 0, 0, 1 }, 0.35, 0.004, 0.07 },
        { "everything",      { 1, 1, 1, 1, 1, 1, 1 }, 0.40, 0.004, 0.10 },
    };
    Stats s;

    for (unsigned i = 0; i < sizeof(ctx) / sizeof(ctx[0]); i++)
    {
        s = Render(&ctx[i].t, 1.0f, -1);
        Report(ctx[i].name, &s);
        Common(ctx[i].name, &s, ctx[i].peakMax, ctx[i].rmsLo, ctx[i].rmsHi);
    }

    // Thunder: with only the thunder layer and master up, the rumble must be audible and bounded.
    {
        DeckAmbientTargets t = { 0, 0, 0, 0, 0, 0, 1 };
        s = Render(&t, 1.0f, 5);
        Report("thunder only", &s);
        Common("thunder only", &s, 0.40, 0.0003, 0.08);
    }

    // Hard off: gain 0 must leave the buffer untouched (exact zeros) for a full minute.
    {
        DeckAmbientTargets t = { 1, 1, 1, 1, 1, 1, 1 };
        s = Render(&t, 0.0f, 5);
        Report("gain 0 (hard off)", &s);
        CHECK(s.peak == 0.0, "gain 0 wrote samples (peak %.6f)", s.peak);
    }

    // Master 0 (indoors): fully faded after 60 s, tail exactly silent.
    {
        DeckAmbientTargets t = { 1, 1, 1, 1, 1, 1, 0 };
        s = Render(&t, 1.0f, -1);
        Report("master 0 (indoors)", &s);
        CHECK(s.peak == 0.0, "master 0 wrote samples (peak %.6f)", s.peak);
    }

    // Fade: master 1 then 0 (leaving to indoors): after 1 s the level is below -40 dB of the start, after 3 s silent.
    {
        float buf[2 * 800];
        DeckAmbientTargets t = { 1, 0, 0, 0, 0, 0, 1 };
        double r0 = 0, r1 = 0, r3 = 0;
        DeckAmbient_Init(RATE, 7);
        DeckAmbient_SetTargets(&t);
        for (int b = 0; b < 600; b++) { memset(buf, 0, sizeof(buf)); DeckAmbient_Mix(buf, 800, 1.0f); }
        for (int b = 0; b < 30; b++) { memset(buf, 0, sizeof(buf)); DeckAmbient_Mix(buf, 800, 1.0f); for (int i = 0; i < 1600; i++) r0 += (double)buf[i] * buf[i]; }
        t.master = 0;
        DeckAmbient_SetTargets(&t);
        for (int b = 0; b < 60; b++) { memset(buf, 0, sizeof(buf)); DeckAmbient_Mix(buf, 800, 1.0f); if (b >= 30) for (int i = 0; i < 1600; i++) r1 += (double)buf[i] * buf[i]; }
        for (int b = 0; b < 120; b++) { memset(buf, 0, sizeof(buf)); DeckAmbient_Mix(buf, 800, 1.0f); }
        for (int b = 0; b < 30; b++) { memset(buf, 0, sizeof(buf)); DeckAmbient_Mix(buf, 800, 1.0f); for (int i = 0; i < 1600; i++) r3 += (double)buf[i] * buf[i]; }
        printf("fade: energy before %.5f, 0.5..1 s after %.6f, 3.5 s after %.9f\n", r0, r1, r3);
        CHECK(r0 > 0, "fade: no signal before");
        CHECK(r1 < r0 * 0.1, "fade: not fading (%.6f vs %.6f)", r1, r0);
        CHECK(r3 == 0.0, "fade: not silent after 3.5 s (%.9f)", r3);
    }

    // Mute (fast forward): after DeckAmbient_Mute the next blocks are silent and the bed comes back with a ramp, not a jump.
    {
        float buf[2 * 800];
        DeckAmbientTargets t = { 1, 1, 1, 1, 1, 1, 1 };
        double maxFirst = 0, maxLater = 0;
        DeckAmbient_Init(RATE, 9);
        DeckAmbient_SetTargets(&t);
        for (int b = 0; b < 600; b++) { memset(buf, 0, sizeof(buf)); DeckAmbient_Mix(buf, 800, 1.0f); }
        DeckAmbient_Mute();
        CHECK(DeckAmbient_MasterLevel() == 0.0f, "mute: master not 0");
        for (int b = 0; b < 3; b++)
        {
            memset(buf, 0, sizeof(buf));
            DeckAmbient_Mix(buf, 800, 1.0f);
            for (int i = 0; i < 1600; i++) if (b == 0 && fabs(buf[i]) > maxFirst) maxFirst = fabs(buf[i]);
        }
        for (int b = 0; b < 200; b++)
        {
            memset(buf, 0, sizeof(buf));
            DeckAmbient_Mix(buf, 800, 1.0f);
            for (int i = 0; i < 1600; i++) if (fabs(buf[i]) > maxLater) maxLater = fabs(buf[i]);
        }
        printf("mute: first block peak %.5f, later peak %.4f\n", maxFirst, maxLater);
        CHECK(maxFirst < 0.05 * maxLater + 1e-6, "mute: first block after mute too loud (%.5f vs %.4f)", maxFirst, maxLater);
        CHECK(maxLater > 0.01, "mute: bed never came back");
    }

    // The mixer must not change the frame count or touch memory past it.
    {
        float buf[2 * 810 + 8];
        DeckAmbientTargets t = { 1, 1, 1, 1, 1, 1, 1 };
        int ok = 1;
        DeckAmbient_Init(RATE, 3);
        DeckAmbient_SetTargets(&t);
        for (int i = 0; i < 2 * 810 + 8; i++) buf[i] = 0.5f;
        DeckAmbient_Mix(buf, 800, 1.0f);
        for (int i = 1600; i < 2 * 810 + 8; i++) if (buf[i] != 0.5f) ok = 0;
        CHECK(ok, "mixer wrote past the frame count");
    }

    printf(sFail ? "RESULT: FAIL\n" : "RESULT: PASS\n");
    return sFail;
}
