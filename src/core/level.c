#include "level.h"

#include <stdlib.h>
#include <stdio.h>

static const char *const DIFF_NAMES[] = {"EASY", "NORMAL", "HARD", "HARDER", "INSANE", "DEMON"};

const char *difficulty_name(int d)
{
    return DIFF_NAMES[clampi(d, 0, 5)];
}

static int char_to_obj(char ch)
{
    switch (ch) {
    case '#': return OBJ_BLOCK;
    case '_': return OBJ_SLAB_LO;
    case '=': return OBJ_SLAB_HI;
    case '^': return OBJ_SPIKE_UP;
    case 'v': return OBJ_SPIKE_DOWN;
    case ',': return OBJ_SPIKE_SM_UP;
    case '`': return OBJ_SPIKE_SM_DOWN;
    case '*': return OBJ_SAW_BIG;
    case '@': return OBJ_SAW_SMALL;
    case 'y': return OBJ_ORB_YELLOW;
    case 'p': return OBJ_ORB_PINK;
    case 'b': return OBJ_ORB_BLUE;
    case 'g': return OBJ_ORB_GREEN;
    case 'Y': return OBJ_PAD_YELLOW;
    case 'P': return OBJ_PAD_PINK;
    case 'B': return OBJ_PAD_BLUE;
    case 'C': return OBJ_PORTAL_CUBE;
    case 'S': return OBJ_PORTAL_SHIP;
    case 'A': return OBJ_PORTAL_BALL;
    case 'U': return OBJ_PORTAL_UFO;
    case 'W': return OBJ_PORTAL_WAVE;
    case 'G': return OBJ_PORTAL_GRAV_FLIP;
    case 'N': return OBJ_PORTAL_GRAV_NORMAL;
    case '0': return OBJ_SPEED_0;
    case '1': return OBJ_SPEED_1;
    case '2': return OBJ_SPEED_2;
    case '3': return OBJ_SPEED_3;
    case '$': return OBJ_COIN;
    default: return OBJ_NONE;
    }
}

static int is_solid_type(int t)
{
    return t == OBJ_BLOCK || t == OBJ_SLAB_LO || t == OBJ_SLAB_HI;
}

static int is_interactable(int t)
{
    return t >= OBJ_ORB_YELLOW && t <= OBJ_COIN;
}

static int header_int(const char *line, const char *key, int *out)
{
    size_t n = strlen(key);
    if (strncmp(line, key, n) != 0 || (line[n] != ' ' && line[n] != '\t')) return 0;
    *out = atoi(line + n + 1);
    return 1;
}

static int header_str(const char *line, const char *key, char *out, size_t cap)
{
    size_t n = strlen(key);
    if (strncmp(line, key, n) != 0 || (line[n] != ' ' && line[n] != '\t')) return 0;
    const char *v = line + n + 1;
    while (*v == ' ' || *v == '\t') v++;
    strncpy(out, v, cap - 1);
    out[cap - 1] = 0;
    return 1;
}

typedef struct {
    int first;  /* index of first line */
    int count;  /* number of lines (rows + trigger rows) */
    int rows;   /* content rows */
    int width;
} Section;

#define MAX_SECTIONS 512

/* Split the source into sections: maximal runs of '|' / '!' lines. */
static int find_sections(const char *const *src, Section *sec)
{
    int n = 0;
    for (int i = 0; src[i];) {
        if (src[i][0] != '|' && src[i][0] != '!') { i++; continue; }
        Section s = {i, 0, 0, 0};
        while (src[i] && (src[i][0] == '|' || src[i][0] == '!')) {
            int w = (int)strlen(src[i]) - 1;
            if (w > s.width) s.width = w;
            if (src[i][0] == '|') s.rows++;
            s.count++;
            i++;
        }
        if (n < MAX_SECTIONS) sec[n++] = s;
    }
    return n;
}

static void parse_header(const char *const *src, Level *L)
{
    for (int i = 0; src[i]; i++) {
        const char *ln = src[i];
        if (ln[0] != '#') continue;
        ln++;
        if (header_str(ln, "name", L->name, sizeof(L->name))) continue;
        if (header_str(ln, "author", L->author, sizeof(L->author))) continue;
        if (header_int(ln, "song", &L->song)) continue;
        if (header_int(ln, "diff", &L->difficulty)) continue;
        if (header_int(ln, "stars", &L->stars)) continue;
        if (header_int(ln, "speed", &L->start_speed)) continue;
        if (header_int(ln, "pal", &L->start_pal)) continue;
    }
}

void level_info(int index, LevelInfo *out)
{
    Level tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.start_speed = 1;
    parse_header(g_levels[index].src, &tmp);
    memset(out, 0, sizeof(*out));
    memcpy(out->name, tmp.name, sizeof(out->name));
    out->difficulty = tmp.difficulty;
    out->stars = tmp.stars;
    out->song = tmp.song;
    out->pal = tmp.start_pal;
    int coins = 0;
    for (int i = 0; g_levels[index].src[i]; i++) {
        const char *ln = g_levels[index].src[i];
        if (ln[0] != '|') continue;
        for (const char *p = ln + 1; *p; p++)
            if (*p == '$') coins++;
    }
    out->ncoins = coins;
}

