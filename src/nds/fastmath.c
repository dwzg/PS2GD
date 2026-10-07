/*
 * sinf, cosf and expf for the DS, in place of the C library's.
 *
 * The ARM9 has no floating point unit: every float operation is a call to
 * libgcc's software routines, of 30 to 100 cycles. The C library's sinf and
 * expf (picolibc's, from Arm's optimized routines) work in double
 * precision, which in software costs several times that. The core calls these for
 * drawing (circles, arcs, animations, the beat's pulse) and for the camera,
 * never for the physics, which is integer arithmetic (sim.c), so these
 * trade the last digits for speed: sinf and cosf interpolate a table of
 * 1024 steps a turn, in integers (off by at most 6e-6), expf is 2^x from its integer
 * part's exponent bits and a polynomial for the rest (relative error under
 * 4e-6, most of it float's rounding of x * log2(e)). The linker takes them before the library's.
 */
#include <math.h>
#include <stdint.h>
#include <string.h>
#ifdef ARM9
#include <nds/arm9/math.h>
#endif

#define STEPS 1024 /* table steps a turn (a power of two) */

static int32_t s_sin[STEPS + 1 + STEPS / 4]; /* sin, 2^30 to 1 */
static int s_ready;

static void init(void)
{
    for (int i = 0; i <= STEPS + STEPS / 4; i++) s_sin[i] = (int32_t)floor(sin(i * (2.0 * M_PI / STEPS)) * 1073741824.0 + 0.5);
    s_ready = 1;
}

/* STEPS / 2pi in 8.24 */
#define K_STEPS ((uint32_t)(STEPS / (2.0 * M_PI) * 16777216.0 + 0.5))

/*
 * The table at |x| (radians) plus a quarter turn times q, all
 * in integers: the angle in table steps from the float's bits (16 bits of
 * them below the step), the two entries around it interpolated, and the
 * result made a float once.
 */
static float lookup(float x, int q, int odd)
{
    if (!s_ready) init();
    uint32_t u;
    memcpy(&u, &x, 4);
    int neg = (int)(u >> 31);
    int e = (int)((u >> 23) & 255);
    /* |x| = m * 2^(e - 150); in steps, 16.16: m * K_STEPS * 2^(e - 150 - 24 + 16) */
    if (e > 150) return e == 255 && (u & 0x7FFFFFu) ? x : (float)(q & 1); /* NaN, or past 2^24: 0 or 1 */
    int sh = 158 - e;
    uint32_t t = 0;
    if (sh < 64) t = (uint32_t)(((uint64_t)((u & 0x7FFFFFu) | 0x800000u) * K_STEPS) >> sh);
    /* sin(-x) = -sin(x), cos(-x) = cos(x) */
    uint32_t i = ((t >> 16) + (uint32_t)q * (STEPS / 4)) & (STEPS - 1);
    int32_t a = s_sin[i], b = s_sin[i + 1];
    int32_t v = a + (int32_t)(((int64_t)(b - a) * (int32_t)(t & 0xFFFF)) >> 16);
    if (neg && odd) v = -v;
    if (v == 0) return 0.0f;
    /* v / 2^30: the int as a float, its exponent lowered by 30 */
    float f = (float)v;
    memcpy(&u, &f, 4);
    u -= 30u << 23;
    memcpy(&f, &u, 4);
    return f;
}

float sinf(float x)
{
    return lookup(x, 0, 1);
}

float cosf(float x)
{
    return lookup(x, 1, 0);
}

void sincosf(float x, float *s, float *c)
{
    *s = sinf(x);
    *c = cosf(x);
}

float expf(float x)
{
    if (x != x) return x;
    if (x > 88.7f) return HUGE_VALF;
    if (x < -87.3f) return 0.0f;
    /* e^x = 2^(n + f), n the nearest whole number, |f| <= 1/2 */
    float t = x * 1.44269504f;
    int32_t n = (int32_t)(t < 0.0f ? t - 0.5f : t + 0.5f);
    float f = t - (float)n;
    /* 2^f: its Taylor series to the 6th power */
    float p = 1.0f + f * (0.693147181f + f * (0.240226507f + f * (0.0555041087f +
                     f * (0.00961812911f + f * (0.00133335581f + f * 0.000154035304f)))));
    uint32_t bits;
    memcpy(&bits, &p, 4);
    bits += (uint32_t)n << 23;
    memcpy(&p, &bits, 4);
    return p;
}

#ifdef ARM9
float sqrtf(float x)
{
    uint32_t u;
    memcpy(&u, &x, 4);
    int e = (int)((u >> 23) & 255);
    if (!(u & 0x7FFFFFFFu) || (e == 255 && !(u >> 31))) return x; /* +-0, +inf, NaN */
    if (u >> 31) return (x - x) / (x - x);                        /* NaN */
    uint32_t m = u & 0x7FFFFFu;
    if (e) m |= 0x800000u;
    else
        for (e = 1; !(m & 0x800000u); e--) m <<= 1; /* (subnormal) */
    /* x = m * 2^p = (m << sh) * 2^(p - sh), sh making that exponent even
     * and m 63 or 64 bits */
    int p = e - 150, sh = (p & 1) ? 39 : 40;
    REG_SQRTCNT = SQRT_64;
    REG_SQRT_PARAM = (uint64_t)m << sh;
    while (REG_SQRTCNT & SQRT_BUSY)
        ;
    uint32_t r = REG_SQRT_RESULT; /* floor(sqrt(m << sh)): 31 or 32 bits */
    /* sqrt(x) = r * 2^((p - sh) / 2): r's top 24 bits, rounded (never a
     * tie: no root falls halfway between two floats) */
    int bits = 32 - __builtin_clz(r), drop = bits - 24;
    uint32_t q = (r >> drop) + ((r >> (drop - 1)) & 1);
    int exp2 = (p - sh) / 2 + drop;
    if (q >> 24) {
        q >>= 1;
        exp2++;
    }
    /* q * 2^exp2, q in [2^23, 2^24) */
    u = ((uint32_t)(exp2 + 23 + 127) << 23) | (q & 0x7FFFFFu);
    memcpy(&x, &u, 4);
    return x;
}
#endif
