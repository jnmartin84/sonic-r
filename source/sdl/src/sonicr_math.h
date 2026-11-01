/**
 * sonicr_math.h — Precision-portable math.
 *
 * Routes double / long double math to either double or float at compile
 * time. Default = double (host x86 / SDL builds). Define SONICR_FLOAT32
 * for the Dreamcast SH4 `-m4-single-only` build, where double-precision
 * math is forbidden and `long double` libc routines (atan2l/sinl/lrintl)
 * are not provided by sh-elf newlib.
 */

#ifndef SONICR_MATH_H
#define SONICR_MATH_H

#include <math.h>

#ifdef SONICR_FLOAT32
typedef float sr_double;

static inline sr_double sr_atan2(sr_double y, sr_double x)
{
    return atan2f(y, x);
}

static inline sr_double sr_sqrt(sr_double x)
{
    return sqrtf(x);
}

static inline sr_double sr_sin(sr_double x) {
    return sinf(x);
}

static inline long sr_lrint(sr_double x) {
    return lrintf(x);
}
#else
typedef double sr_double;

#ifdef SONICR_XBOX
/* nxdk's libm atan2/lrint hang on real hardware (the race camera froze inside
 * them), so use self-contained versions built from plain arithmetic. */
static inline double sr_xb_atan(double z)   /* |z| <= 1 */
{
    double z2 = z * z;
    /* minimax-ish odd polynomial, error ~1e-5 rad before refinement; use
     * series on a halved argument for ~1e-9 accuracy */
    double h = z / (1.0 + sqrt(1.0 + z2));      /* atan(z) = 2*atan(h), |h|<=0.4142 */
    double h2 = h * h;
    double p = h * (1.0 + h2 * (-1.0/3 + h2 * (1.0/5 + h2 * (-1.0/7 + h2 * (1.0/9 +
               h2 * (-1.0/11 + h2 * (1.0/13 + h2 * (-1.0/15 + h2 * (1.0/17)))))))));
    return 2.0 * p;
}
static inline sr_double sr_atan2(sr_double y, sr_double x)
{
    const double PI = 3.14159265358979323846;
    double ax = x < 0 ? -x : x, ay = y < 0 ? -y : y, r;
    if (ax == 0.0 && ay == 0.0) return 0.0;
    if (ay <= ax) r = sr_xb_atan(ay / ax);
    else          r = PI / 2 - sr_xb_atan(ax / ay);
    if (x < 0) r = PI - r;
    return y < 0 ? -r : r;
}
#else
static inline sr_double sr_atan2(sr_double y, sr_double x)
{
    return atan2(y, x);
}
#endif

static inline sr_double sr_sqrt(sr_double x)
{
    return sqrt(x);
}

static inline sr_double sr_sin(sr_double x)
{
    return sin(x);
}

#ifdef SONICR_XBOX
static inline long sr_lrint(sr_double x)   /* round half to even, like lrint */
{
    long f = (long)x;                      /* truncates toward zero */
    double d = x - (double)f;
    if (d >= 0.5 || d <= -0.5) {
        if (d == 0.5 || d == -0.5) {
            if (f & 1) f += (x < 0 ? -1 : 1);
        } else f += (x < 0 ? -1 : 1);
    }
    return f;
}
#else
static inline long sr_lrint(sr_double x)
{
    return lrint(x);
}
#endif
#endif

/* Always-float sqrt — independent of SONICR_FLOAT32. Use at sites where
 * the input is float and the result is float, to keep the operand off
 * the double-precision path on builds where sr_sqrt promotes (host SDL
 * default).
 *
 * On SH4 (DC) we go via fsrra (1/sqrt(x), 1 cycle) and one fmul, ~2
 * cycles total — substantially faster than fsqrt (~11 cycles). The
 * x==0 guard prevents the inf*0 → NaN that fsrra(0)*0 would produce.
 * Host falls back to libc sqrtf. */
#ifdef SONICR_DC
__attribute__((always_inline)) static inline float sr_inv_sqrtf(float x)
{
    asm volatile ("fsrra %0" : "+f"(x));
    return x;
}
__attribute__((always_inline)) static inline float sr_sqrtf(float x)
{
    return (x == 0.0f) ? 0.0f : sr_inv_sqrtf(x) * x;
}
#else
static inline float sr_sqrtf(float x)
{
    return sqrtf(x);
}
#endif

#endif /* SONICR_MATH_H */
