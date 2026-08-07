/* nl_util.c - Nairobi Life: RNG, math helpers, value noise / fBm, log ring.
 *
 * Pure C99, no external dependencies beyond the C standard library.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include "nl_core.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ======================================================================== */
/*  Random number generation - xorshift32                                    */
/* ======================================================================== */

/* Marsaglia's xorshift32 with the classic (13, 17, 5) triple. Period 2^32-1.
 * A zero state is degenerate (it would stick at zero forever), so we fold a
 * non-zero constant in if the caller hands us zero. */
uint32_t nl_rand(uint32_t *state)
{
    uint32_t x;

    if (state == NULL) {
        /* Fall back to a private, still-deterministic stream. */
        static uint32_t fallback = 0x9E3779B9u;
        state = &fallback;
    }

    x = *state;
    if (x == 0u) x = 0x6D2B79F5u;   /* re-seed a dead state */

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;

    *state = x;
    return x;
}

/* Uniform float in [0,1). Uses the top 24 bits, which are the best mixed
 * bits of xorshift32, and matches the mantissa width of a float. */
float nl_randf(uint32_t *state)
{
    uint32_t r = nl_rand(state) >> 8;          /* 24 bits */
    return (float)r * (1.0f / 16777216.0f);    /* 1 / 2^24 */
}

float nl_randf_range(uint32_t *state, float a, float b)
{
    return a + (b - a) * nl_randf(state);
}

/* Inclusive integer range. Handles a > b by swapping. */
int nl_rand_range(uint32_t *state, int a, int b)
{
    uint32_t span, r;
    int lo = a, hi = b;

    if (lo > hi) { int t = lo; lo = hi; hi = t; }
    if (lo == hi) return lo;

    span = (uint32_t)(hi - lo) + 1u;
    if (span == 0u) return (int)nl_rand(state);   /* full 32-bit range */

    r = nl_rand(state) % span;
    return lo + (int)r;
}

/* ======================================================================== */
/*  Scalar / vector math helpers                                             */
/* ======================================================================== */

