#include <math.h>
#include <stdint.h>
#include <string.h>
#include "deck_ambient.h"

// Procedural ambient layers. All constants were set by reading per-layer RMS and peak from
// tools/deck_tests/test_ambient.c (target: bed at gain 1 about 15 to 20 dB under the music, peak under 0.3).

enum { L_WIND, L_BIRDS, L_CRICKETS, L_WATER, L_RAIN, L_CAVE, L_COUNT };

#define TWO_PI 6.28318530718f
#define SIN_N 2048
#define MAX_BIRDS 3
#define MAX_CRICKETS 4
#define MAX_DRIPS 4

typedef struct { float lp1, lp2; } Pole2;

typedef struct
{
    float phase;        // 0..1 position in the swell
    float len;          // swell length in samples
    float lp1, lp2;
} Wave;

typedef struct
{
    float freq, phase, pan, amp;
    float pulseLen;     // samples per pulse
    float trainLen;     // samples per whole cheep cycle
    int pulses;
    float pos;
} Cricket;

typedef struct
{
    int active;
    int notes, note;
    float t, noteLen, gapLen;
    float f0, f1, phase, pan, amp;
} Bird;

typedef struct
{
    int active;
    float delay, freq, phase, env, amp, pan;
} Drip;

static int sInit;
static float sRate = 48000.0f;
static uint32_t sRng = 1;
static float sSin[SIN_N + 1];

static float sTarget[L_COUNT], sLevel[L_COUNT];
static float sMasterTarget, sMaster;
static float sGainPrev;
static float sTime;

static Pole2 sWind[2];
static Pole2 sRainLp[2];
static float sDropEnv[2];
static Wave sWave[2];
static Cricket sCricket[MAX_CRICKETS];
static Bird sBird[MAX_BIRDS];
static float sBirdWait;
static float sDensity = 1.0f;   // one-shot rate multiplier (DeckAmbient_SetDensity)
static Pole2 sCaveLp[2];
static float sCavePhaseA, sCavePhaseB;
static Drip sDrip[MAX_DRIPS];
static float sDripWait;
static float sThunderDelay;   // samples until the rumble starts; < 0 = none pending
static float sThunderEnv, sThunderAge;
static int sThunderQueued;    // a bolt arrived during a rumble: start the next one when this one ends
static Pole2 sThunderLp[2];

static uint32_t Rnd(void)
{
    sRng ^= sRng << 13;
    sRng ^= sRng >> 17;
    sRng ^= sRng << 5;
    return sRng;
}

static float Rf(void) { return (float)(Rnd() >> 8) * (1.0f / 16777216.0f); }            // 0..1
static float Rs(void) { return (float)(Rnd() >> 8) * (2.0f / 16777216.0f) - 1.0f; }     // -1..1
static float Rr(float lo, float hi) { return lo + (hi - lo) * Rf(); }

static float Sin01(float p)
{
    float x = p * SIN_N;
    int i = (int)x;
    float fr = x - (float)i;
    i &= (SIN_N - 1);
    return sSin[i] + (sSin[i + 1] - sSin[i]) * fr;
}

static float Wrap01(float p) { return p - floorf(p); }
static float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static float CutoffCoef(float hz) { return 1.0f - expf(-TWO_PI * hz / sRate); }

static void NewWave(Wave *w, float startPhase)
{
    w->phase = startPhase;
    w->len = Rr(6.0f, 10.0f) * sRate;
}

static void NewCricket(Cricket *c, int index)
{
    c->freq = Rr(3600.0f, 5200.0f);
    c->phase = 0.0f;
    c->pan = -0.8f + 1.6f * (float)index / (float)(MAX_CRICKETS - 1);
    c->amp = Rr(0.7f, 1.0f);
    c->pulseLen = sRate / Rr(26.0f, 36.0f);
    c->pulses = 3 + (int)(Rnd() % 3u);
    c->trainLen = Rr(0.35f, 0.75f) * sRate;
    c->pos = Rf() * c->trainLen;
}

