/*
 * What gba_tool writes into build/gba/gen for the Game Boy Advance ROM:
 * binary blobs (graphics, sound), each taken into the ROM by data.s with
 * .incbin under a global symbol, and C sources/headers with the tables that
 * describe them. The exporters (gba_art.c, gba_audio.c) add to one GbaGen.
 */
#ifndef PD_GBA_GEN_H
#define PD_GBA_GEN_H

#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

typedef struct {
    const char *dir; /* output directory */
    FILE *data_s;    /* data.s: the blobs, in the ROM's .rodata */
    FILE *hdr;       /* gen.h: declarations of everything generated */
    FILE *src;       /* gen.c: generated tables (C) */
    size_t blob_bytes;
} GbaGen;

/* Write data to <dir>/<name>.bin and add it to data.s as a 4-byte aligned
 * global array `name`; declares `extern const <ctype> name[];` in gen.h
 * (ctype: "uint8_t", "uint16_t", "uint32_t", "int8_t"...). */
void gba_gen_blob(GbaGen *g, const char *name, const char *ctype, const void *data, size_t bytes);

/* Exporters */
int gba_audio_export(GbaGen *g);
int gba_art_export(GbaGen *g);
/* (after gba_art_export: it puts the levels' tiles together from its cells) */
int gba_levels_export(GbaGen *g);
const uint16_t *gba_art_cells(void);
const uint16_t *gba_art_glow(void);

#endif
