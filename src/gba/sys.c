/*
 * What newlib needs from the system: the heap for malloc (the core parses
 * levels into it), between the end of EWRAM's data and its end (gba.ld).
 * Nothing else of the C library's system interface is used.
 */
#include <errno.h>
#include <stddef.h>

extern char __heap_start[], __heap_end[];

void *_sbrk(ptrdiff_t n)
{
    static char *brk = __heap_start;
    char *p = brk;
    if (n > __heap_end - brk || n < __heap_start - brk) {
        errno = ENOMEM;
        return (void *)-1;
    }
    brk += n;
    return p;
}