static void StartBird(Bird *b)
{
    b->active = 1;
    b->notes = 1 + (int)(Rnd() % 4u);
    b->note = 0;
    b->t = 0.0f;
    b->noteLen = Rr(0.05f, 0.14f) * sRate;
    b->gapLen = Rr(0.03f, 0.09f) * sRate;
    b->f0 = Rr(2200.0f, 4800.0f);
    b->f1 = b->f0 + Rr(-1500.0f, 1500.0f);
    if (b->f1 < 1500.0f)
        b->f1 = 1500.0f;
    b->phase = 0.0f;
    b->pan = Rs() * 0.9f;
    b->amp = Rr(0.5f, 1.0f);
}

static void StartDrip(float level)
{
    const float pan = Rs() * 0.7f;
    for (int pass = 0; pass < 2; pass++)
    {
        for (int i = 0; i < MAX_DRIPS; i++)
        {
            if (!sDrip[i].active)
            {
                sDrip[i].active = 1;
                sDrip[i].delay = (pass == 0) ? 0.0f : Rr(0.12f, 0.2f) * sRate;
                sDrip[i].freq = Rr(1200.0f, 2600.0f) * (pass == 0 ? 1.0f : 0.96f);
                sDrip[i].phase = 0.0f;
                sDrip[i].env = 1.0f;
                sDrip[i].amp = (pass == 0 ? 1.0f : 0.35f) * (0.6f + 0.4f * level);
                sDrip[i].pan = pan;
                break;
            }
        }
    }
}

void DeckAmbient_SetDensity(float density)
{
    sDensity = density < 0.2f ? 0.2f : (density > 3.0f ? 3.0f : density);
}

void DeckAmbient_Init(unsigned rate, unsigned seed)
{
    sRate = rate > 8000u ? (float)rate : 48000.0f;
    sRng = seed ? seed : 0x9e3779b9u;
    for (int i = 0; i <= SIN_N; i++)
        sSin[i] = sinf(TWO_PI * (float)i / (float)SIN_N);

    memset(sTarget, 0, sizeof(sTarget));
    memset(sLevel, 0, sizeof(sLevel));
    sMasterTarget = sMaster = 0.0f;
    sGainPrev = 0.0f;
    sTime = 0.0f;
    memset(sWave, 0, sizeof(sWave));
    memset(sCricket, 0, sizeof(sCricket));
    memset(sWind, 0, sizeof(sWind));
    memset(sRainLp, 0, sizeof(sRainLp));
    memset(sDropEnv, 0, sizeof(sDropEnv));
    memset(sBird, 0, sizeof(sBird));
    memset(sCaveLp, 0, sizeof(sCaveLp));
    memset(sDrip, 0, sizeof(sDrip));
    memset(sThunderLp, 0, sizeof(sThunderLp));
    sCavePhaseA = sCavePhaseB = 0.0f;
    sThunderDelay = -1.0f;
    sThunderEnv = 0.0f;
    sThunderAge = 0.0f;
    sThunderQueued = 0;
    NewWave(&sWave[0], 0.0f);
    NewWave(&sWave[1], 0.37f);
    for (int i = 0; i < MAX_CRICKETS; i++)
        NewCricket(&sCricket[i], i);
    sBirdWait = Rr(0.5f, 2.0f) * sRate;
    sDripWait = Rr(1.0f, 3.0f) * sRate;
    sInit = 1;
}

void DeckAmbient_SetTargets(const DeckAmbientTargets *t)
{
    if (t == NULL)
        return;
    sTarget[L_WIND] = Clamp01(t->wind);
    sTarget[L_BIRDS] = Clamp01(t->birds);
    sTarget[L_CRICKETS] = Clamp01(t->crickets);
    sTarget[L_WATER] = Clamp01(t->water);
    sTarget[L_RAIN] = Clamp01(t->rain);
    sTarget[L_CAVE] = Clamp01(t->cave);
    sMasterTarget = Clamp01(t->master);
}

void DeckAmbient_Thunder(void)
{
    // sThunderDelay: -1 idle, >= 0 pending, -2 rumbling. A bolt while idle starts one; during a rumble it queues one
    // more to follow (so the playing envelope never restarts or steps); while one is already pending it adds nothing.
    if (!sInit)
        return;
    if (sThunderDelay == -1.0f)
        sThunderDelay = Rr(0.3f, 1.5f) * sRate;
    else if (sThunderDelay == -2.0f)
        sThunderQueued = 1;
}

