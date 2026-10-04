/*
 * Minimal PNG writer: each row gets the PNG filter that suits it best, then
 * the image is deflated with LZ77 matches and the fixed Huffman codes. The
 * game's flat-shaded pictures compress well that way, and pd_tool needs no
 * zlib.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "png_write.h"

/* ------------------------------------------------------------------ */
/* Bit output (deflate packs bits starting with the least significant)  */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t *buf;
    size_t len, cap;
    uint32_t bits;
    int nbits;
} BitOut;

static void put_byte(BitOut *o, uint8_t b)
{
    if (o->len == o->cap) {
        o->cap = o->cap ? o->cap * 2 : 65536;
        o->buf = realloc(o->buf, o->cap);
    }
    o->buf[o->len++] = b;
}

static void put_bits(BitOut *o, uint32_t v, int n)
{
    o->bits |= v << o->nbits;
    o->nbits += n;
    while (o->nbits >= 8) {
        put_byte(o, (uint8_t)o->bits);
        o->bits >>= 8;
        o->nbits -= 8;
    }
}

/* Huffman codes go out most significant bit first. */
static void put_code(BitOut *o, uint32_t code, int n)
{
    uint32_t r = 0;
    for (int i = 0; i < n; i++) r |= ((code >> i) & 1u) << (n - 1 - i);
    put_bits(o, r, n);
}

static void put_litlen(BitOut *o, int v)
{
    if (v < 144) put_code(o, 0x30 + v, 8);
    else if (v < 256) put_code(o, 0x190 + v - 144, 9);
    else if (v < 280) put_code(o, v - 256, 7);
    else put_code(o, 0xC0 + v - 280, 8);
}