Level *level_parse(const char *const *src)
{
    static Section sec[MAX_SECTIONS];
    Level *L = (Level *)calloc(1, sizeof(Level));
    if (!L) return NULL;
    L->start_speed = 1;
    parse_header(src, L);

    int nsec = find_sections(src, sec);
    int width = 0, height = 1, nobj = 0;
    for (int s = 0; s < nsec; s++) {
        width += sec[s].width;
        if (sec[s].rows > height) height = sec[s].rows;
        for (int k = 0; k < sec[s].count; k++) {
            const char *ln = src[sec[s].first + k];
            if (ln[0] != '|') continue;
            for (const char *p = ln + 1; *p; p++) {
                int t = char_to_obj(*p);
                if (t != OBJ_NONE && !is_solid_type(t)) nobj++;
            }
        }
    }
    height += 2; /* headroom so lookups above the art stay in range */
    if (width < 1) width = 1;

    L->width = width;
    L->height = height;
    L->grid = (uint8_t *)calloc((size_t)width * height, 1);
    L->edges = (uint8_t *)calloc((size_t)width * height, 1);
    L->objs = (LevelObj *)calloc((size_t)(nobj > 0 ? nobj : 1), sizeof(LevelObj));
    L->col_start = (int *)calloc((size_t)width + 1, sizeof(int));
    if (!L->grid || !L->edges || !L->objs || !L->col_start) {
        level_free(L);
        return NULL;
    }

    /* Fill grid and collect objects column by column so objs end up sorted. */
    int x0 = 0, n = 0, coin_index = 0;
    for (int s = 0; s < nsec; s++) {
        const Section *S = &sec[s];
        for (int c = 0; c < S->width; c++) {
            int gx = x0 + c;
            L->col_start[gx] = n;
            int row = S->rows; /* row counter from the top */
            for (int k = 0; k < S->count; k++) {
                const char *ln = src[S->first + k];
                int len = (int)strlen(ln) - 1;
                char ch = c < len ? ln[1 + c] : ' ';
                if (ln[0] == '!') {
                    if (ch >= '0' && ch <= '9' && L->ntrig < LEVEL_MAX_TRIGGERS) {
                        L->trig[L->ntrig].x = (float)gx;
                        L->trig[L->ntrig].pal = (uint8_t)(ch - '0');
                        L->ntrig++;
                    }
                    continue;
                }
                row--;
                int t = char_to_obj(ch);
                if (t == OBJ_NONE) continue;
                if (is_solid_type(t)) {
                    L->grid[row * width + gx] = (uint8_t)t;
                } else {
                    LevelObj *o = &L->objs[n++];
                    o->type = (uint8_t)t;
                    o->cx = (int16_t)gx;
                    o->cy = (int16_t)row;
                    if (is_interactable(t) && L->ninteract < LEVEL_MAX_INTERACT)
                        o->id = (uint16_t)L->ninteract++;
                    if (t == OBJ_COIN) {
                        o->flags = (uint8_t)((coin_index & 3) << 4);
                        coin_index++;
                    }
                }
            }
        }
        x0 += S->width;
    }
    L->col_start[width] = n;
    L->nobjs = n;
    L->ncoins = coin_index;
    L->end_x = (float)width;

    /* Pads stick to a ceiling when there is a solid above and nothing below. */
    for (int i = 0; i < n; i++) {
        LevelObj *o = &L->objs[i];
        if (o->type >= OBJ_PAD_YELLOW && o->type <= OBJ_PAD_BLUE) {
            int below = o->cy == 0 || level_solid_at(L, o->cx, o->cy - 1);
            int above = level_solid_at(L, o->cx, o->cy + 1);
            if (above && !below) o->flags |= OF_CEILING;
        }
    }

    /* Exposed edges for block outlines. */
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int t = L->grid[y * width + x];
            if (!t) continue;
            uint8_t e = 0;
            if (t == OBJ_BLOCK) {
                if (level_solid_at(L, x - 1, y) != OBJ_BLOCK) e |= EDGE_L;
                if (level_solid_at(L, x + 1, y) != OBJ_BLOCK) e |= EDGE_R;
                if (level_solid_at(L, x, y + 1) != OBJ_BLOCK) e |= EDGE_T;
                if (y > 0 && level_solid_at(L, x, y - 1) != OBJ_BLOCK) e |= EDGE_B;
            } else {
                if (level_solid_at(L, x - 1, y) != t) e |= EDGE_L;
                if (level_solid_at(L, x + 1, y) != t) e |= EDGE_R;
                e |= EDGE_T | EDGE_B;
            }
            L->edges[y * width + x] = e;
        }
    }
    return L;
}

void level_free(Level *L)
{
    if (!L) return;
    free(L->grid);
    free(L->edges);
    free(L->objs);
    free(L->col_start);
    free(L);
}
