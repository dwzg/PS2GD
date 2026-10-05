/*
 * The pictures gba_tool draws for the Game Boy Advance and the ROM uses
 * (gen.h has the data): their ids and layouts, shared by both.
 */
#ifndef PD_GBA_ART_H
#define PD_GBA_ART_H

/* The level is drawn 12 pixels to a block: a block is 3x3 quarters of a
 * tile (4x4 pixels), so a tile always shows whole quarters of up to 4
 * cells, and any tile can be put together from the cells' pictures. */
#define BLOCK_PIX 12

/*
 * Colours 0..HDMA_COLORS-1 of the world palette are set again on every
 * scanline (by DMA 0 in the horizontal blank): the backdrop's gradient and
 * everything drawn see-through over it, mixed with it line by line.
 */
#define HDMA_COLORS 10 /* (video.h; even: DMA 0 copies words) */
enum {
    WC_BACKDROP = 0,  /* the gradient */
    WC_SQ_FILL,       /* the squares behind, filled and outlined */
    WC_SQ_EDGE,
    WC_FILL,          /* block fill (see-through on the PC), and its lighter top */
    WC_FILL_TOP,
    WC_DECOR,         /* the inner square some blocks have */
    WC_BAND,          /* corridor floor/ceiling band, fading in and out */
    WC_BAND_LINE,
    WC_HALO,          /* the glow around blocks and spikes: the pixel next to */
    WC_HALO2,         /* an edge, and the one after it (lit on the beat) */
    /* per frame */
    WC_EDGE = HDMA_COLORS, /* block and spike outlines, lit on the beat */
    WC_SPIKE,         /* spike body, darkest at the base */
    WC_SPIKE_MID,
    WC_SPIKE_TIP,
    WC_GLOW,          /* the ground line's glow over the playfield */
    WC_GLOW2,
};

/*
 * The glow around blocks (render_level: 8 of the PC's 34 pixels beyond an
 * exposed edge, the 2 pixels next to it here) falls into the cells beside
 * them: a cell's glow is a mask of where it comes from, G_* (on screen:
 * top is the cell above), and g_glow[256 * 36] holds each mask's picture,
 * laid out as a cell's (WC_HALO and WC_HALO2 where it glows, 0 where not),
 * shown where the cell's own picture has nothing. The slabs' and spikes'
 * glow inside their own cells is in their pictures.
 */
enum {
    G_TOP = 1, G_BOT = 2, G_LEFT = 4, G_RIGHT = 8, /* a side of a block beside it */
    G_TL = 16, G_TR = 32, G_BL = 64, G_BR = 128,   /* a corner of one diagonally */
};

/*
 * A level's tiles, made by gba_tool (src/host/gba_levels.c), in
 * g_level_art[] in the order of g_levels, the title's demo run last: its
 * world in 8x8 tiles (src/gba/cellmap.c), each different one once.
 */
typedef struct {
    const uint32_t *tiles; /* ntiles tiles, 8 words each */
    int ntiles;
    /* tw x th tiles: x from the level's start, row 0 the one over the
     * ground's line and up from there; each a tile's index, or
     * LEVEL_EMPTY */
    const uint16_t *map;
    int tw, th;
} LevelArt;
#define LEVEL_EMPTY 0xFFFF

/* screen entry flips (gba.h) */
#define ART_HFLIP 0x0400
#define ART_VFLIP 0x0800

/*
 * A cell's picture: 3x3 quarters, each 4 rows of 4 pixels (16 bits a row,
 * the leftmost pixel in the low 4 bits), in the world palette's colours
 * (video.h WC_*). g_cells[CELL_COUNT * 36].
 */
enum {
    CELL_EMPTY = 0,
    CELL_BLOCK = 1,        /* + the exposed edges (level.h EDGE_*, 0..15) */
    CELL_BLOCK_DECOR = 17, /* the same with the inner square some blocks have */
    CELL_SLAB_LO = 33,     /* + 1 if its left end is exposed, + 2 its right */
    CELL_SLAB_HI = 37,
    CELL_SPIKE_UP = 41,
    CELL_SPIKE_DOWN,
    CELL_SPIKE_SM_UP,
    CELL_SPIKE_SM_DOWN,
    CELL_COUNT
};

/*
 * The ground and the corridor's bands (BG2): tiles g_ground_tiles[GT_COUNT
 * * 8] (8 words each). The ground's tiles use the ground palette (video.h
 * PAL_GROUND, colours GC_*), the glow and the bands the world palette.
 */
enum {
    GT_EMPTY = 0,
    GT_GROUND,          /* + depth (0: the top row, with the line) * 2 + separator */
    GT_GLOW = GT_GROUND + 10, /* the tile row over the ground: the line's glow */
    GT_BAND,            /* inside a band */
    GT_CEIL_7,          /* a ceiling band's line in the tile's row 7, or 3 */
    GT_CEIL_3,
    GT_FLOOR_0,         /* a floor band's line in the tile's row 0, or 4 */
    GT_FLOOR_4,
    GT_COUNT
};
#define GROUND_DEPTHS 5