float nl_clampf(float v, float lo, float hi)
{
    if (lo > hi) { float t = lo; lo = hi; hi = t; }
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

float nl_lerpf(float a, float b, float t)
{
    return a + (b - a) * t;
}

/* Classic Hermite smoothstep, clamped outside [e0,e1]. */
float nl_smoothstep(float e0, float e1, float x)
{
    float t;
    float d = e1 - e0;

    if (fabsf(d) < 1e-9f) return (x < e0) ? 0.0f : 1.0f;

    t = (x - e0) / d;
    t = nl_clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float nl_vec_dist(NLVec2 a, NLVec2 b)
{
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return sqrtf(dx * dx + dy * dy);
}

NLVec2 nl_vec_norm(NLVec2 v)
{
    NLVec2 out;
    float len = sqrtf(v.x * v.x + v.y * v.y);

    if (len < 1e-6f) {
        out.x = 0.0f;
        out.y = 0.0f;
        return out;
    }
    out.x = v.x / len;
    out.y = v.y / len;
    return out;
}

/* ======================================================================== */
/*  Value noise and fractional Brownian motion                               */
/* ======================================================================== */

/* Integer hash -> [0,1). A finalizer-style avalanche mix so that neighbouring
 * lattice points give completely uncorrelated values. */
static float nl_hash1(int32_t i, uint32_t seed)
{
    uint32_t h = (uint32_t)i * 0x9E3779B1u + seed * 0x85EBCA6Bu;

    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;

    return (float)(h >> 8) * (1.0f / 16777216.0f);
}

/* 1D value noise in [0,1], Hermite-interpolated between lattice points so it
 * is C1-continuous - no visible kinks as the simulated hour advances. */
float nl_noise1(float x, uint32_t seed)
{
    float fx = floorf(x);
    int32_t i0 = (int32_t)fx;
    float t = x - fx;
    float u = t * t * (3.0f - 2.0f * t);
    float a = nl_hash1(i0, seed);
    float b = nl_hash1(i0 + 1, seed);

    return a + (b - a) * u;
}

/* Fractional Brownian motion: sum of octaves at doubling frequency and
 * halving amplitude, normalised back into [0,1]. */
float nl_fbm1(float x, uint32_t seed, int octaves)
{
    float sum = 0.0f;
    float amp = 0.5f;
    float freq = 1.0f;
    float norm = 0.0f;
    int i;

    if (octaves < 1) octaves = 1;
    if (octaves > 12) octaves = 12;

    for (i = 0; i < octaves; ++i) {
        /* Offset each octave so the lattices do not line up at x = 0. */
        sum  += amp * nl_noise1(x * freq + (float)i * 37.31f,
                                seed + (uint32_t)i * 0x1F123BB5u);
        norm += amp;
        freq *= 2.0f;
        amp  *= 0.5f;
    }

    if (norm <= 0.0f) return 0.5f;
    return nl_clampf(sum / norm, 0.0f, 1.0f);
}

/* ======================================================================== */
/*  Message log - fixed-capacity ring buffer                                 */
/* ======================================================================== */

void nl_log_init(NLLog *log)
{
    if (log == NULL) return;

    memset(log, 0, sizeof(*log));
    log->head = 0;
    log->count = 0;
}

/* Default lifetimes (seconds of real time) per message class. Bad news hangs
 * around longer than routine chatter. */
static float nl_log_ttl_for(NLMsgKind kind)
{
    switch (kind) {
        case NL_MSG_GOOD:  return 7.0f;
        case NL_MSG_WARN:  return 9.0f;
        case NL_MSG_BAD:   return 11.0f;
        case NL_MSG_MONEY: return 8.0f;
        case NL_MSG_INFO:
        default:           return 6.0f;
    }
}

void nl_log_push(NLLog *log, NLMsgKind kind, const char *fmt, ...)
{
    NLLogLine *line;
    va_list ap;
    int written;
    int slot;

    if (log == NULL || fmt == NULL) return;

    /* head is the index of the *next* slot to write. */
    slot = log->head % NL_MAX_LOG_LINES;
    if (slot < 0) slot = 0;
    line = &log->lines[slot];

    memset(line, 0, sizeof(*line));

    va_start(ap, fmt);
    written = vsnprintf(line->text, sizeof(line->text), fmt, ap);
    va_end(ap);

    /* vsnprintf always NUL-terminates when the buffer size is > 0, but be
     * defensive about hostile/broken implementations and encoding errors. */
    if (written < 0) {
        line->text[0] = '\0';
    } else if ((size_t)written >= sizeof(line->text)) {
        /* Truncated: mark it visibly rather than silently cutting a word. */
        line->text[NL_LOG_LINE_LEN - 1] = '\0';
        if (NL_LOG_LINE_LEN >= 4) {
            line->text[NL_LOG_LINE_LEN - 2] = '.';
            line->text[NL_LOG_LINE_LEN - 3] = '.';
            line->text[NL_LOG_LINE_LEN - 4] = '.';
        }
    }
    line->text[NL_LOG_LINE_LEN - 1] = '\0';

    line->kind = kind;
    line->age = 0.0f;
    line->ttl = nl_log_ttl_for(kind);

    log->head = (slot + 1) % NL_MAX_LOG_LINES;
    if (log->count < NL_MAX_LOG_LINES) log->count++;
}

void nl_log_update(NLLog *log, float dt)
{
    int i;

    if (log == NULL) return;
    if (dt < 0.0f) dt = 0.0f;

    if (log->count > NL_MAX_LOG_LINES) log->count = NL_MAX_LOG_LINES;
    if (log->count < 0) log->count = 0;

    for (i = 0; i < log->count; ++i) {
        /* Walk backwards from the newest entry. */
        int idx = log->head - 1 - i;
        while (idx < 0) idx += NL_MAX_LOG_LINES;
        idx %= NL_MAX_LOG_LINES;

        log->lines[idx].age += dt;
        if (log->lines[idx].ttl > 0.0f &&
            log->lines[idx].age > log->lines[idx].ttl + 3600.0f) {
            /* Very old entries get their age pinned so the float does not
             * grow without bound over a long session. */
            log->lines[idx].age = log->lines[idx].ttl + 3600.0f;
        }
    }
}