static const int LEN_BASE[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27,
                                 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const int LEN_EXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                  2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const int DIST_BASE[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                  193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const int DIST_EXTRA[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                   6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static void put_match(BitOut *o, int len, int dist)
{
    int i = 28;
    while (LEN_BASE[i] > len) i--;
    put_litlen(o, 257 + i);
    put_bits(o, (uint32_t)(len - LEN_BASE[i]), LEN_EXTRA[i]);
    int d = 29;
    while (DIST_BASE[d] > dist) d--;
    put_code(o, (uint32_t)d, 5);
    put_bits(o, (uint32_t)(dist - DIST_BASE[d]), DIST_EXTRA[d]);
}

/* ------------------------------------------------------------------ */
/* zlib stream: one fixed-Huffman block                                 */
/* ------------------------------------------------------------------ */

#define WINDOW 32768
#define MAX_MATCH 258
#define HASH_BITS 15
#define MAX_CHAIN 64

static void deflate_zlib(BitOut *o, const uint8_t *d, size_t n)
{
    put_byte(o, 0x78); /* deflate, 32K window */
    put_byte(o, 0x01);
    put_bits(o, 1, 1); /* final block */
    put_bits(o, 1, 2); /* fixed Huffman codes */

    int32_t *head = malloc(sizeof(int32_t) << HASH_BITS);
    int32_t *prev = malloc(sizeof(int32_t) * (n ? n : 1));
    for (int i = 0; i < 1 << HASH_BITS; i++) head[i] = -1;
#define HASH(p) ((((uint32_t)d[p] << 10) ^ ((uint32_t)d[(p) + 1] << 5) ^ d[(p) + 2]) & ((1u << HASH_BITS) - 1))
    size_t i = 0;
    while (i < n) {
        int best = 0, best_dist = 0;
        if (i + 3 <= n) {
            uint32_t h = HASH(i);
            int chain = 0;
            for (int32_t c = head[h]; c >= 0 && i - (size_t)c <= WINDOW && chain < MAX_CHAIN; c = prev[c], chain++) {
                int len = 0, lim = (int)(n - i < MAX_MATCH ? n - i : MAX_MATCH);
                while (len < lim && d[c + len] == d[i + len]) len++;
                if (len > best) {
                    best = len;
                    best_dist = (int)(i - (size_t)c);
                    if (len == lim) break;
                }
            }
        }
        size_t step = best >= 3 ? (size_t)best : 1;
        if (best >= 3) put_match(o, best, best_dist);
        else put_litlen(o, d[i]);
        for (size_t k = 0; k < step; k++, i++) {
            if (i + 3 <= n) {
                uint32_t h = HASH(i);
                prev[i] = head[h];
                head[h] = (int32_t)i;
            }
        }
    }
#undef HASH
    free(head);
    free(prev);
    put_litlen(o, 256);
    if (o->nbits) put_bits(o, 0, 8 - o->nbits);

    uint32_t a = 1, b = 0;
    for (size_t k = 0; k < n; k++) {
        a = (a + d[k]) % 65521u;
        b = (b + a) % 65521u;
    }
    uint32_t adler = (b << 16) | a;
    for (int s = 24; s >= 0; s -= 8) put_byte(o, (uint8_t)(adler >> s));
}

/* ------------------------------------------------------------------ */
/* PNG                                                                  */
/* ------------------------------------------------------------------ */

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n)
{
    static uint32_t table[256];
    if (!table[1]) {
        for (uint32_t k = 0; k < 256; k++) {
            uint32_t c = k;
            for (int j = 0; j < 8; j++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[k] = c;
        }
    }
    crc = ~crc;
    while (n--) crc = table[(crc ^ *p++) & 255] ^ (crc >> 8);
    return ~crc;
}

static void put_be32(FILE *f, uint32_t v)
{
    uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    fwrite(b, 1, 4, f);
}

static void put_chunk(FILE *f, const char *type, const uint8_t *data, size_t n)
{
    put_be32(f, (uint32_t)n);
    fwrite(type, 1, 4, f);
    if (n) fwrite(data, 1, n, f);
    uint32_t crc = crc32_update(0, (const uint8_t *)type, 4);
    put_be32(f, crc32_update(crc, data, n));
}

static int paeth(int a, int b, int c)
{
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return pa <= pb && pa <= pc ? a : (pb <= pc ? b : c);
}

/* Filter one row with filter type t (prev = the row above, NULL for none). */
static void filter_row(int t, const uint8_t *row, const uint8_t *prev, int n, uint8_t *out)
{
    for (int x = 0; x < n; x++) {
        int a = x >= 3 ? row[x - 3] : 0, b = prev ? prev[x] : 0, c = prev && x >= 3 ? prev[x - 3] : 0;
        int p = t == 1 ? a : t == 2 ? b : t == 3 ? (a + b) / 2 : t == 4 ? paeth(a, b, c) : 0;
        out[x] = (uint8_t)(row[x] - p);
    }
}

int png_write_rgb(const char *path, const uint8_t *rgb, int w, int h)
{
    int stride = w * 3;
    size_t raw_n = (size_t)(stride + 1) * (size_t)h;
    uint8_t *raw = malloc(raw_n), *tmp = malloc((size_t)stride);
    for (int y = 0; y < h; y++) {
        const uint8_t *row = rgb + (size_t)y * stride, *prev = y ? row - stride : NULL;
        uint8_t *dst = raw + (size_t)y * (stride + 1);
        long best_cost = -1;
        for (int t = 0; t < 5; t++) {
            filter_row(t, row, prev, stride, tmp);
            long cost = 0;
            for (int x = 0; x < stride; x++) cost += abs((int8_t)tmp[x]);
            if (best_cost < 0 || cost < best_cost) {
                best_cost = cost;
                dst[0] = (uint8_t)t;
                memcpy(dst + 1, tmp, (size_t)stride);
            }
        }
    }
    BitOut z = {0};
    deflate_zlib(&z, raw, raw_n);
    free(raw);
    free(tmp);

    FILE *f = fopen(path, "wb");
    if (!f) {
        free(z.buf);
        return -1;
    }
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13] = {(uint8_t)(w >> 24), (uint8_t)(w >> 16), (uint8_t)(w >> 8), (uint8_t)w,
                        (uint8_t)(h >> 24), (uint8_t)(h >> 16), (uint8_t)(h >> 8), (uint8_t)h,
                        8, 2, 0, 0, 0}; /* 8-bit RGB, deflate, adaptive filters, no interlace */
    put_chunk(f, "IHDR", ihdr, sizeof(ihdr));
    put_chunk(f, "IDAT", z.buf, z.len);
    put_chunk(f, "IEND", NULL, 0);
    free(z.buf);
    return fclose(f) == 0 ? 0 : -1;
}