/* the ground palette */
enum {
    GC_SHADE = 1,       /* 1..5: by depth */
    GC_SEP = 6,         /* 6..10: the separators every 4 blocks, by depth */
    GC_LINE = 11,
};

/* The squares behind (BG3): a 512x256 picture made of g_sq_tiles
 * (deduplicated, 8 words each) by the 64x32 map g_sq_map; the ground is at
 * its bottom row, and it repeats across. */
#define SQ_MAP_W 64
#define SQ_MAP_H 32

/*
 * Sprites. OBJ VRAM tiles 0..127 hold the player's vehicles (the chosen
 * icon's, and the garage's cubes), 128.. the sprites that never change
 * (g_obj_tiles: OT_* in gen.h are their first tiles), 768.. text drawn into
 * sprites.
 */
#define OBJ_PLAYER_TILE 0
#define OBJ_STATIC_TILE 128
#define OBJ_TEXT_TILE 768

/* OBJ palette banks (OP_* in gen.h name the static ones) */
enum {
    OBJ_PAL_PLAYER = 0, /* the garage's colours: PC_* */
    OBJ_PAL_TEXT,       /* text in sprites */
    OBJ_PAL_ORB,        /* 2..5: yellow, pink, blue, green orbs and pads */
    OBJ_PAL_PORTAL = 6, /* 6..12: the portals' colours */
    OBJ_PAL_SPEED = 13, /* speed portals and coins */
    OBJ_PAL_MISC,       /* saws, checkpoints, the finish line */
    OBJ_PAL_FX,         /* particles and glows, set every frame */
};

/*
 * Pictures drawn turned at build time, rather than turned by the hardware
 * (which picks the nearest pixel: a small round picture turned so comes
 * out lumpy): each frame's tiles are copied to the picture's place in
 * VRAM as its angle changes. The player's vehicles, g_player_frames
 * [ICON_COUNT][VF_TILES] (VFT_* the first tile of each vehicle's frames):
 * the cube and the ball a whole turn round in VF_CUBE and VF_BALL frames,
 * the ship and the wave from straight up to straight down (-pi/2..pi/2) in
 * VF_SHIP and VF_WAVE, the UFO its tilt (-VF_UFO_TILT..VF_UFO_TILT) in
 * VF_UFO; upside down, the hardware mirrors the frame of the opposite
 * angle. The saws (12 teeth: the same every 30 degrees), SAW_FRAMES each
 * (g_saw_frames: the big one's, then the small one's), the title's cog
 * (COG_FRAMES, g_cog_frames) and the orbs' four dashes (the same every
 * quarter turn: ORB_FRAMES, g_orb_frames). Angles clockwise on the
 * screen.
 */
enum { VF_CUBE = 64, VF_BALL = 64, VF_SHIP = 33, VF_UFO = 13, VF_WAVE = 33 };
enum {
    VFT_CUBE = 0,
    VFT_BALL = VFT_CUBE + VF_CUBE * 4,
    VFT_SHIP = VFT_BALL + VF_BALL * 4,
    VFT_UFO = VFT_SHIP + VF_SHIP * 16,
    VFT_WAVE = VFT_UFO + VF_UFO * 16,
    VF_TILES = VFT_WAVE + VF_WAVE * 4
};
#define VF_UFO_TILT 0.3f
#define SAW_FRAMES 6
#define COG_FRAMES 12
#define ORB_FRAMES 8
/* The garage's vehicles, bigger (garage_render draws them GARAGE_BLOCK
 * pixels a block, the run BLOCK_PX): g_garage_frames[ICON_COUNT][GF_PICS],
 * 32x32 (16 tiles) each: the five as the garage shows them (in the
 * vehicles' order, the wave at GARAGE_WAVE_ANGLE), then the ball a whole
 * turn round in GF_BALL frames. */
#define GARAGE_BLOCK 44.0f
#define GARAGE_WAVE_ANGLE -0.6f
enum { GF_STILL = 5, GF_BALL = 64, GF_PICS = GF_STILL + GF_BALL };

/* The player: g_player_tiles[ICON_COUNT][PLAYER_TILES] holds each icon's
 * vehicles (PT_* their first tiles: the cube, ball and wave 16x16, the
 * ship and UFO 32x32), in the parts the garage colours: */
enum { PT_CUBE = 0, PT_SHIP = 4, PT_BALL = 20, PT_UFO = 24, PT_WAVE = 40, PLAYER_TILES = 44 };
/* (in VRAM each vehicle's place holds its frame of the moment: art.h
 * VF_*; after them, the chosen icon's cube as it is, for the title) */
#define PT_CUBE_STILL PLAYER_TILES
enum {
    PC_K = 1,     /* the outline */
    PC_C1,        /* colour 1, its highlight (* 1.25), colour 2 */
    PC_C1HI,
    PC_C2,
    PC_DOME,      /* the UFO's glass dome, and what is under it */
    PC_K_DOME,
    PC_C1_DOME,
    PC_C1HI_DOME,
    PC_C2_DOME,
};

#endif
