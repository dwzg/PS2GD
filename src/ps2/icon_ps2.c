/*
 * Generates the memory card browser icon: a textured cube wearing the
 * default "CORE" face. Format: PS2 icon (.ico/.icn) with one shape, one
 * static animation frame and an uncompressed 128x128 A1B5G5R5 texture.
 * Layout checked against the icon parsers in Play! (saves/Icon.cpp) and
 * ps2iconsys (ps2_ps2icon.hpp).
 */
#include <string.h>

#include "ps2_platform.h"

static uint8_t *s_p;
static int s_left;

static void put_bytes(const void *d, int n)
{
    if (s_left < n) {
        s_left = -1;
        return;
    }
    memcpy(s_p, d, (size_t)n);
    s_p += n;
    s_left -= n;
}

static void put_u32(uint32_t v) { put_bytes(&v, 4); }
static void put_f32(float v) { put_bytes(&v, 4); }
static void put_s16(int v)
{
    int16_t s = (int16_t)v;
    put_bytes(&s, 2);
}

static uint16_t tex_px(int x, int y)
{
    /* yellow border, dark ring, cyan square, yellow core */
    int d = x < y ? x : y;
    int e = (127 - x) < (127 - y) ? (127 - x) : (127 - y);
    int m = d < e ? d : e; /* distance to nearest edge */
    int r, g, b;
    if (m < 6) { r = 2; g = 2; b = 3; }
    else if (m < 30) { r = 31; g = 25; b = 0; }
    else if (m < 36) { r = 2; g = 2; b = 3; }
    else if (m < 50) { r = 0; g = 27; b = 31; }
    else { r = 31; g = 25; b = 0; }
    return (uint16_t)(0x8000 | (b << 10) | (g << 5) | r);
}

int icon_ps2_build(uint8_t *buf, int cap)
{
    s_p = buf;
    s_left = cap;

    /* 6 faces x 2 triangles; cube spans x,z in [-1,1], y in [-2,0] (y points down) */
    static const float face_n[6][3] = {{0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}};
    static const float face_u[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, -1}, {0, 0, 1}, {1, 0, 0}, {1, 0, 0}};
    static const float face_v[6][3] = {{0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, -1}};
    static const float corner[6][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};

    put_u32(0x00010000u); /* id */
    put_u32(1);           /* animation shapes */
    put_u32(0x07);        /* uncompressed texture */
    put_u32(0x3F800000u); /* 1.0f */
    put_u32(36);          /* vertices */

    for (int f = 0; f < 6; f++) {
        for (int k = 0; k < 6; k++) {
            float a = corner[k][0], b = corner[k][1];
            float x = face_n[f][0] + face_u[f][0] * a + face_v[f][0] * b;
            float y = face_n[f][1] + face_u[f][1] * a + face_v[f][1] * b;
            float z = face_n[f][2] + face_u[f][2] * a + face_v[f][2] * b;
            y -= 1.0f; /* sit on the floor */
            put_s16((int)(x * 4096.0f));
            put_s16((int)(y * 4096.0f));
            put_s16((int)(z * 4096.0f));
            put_s16(0);
            put_s16((int)(face_n[f][0] * 4096.0f));
            put_s16((int)(face_n[f][1] * 4096.0f));
            put_s16((int)(face_n[f][2] * 4096.0f));
            put_s16(0);
            put_s16((int)((a * 0.5f + 0.5f) * 4096.0f));
            put_s16((int)((b * 0.5f + 0.5f) * 4096.0f));
            uint8_t col[4] = {0x80, 0x80, 0x80, 0x80};
            put_bytes(col, 4);
        }
    }

    /* animation header + one static frame (same defaults as ps2iconsys) */
    put_u32(0x01); /* id */
    put_u32(31);   /* frame length */
    put_f32(1.0f); /* speed */
    put_u32(0);    /* play offset */
    put_u32(1);    /* frames */
    put_u32(0);    /* frame 0: shape id */
    put_u32(1);    /*          key count */
    put_f32(0.0f); /*          key time */
    put_f32(1.0f); /*          key value */

    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 128; x++) {
            uint16_t px = tex_px(x, y);
            put_bytes(&px, 2);
        }
    return s_left < 0 ? -1 : (int)(s_p - buf);
}
