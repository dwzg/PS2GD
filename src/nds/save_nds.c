/* placeholder */
#include "nds_platform.h"
#include "../core/platform.h"

int save_nds_init(int argc, char **argv) { (void)argc; (void)argv; return 0; }
int plat_save_read(void *buf, int size) { (void)buf; (void)size; return -1; }
int plat_save_write(const void *buf, int size) { (void)buf; (void)size; return 0; }
const char *plat_name(void) { return "DS"; }
