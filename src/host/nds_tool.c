/*
 * nds_tool - makes the Nintendo DS ROM's data from the game itself
 * (Makefile.nds runs it).
 *
 *   nds_tool audio <dir> <gen_dir>
 *                          record every song and sound effect with the
 *                          game's synthesizer into dir (the ROM's NitroFS):
 *                          music_hp.bin, music_spk.bin, sfx.bin; and the
 *                          songs' names, tempos and lengths into gen_dir
 *                          (gen_nds.h, gen_nds.c) (nds_audio.c)
 */
#include <stdio.h>
#include <string.h>

#include "../core/audio.h"

int nds_audio_export(const char *dir, const char *gen_dir);

int main(int argc, char **argv)
{
    if (argc == 4 && !strcmp(argv[1], "audio")) {
        audio_init();
        return nds_audio_export(argv[2], argv[3]);
    }
    fprintf(stderr, "usage: nds_tool audio <dir> <gen_dir>\n");
    return 2;
}
