/* Minimal PNG writer for pd_tool (no zlib needed). */
#ifndef PD_PNG_WRITE_H
#define PD_PNG_WRITE_H

#include <stdint.h>

/* Write w x h pixels of 8-bit RGB (rows top to bottom, 3 bytes a pixel) as a
 * PNG file. Returns 0 on success. */
int png_write_rgb(const char *path, const uint8_t *rgb, int w, int h);

#endif
