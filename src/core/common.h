/*
 * Pulse Dash - shared types and helpers used by every module.
 *
 * Coordinates: the game draws into a virtual 640x448 screen (NTSC-sized);
 * each backend scales that to its real framebuffer. World space is measured
 * in blocks, with x growing to the right and y growing upwards from the
 * ground surface (y = 0).
 */
#ifndef PD_COMMON_H
#define PD_COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

#define GAME_TITLE "PULSE DASH"

/*
 * Target profile. The default is the PS2's (the PC build plays the same):
 * a 640x448 virtual screen. PD_PSP selects the PSP's: its 480x272 screen is
 * wider, so the virtual screen is too (levels show more of what is ahead,
 * menus laid out for 640 stay centred through UI_X), text, icons and thin
 * lines are drawn on whole pixels of the PSP's screen so they stay sharp
 * and even when scaled down (the pixel grid, draw.h), and help texts name
 * the PSP's buttons.
 */
#ifdef PD_PSP
#define SCREEN_W 790 /* 448 * 480 / 272, rounded down */
#define TARGET_NAME "PSP"
#define BTN_NAME_L "L"
#define BTN_NAME_R "R"
/* device pixels per virtual pixel, across and down: the 790 virtual pixels
 * across come to 479.6 of the LCD's 480 */
#define PIXEL_GRID (272.0f / SCREEN_H)
#else
#define SCREEN_W 640
#define TARGET_NAME "PLAYSTATION 2"
#define BTN_NAME_L "L1"
#define BTN_NAME_R "R1"
#endif
#define SCREEN_H 448

/* x in a layout drawn for a 640-wide screen, centred on this one */
#define UI_X(x) ((x) + (SCREEN_W - 640) / 2)

#define TICK_HZ 60
#define TICK_DT (1.0f / TICK_HZ)

/* Size of one world block in virtual pixels. */
#define BLOCK_PX 34.0f

#ifndef PI
#define PI 3.14159265358979f
#endif

/* Colors are packed 0xAARRGGBB with 8-bit alpha (255 = opaque). */
typedef uint32_t Color;

#define RGBA(r, g, b, a) \
    ((Color)((((uint32_t)(a) & 255u) << 24) | (((uint32_t)(r) & 255u) << 16) | \
             (((uint32_t)(g) & 255u) << 8) | ((uint32_t)(b) & 255u)))
#define RGB(r, g, b) RGBA(r, g, b, 255)
#define COL_A(c) (((c) >> 24) & 255u)
#define COL_R(c) (((c) >> 16) & 255u)
#define COL_G(c) (((c) >> 8) & 255u)
#define COL_B(c) ((c) & 255u)

#define COL_WHITE RGB(255, 255, 255)
#define COL_BLACK RGB(0, 0, 0)

/* Logical controller buttons (PS2 layout; the PC build maps keys onto these). */
enum {
    BTN_CROSS = 1u << 0,
    BTN_CIRCLE = 1u << 1,
    BTN_SQUARE = 1u << 2,
    BTN_TRIANGLE = 1u << 3,
    BTN_UP = 1u << 4,
    BTN_DOWN = 1u << 5,
    BTN_LEFT = 1u << 6,
    BTN_RIGHT = 1u << 7,
    BTN_L1 = 1u << 8,
    BTN_R1 = 1u << 9,
    BTN_L2 = 1u << 10,
    BTN_R2 = 1u << 11,
    BTN_START = 1u << 12,
    BTN_SELECT = 1u << 13
};

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float minf(float a, float b) { return a < b ? a : b; }
static inline float maxf(float a, float b) { return a > b ? a : b; }
static inline int mini(int a, int b) { return a < b ? a : b; }
static inline int maxi(int a, int b) { return a > b ? a : b; }
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

/* Move v towards target by at most step. */
static inline float approachf(float v, float target, float step)
{
    if (v < target) return v + step < target ? v + step : target;
    return v - step > target ? v - step : target;
}

static inline float smoothstepf(float t)
{
    t = clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static inline float ease_out_back(float t)
{
    const float c1 = 1.70158f, c3 = c1 + 1.0f;
    t -= 1.0f;
    return 1.0f + c3 * t * t * t + c1 * t * t;
}

static inline Color col_with_alpha(Color c, float a)
{
    int na = (int)(COL_A(c) * clampf(a, 0.0f, 1.0f) + 0.5f);
    return (c & 0x00FFFFFFu) | ((uint32_t)na << 24);
}

static inline Color col_lerp(Color a, Color b, float t)
{
    t = clampf(t, 0.0f, 1.0f);
    return RGBA((int)(COL_R(a) + ((int)COL_R(b) - (int)COL_R(a)) * t),
                (int)(COL_G(a) + ((int)COL_G(b) - (int)COL_G(a)) * t),
                (int)(COL_B(a) + ((int)COL_B(b) - (int)COL_B(a)) * t),
                (int)(COL_A(a) + ((int)COL_A(b) - (int)COL_A(a)) * t));
}

/* Scale brightness; k > 1 brightens (clamped), k < 1 darkens. */
static inline Color col_scale(Color c, float k)
{
    int r = (int)(COL_R(c) * k), g = (int)(COL_G(c) * k), b = (int)(COL_B(c) * k);
    return RGBA(clampi(r, 0, 255), clampi(g, 0, 255), clampi(b, 0, 255), COL_A(c));
}

/* Small deterministic hash used for procedural decoration. */
static inline uint32_t hash_u32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static inline float hash_f01(uint32_t x)
{
    return (hash_u32(x) & 0xFFFFFF) / 16777216.0f;
}

#endif
