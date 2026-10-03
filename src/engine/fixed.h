// fixed.h - fixed-point 24.8 math (256 = 1.0), sine/cosine in degrees, random numbers.
// The GBA has no floating point unit, so the engine never uses float or double.
#ifndef ENGINE_FIXED_H
#define ENGINE_FIXED_H

#include <tonc_types.h>

typedef s32 fixed;

#define FX_SHIFT 8
#define FX_ONE   (1 << FX_SHIFT)

// Whole number -> fixed.
static inline fixed fx_from_int(s32 n) { return n * FX_ONE; }
// Fixed -> whole number (rounds toward minus infinity, so negatives stay consistent).
static inline s32 fx_to_int(fixed f) { return f >> FX_SHIFT; }
// Multiply two fixed numbers.
static inline fixed fx_mul(fixed a, fixed b) { return (fixed)(((s64)a * b) >> FX_SHIFT); }
// Divide two fixed numbers.
static inline fixed fx_div(fixed a, fixed b) { return b ? (fixed)(((s64)a << FX_SHIFT) / b) : 0; }
// Absolute value.
static inline s32 fx_abs(s32 v) { return v < 0 ? -v : v; }
// Sign: -1, 0 or 1.
static inline s32 fx_sign(s32 v) { return (v > 0) - (v < 0); }

// Sine of an angle in whole degrees, as fixed (-256..256).
static inline fixed fx_sin_deg(s32 deg) {
    static const u16 quarter[91] = {   // round(sin(d) * 256) for d = 0..90
          0,   4,   9,  13,  18,  22,  27,  31,  36,  40,  44,  49,  53,  58,  62,  66,
         71,  75,  79,  83,  88,  92,  96, 100, 104, 108, 112, 116, 120, 124, 128, 132,
        136, 139, 143, 147, 150, 154, 158, 161, 165, 168, 171, 175, 178, 181, 184, 187,
        190, 193, 196, 199, 202, 204, 207, 210, 212, 215, 217, 219, 222, 224, 226, 228,
        230, 232, 234, 236, 237, 239, 241, 242, 243, 245, 246, 247, 248, 249, 250, 251,
        252, 253, 254, 254, 255, 255, 255, 256, 256, 256, 256 };
    if ((u32)deg >= 360u) {         // Thumb code has no fast divide: skip % for the usual angles
        if (deg >= 360 && deg < 720) deg -= 360;
        else if (deg < 0 && deg >= -360) deg += 360;
        else { deg %= 360; if (deg < 0) deg += 360; }
    }
    if (deg <= 90)  return quarter[deg];
    if (deg <= 180) return quarter[180 - deg];
    if (deg <= 270) return -quarter[deg - 180];
    return -quarter[360 - deg];
}

// Cosine of an angle in whole degrees, as fixed.
static inline fixed fx_cos_deg(s32 deg) { return fx_sin_deg(deg + 90); }

// Integer square root.
static inline u32 fx_isqrt(u32 n) {
    u32 r = 0, bit = 1u << 30;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= r + bit) { n -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

// Next pseudo-random 32-bit number (xorshift). State lives in core.c.
u32 rng_next(void);
// Random whole number in [lo, hi] (inclusive).
s32 rng_range(s32 lo, s32 hi);

#endif