void DeckAmbient_Mute(void)
{
    if (!sInit)
        return;
    memset(sLevel, 0, sizeof(sLevel));
    sMaster = 0.0f;
    sGainPrev = 0.0f;
    memset(sBird, 0, sizeof(sBird));
    memset(sDrip, 0, sizeof(sDrip));
    sThunderDelay = -1.0f;
    sThunderEnv = 0.0f;
    sThunderQueued = 0;
    sDropEnv[0] = sDropEnv[1] = 0.0f;
}

float DeckAmbient_MasterLevel(void) { return sMaster; }

static float SoftLimit(float x)
{
    return x / (1.0f + fabsf(x));
}

void DeckAmbient_Mix(float *out, int frames, float gain)
{
    static const float tau[L_COUNT] = { 2.5f, 2.5f, 2.5f, 2.5f, 1.5f, 2.5f };
    float lv0[L_COUNT], lv1[L_COUNT];
    float m0, m1, g0, g1;
    const float dt = (float)frames / sRate;

    if (!sInit || out == NULL || frames <= 0 || gain <= 0.0f)
    {
        sGainPrev = 0.0f;   // hard off: no DSP; the next audible block ramps up from zero
        return;
    }
    if (sMasterTarget <= 0.0f && sMaster < 0.0005f)
    {
        sMaster = 0.0f;
        sGainPrev = gain;
        return;             // fully faded out (indoors, battle, menu): nothing to render
    }

    for (int i = 0; i < L_COUNT; i++)
    {
        lv0[i] = sLevel[i];
        sLevel[i] += (sTarget[i] - sLevel[i]) * (1.0f - expf(-dt / tau[i]));
        lv1[i] = sLevel[i];
    }
    m0 = sMaster;
    sMaster += (sMasterTarget - sMaster) * (1.0f - expf(-dt / 0.33f));
    m1 = sMaster;
    g0 = sGainPrev;
    g1 = gain;
    sGainPrev = gain;

    // Control-rate values, once per block
    sTime += dt;
    if (sTime > 100.0f)
        sTime -= 100.0f;   // 100 s is a whole number of periods of 0.11 and 0.27 Hz, so the gust does not jump
    {
        const float gust = 0.5f + 0.5f * (0.6f * sinf(TWO_PI * 0.11f * sTime) + 0.4f * sinf(TWO_PI * 0.27f * sTime + 1.3f));
        const float windFc = 160.0f + 520.0f * gust * (0.4f + 0.6f * lv1[L_WIND]);
        const float windA = CutoffCoef(windFc);
        const float windAmp = 0.35f + 0.65f * gust;
        const float rainHp = CutoffCoef(1400.0f);
        const float dropRate = (25.0f + 170.0f * lv1[L_RAIN]) / sRate;
        const float caveA = CutoffCoef(140.0f);
        const float thunderA = CutoffCoef(110.0f);
        const float thunderDecay = expf(-1.0f / (1.2f * sRate));
        const float dripDecay = expf(-1.0f / (0.045f * sRate));
        const int windOn = lv1[L_WIND] > 0.001f || lv0[L_WIND] > 0.001f;
        const int rainOn = lv1[L_RAIN] > 0.001f || lv0[L_RAIN] > 0.001f;
        const int waterOn = lv1[L_WATER] > 0.001f || lv0[L_WATER] > 0.001f;
        const int cricketsOn = lv1[L_CRICKETS] > 0.001f || lv0[L_CRICKETS] > 0.001f;
        const int birdsOn = lv1[L_BIRDS] > 0.001f || lv0[L_BIRDS] > 0.001f;
        const int caveOn = lv1[L_CAVE] > 0.001f || lv0[L_CAVE] > 0.001f;

        for (int n = 0; n < frames; n++)
        {
            const float u = (float)(n + 1) / (float)frames;
            float sum[2] = { 0.0f, 0.0f };

            if (windOn)
            {
                const float lv = lv0[L_WIND] + (lv1[L_WIND] - lv0[L_WIND]) * u;
                for (int c = 0; c < 2; c++)
                {
                    sWind[c].lp1 += windA * (Rs() - sWind[c].lp1);
                    sWind[c].lp2 += windA * (sWind[c].lp1 - sWind[c].lp2);
                    sum[c] += sWind[c].lp2 * windAmp * lv * 0.9f;
                }
            }

            if (rainOn)
            {
                const float lv = lv0[L_RAIN] + (lv1[L_RAIN] - lv0[L_RAIN]) * u;
                if (Rf() < dropRate * lv)
                {
                    const float pan = Rf();
                    const float a = Rr(0.3f, 1.0f);
                    sDropEnv[0] += a * (1.0f - pan);
                    sDropEnv[1] += a * pan;
                }
                for (int c = 0; c < 2; c++)
                {
                    const float nz = Rs();
                    sRainLp[c].lp1 += rainHp * (nz - sRainLp[c].lp1);
                    const float hp = nz - sRainLp[c].lp1;             // high-passed hiss
                    sRainLp[c].lp2 += 0.45f * (hp - sRainLp[c].lp2);  // takes the sting off above ~5 kHz
                    sum[c] += sRainLp[c].lp2 * lv * 0.15f + hp * sDropEnv[c] * 0.05f * lv;
                    sDropEnv[c] *= 0.9955f;
                    if (sDropEnv[c] < 1e-9f)
                        sDropEnv[c] = 0.0f;   // no denormals
                }
            }

            if (waterOn)
            {
                const float lv = lv0[L_WATER] + (lv1[L_WATER] - lv0[L_WATER]) * u;
                for (int c = 0; c < 2; c++)
                {
                    Wave *w = &sWave[c];
                    const float s = Sin01(w->phase * 0.5f);   // 0..1 half sine over the swell
                    const float env = s * s;
                    const float a = CutoffCoef(260.0f + 1900.0f * env);
                    w->lp1 += a * (Rs() - w->lp1);
                    w->lp2 += a * (w->lp1 - w->lp2);
                    sum[c] += w->lp2 * (0.12f + 0.88f * env) * lv * 0.75f;
                    w->phase += 1.0f / w->len;
                    if (w->phase >= 1.0f)
                        NewWave(w, 0.0f);
                }
            }

            if (cricketsOn)
            {
                const float lv = lv0[L_CRICKETS] + (lv1[L_CRICKETS] - lv0[L_CRICKETS]) * u;
                for (int i = 0; i < MAX_CRICKETS; i++)
                {
                    Cricket *k = &sCricket[i];
                    const float trainSpan = k->pulseLen * (float)k->pulses;
                    float env = 0.0f;
                    if (k->pos < trainSpan)
                    {
                        const float within = k->pos - k->pulseLen * floorf(k->pos / k->pulseLen);
                        const float x = within / (k->pulseLen * 0.6f);
                        if (x < 1.0f)
                            env = Sin01(x * 0.5f);
                    }
                    k->pos += 1.0f;
                    if (k->pos >= k->trainLen)
                    {
                        const float f = k->freq;
                        const float p = k->pan;
                        NewCricket(k, i);
                        k->freq = f * Rr(0.995f, 1.005f);
                        k->pan = p;
                        k->pos = 0.0f;
                    }
                    if (env > 0.0f)
                    {
                        const float v = Sin01(k->phase) * env * env * k->amp * lv * 0.09f;
                        sum[0] += v * (0.5f - 0.5f * k->pan);
                        sum[1] += v * (0.5f + 0.5f * k->pan);
                    }
                    k->phase = Wrap01(k->phase + k->freq / sRate);
                }
            }

            if (birdsOn)
            {
                const float lv = lv0[L_BIRDS] + (lv1[L_BIRDS] - lv0[L_BIRDS]) * u;
                sBirdWait -= 1.0f;
                if (sBirdWait <= 0.0f)
                {
                    for (int i = 0; i < MAX_BIRDS; i++)
                    {
                        if (!sBird[i].active)
                        {
                            StartBird(&sBird[i]);
                            break;
                        }
                    }
                    sBirdWait = Rr(0.8f, 4.5f) * sRate / ((0.25f + lv) * sDensity);
                }
                for (int i = 0; i < MAX_BIRDS; i++)
                {
                    Bird *b = &sBird[i];
                    if (!b->active)
                        continue;
                    if (b->t < b->noteLen)
                    {
                        const float x = b->t / b->noteLen;
                        const float e = Sin01(x * 0.5f);
                        const float f = b->f0 + (b->f1 - b->f0) * x;
                        const float v = (Sin01(b->phase) + 0.25f * Sin01(Wrap01(b->phase * 2.0f))) * e * e * b->amp * lv * 0.12f;
                        sum[0] += v * (0.5f - 0.5f * b->pan);
                        sum[1] += v * (0.5f + 0.5f * b->pan);
                        b->phase = Wrap01(b->phase + f / sRate);
                    }
                    b->t += 1.0f;
                    if (b->t >= b->noteLen + b->gapLen)
                    {
                        b->note++;
                        b->t = 0.0f;
                        b->f0 *= Rr(0.9f, 1.12f);
                        b->f1 = b->f0 + Rr(-1200.0f, 1200.0f);
                        if (b->f1 < 1500.0f)
                            b->f1 = 1500.0f;
                        if (b->note >= b->notes)
                            b->active = 0;
                    }
                }
            }

            if (caveOn)
            {
                const float lv = lv0[L_CAVE] + (lv1[L_CAVE] - lv0[L_CAVE]) * u;
                for (int c = 0; c < 2; c++)
                {
                    sCaveLp[c].lp1 += caveA * (Rs() - sCaveLp[c].lp1);
                    sCaveLp[c].lp2 += caveA * (sCaveLp[c].lp1 - sCaveLp[c].lp2);
                    sum[c] += sCaveLp[c].lp2 * lv * 0.6f;
                }
                {
                    const float tone = (Sin01(sCavePhaseA) + Sin01(sCavePhaseB)) * 0.5f * lv * 0.03f;
                    sum[0] += tone;
                    sum[1] += tone;
                    sCavePhaseA = Wrap01(sCavePhaseA + 52.0f / sRate);
                    sCavePhaseB = Wrap01(sCavePhaseB + 53.3f / sRate);
                }
                sDripWait -= 1.0f;
                if (sDripWait <= 0.0f)
                {
                    StartDrip(lv);
                    sDripWait = Rr(1.5f, 6.0f) * sRate / sDensity;
                }
                for (int i = 0; i < MAX_DRIPS; i++)
                {
                    Drip *d = &sDrip[i];
                    if (!d->active)
                        continue;
                    if (d->delay > 0.0f)
                    {
                        d->delay -= 1.0f;
                        continue;
                    }
                    {
                        const float v = Sin01(d->phase) * d->env * d->amp * 0.05f;
                        sum[0] += v * (0.5f - 0.5f * d->pan);
                        sum[1] += v * (0.5f + 0.5f * d->pan);
                    }
                    d->phase = Wrap01(d->phase + d->freq / sRate);
                    d->freq *= 0.99997f;
                    d->env *= dripDecay;
                    if (d->env < 0.003f)
                        d->active = 0;
                }
            }

            if (sThunderDelay >= 0.0f)
            {
                sThunderDelay -= 1.0f;
                if (sThunderDelay < 0.0f)
                {
                    sThunderEnv = 1.0f;
                    sThunderAge = 0.0f;
                    sThunderDelay = -2.0f;   // playing
                }
            }
            if (sThunderEnv > 0.002f)
            {
                const float atk = sThunderAge < 0.06f * sRate ? sThunderAge / (0.06f * sRate) : 1.0f;
                for (int c = 0; c < 2; c++)
                {
                    sThunderLp[c].lp1 += thunderA * (Rs() - sThunderLp[c].lp1);
                    sThunderLp[c].lp2 += thunderA * (sThunderLp[c].lp1 - sThunderLp[c].lp2);
                    sum[c] += sThunderLp[c].lp2 * sThunderEnv * atk * 4.0f;
                }
                sThunderEnv *= thunderDecay;
                sThunderAge += 1.0f;
            }
            else if (sThunderDelay == -2.0f)
            {
                sThunderEnv = 0.0f;
                sThunderDelay = -1.0f;   // finished; a new request may start
                if (sThunderQueued)
                {
                    sThunderQueued = 0;
                    sThunderDelay = Rr(0.3f, 1.5f) * sRate;
                }
            }

            {
                const float mg = (m0 + (m1 - m0) * u) * (g0 + (g1 - g0) * u);
                out[2 * n] += SoftLimit(sum[0]) * mg * 0.8f;
                out[2 * n + 1] += SoftLimit(sum[1]) * mg * 0.8f;
            }
        }
    }
}
