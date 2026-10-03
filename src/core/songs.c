/*
 * Original music for Pulse Dash. Every track below was written for this
 * project; see songdata.h for the notation.
 */
#include "songdata.h"

#include <stddef.h>

/* Shared drum patterns and risers. */
const PatternDef g_common_patterns[] = {
    {"k4", "x...x...x...x..."},
    {"k4f", "x...x...x...x.xx"},
    {"k2", "x.......x......."},
    {"kchill", "x.......x.....x."},
    {"c24", "....c.......c..."},
    {"s24", "....x.......x..."},
    {"cfill", "....c.......c.xx"},
    {"chalf", "........c......."},
    {"sbuild", "x...x...x...x... x...x...x...x... x.x.x.x.x.x.x.x. xxxxxxxxXXXXXXXX"},
    {"sbuild2", "x.x.x.x.x.x.x.x. xxxxxxxxXXXXXXXX"},
    {"hoff", "..x...x...x...x."},
    {"h8", "x.x.x.x.x.x.x.x."},
    {"h16", "xxXxxxXxxxXxxxXx"},
    {"hcrash", "o.x...x...x...x."},
    {"hopen", "..o...o...o...o."},
    {"riserMin", "A4:4 B4:4 C5:4 D5:4 E5:4 F5:4 G5:4 G#5:4 "
                 "A5:2 B5:2 C6:2 D6:2 E6:2 F6:2 G6:2 G#6:2 A6:12 -:4"},
    {"riserMaj", "C5:4 D5:4 E5:4 F5:4 G5:4 A5:4 B5:4 C6:4 "
                 "C6:2 D6:2 E6:2 F6:2 G6:2 A6:2 B6:2 C7:2 G6:12 -:4"},
    {NULL, NULL},
};

/* Standard 40-bar level arrangement:
 * intro 4 | verse 8 | build 4 | drop 8 | break 4 | drop 8 | outro 4 */
#define ARR_KICK ".*4 k4*8 k4*4 k4*8 .*4 k4*8 k4*3 ."
#define ARR_SNARE ".*4 c24*7 cfill sbuild c24*7 cfill .*2 sbuild2 c24*7 cfill c24*3 ."
#define ARR_HAT "hoff*4 hoff*8 h8*4 hcrash h16*7 .*2 h8*2 hcrash h16*7 hoff*4"
#define ARR_BASS ".*4 bassV*2 .*4 bassD*2 .*4 bassD*2 bassV"
#define ARR_PAD "pad .*12 pad*2 pad pad*2 pad"
#define ARR_ARP "arp arp*2 arp arp*2 arp arp*2 arp"
#define ARR_EXTRA ".*4 verseA verseB .*12 verseA .*12"

/* ------------------------------------------------------------------ */
/* Menu loop: "Menu Drift" (F major, 112 BPM)                          */
/* ------------------------------------------------------------------ */
static const PatternDef P_MENU[] = {
    {"pad", "F3+A3+C4+E4:16 A3+C4+E4+G4:16 Bb3+D4+F4+A4:16 C4+E4+G4:16"},
    {"bass", "F2:6 F2:2 -:4 C2:4 A1:6 A1:2 -:4 E2:4 Bb1:6 Bb1:2 -:4 F2:4 C2:6 C2:2 -:4 G2:4"},
    {"arp", "F4:2 A4:2 C5:2 E5:2 C5:2 A4:2 F4:2 A4:2 "
            "E4:2 A4:2 C5:2 G5:2 C5:2 A4:2 E4:2 A4:2 "
            "F4:2 Bb4:2 D5:2 A5:2 D5:2 Bb4:2 F4:2 Bb4:2 "
            "E4:2 G4:2 C5:2 E5:2 G5:2 E5:2 C5:2 G4:2"},
    {"lead", "A5:4 G5:2 F5:2 C5:8 E5:4 D5:2 C5:2 A4:8 D5:4 F5:4 A5:4 G5:4 E5:8 G5:8 "
             "A5:4 C6:4 A5:2 G5:2 F5:4 E5:4 G5:4 C5:8 D5:2 E5:2 F5:4 A5:4 F5:4 G5:16"},
    {NULL, NULL},
};
static const SongDef SONG_MENU_DEF = {
    "Menu Drift", 112.0f, 4, P_MENU,
    {".*4 kchill*12", ".*8 chalf*8", ".*2 hoff*14", ".*4 bass*3", "pad*4", "arp*4", NULL, ".*8 lead"},
    {0, 0, 0, INS_BASS_SUB, INS_PAD_SOFT, INS_BELL, INS_LEAD_SQUARE, INS_CHIP_TRI},
    {0.8f, 0.8f, 0.6f, 0.9f, 1.0f, 0.75f, 1.0f, 0.75f},
};

/* ------------------------------------------------------------------ */
/* Practice loop: "Practice Room" (D major, 96 BPM)                    */
/* ------------------------------------------------------------------ */
static const PatternDef P_PRACTICE[] = {
    {"pad", "D3+F#3+A3:16 B2+D3+F#3:16 G2+B2+D3:16 A2+C#3+E3:16"},
    {"bass", "D2:8 D2:4 A1:4 B1:8 B1:4 F#1:4 G1:8 G1:4 D2:4 A1:8 A1:4 E2:4"},
    {"arp", "D4:2 F#4:2 A4:2 D5:2 A4:2 F#4:2 D4:2 F#4:2 "
            "D4:2 F#4:2 B4:2 D5:2 B4:2 F#4:2 D4:2 F#4:2 "
            "D4:2 G4:2 B4:2 D5:2 B4:2 G4:2 D4:2 G4:2 "
            "C#4:2 E4:2 A4:2 C#5:2 A4:2 E4:2 C#4:2 E4:2"},
    {"lead", "F#5:6 E5:2 D5:8 D5:4 E5:4 F#5:8 G5:6 F#5:2 E5:4 D5:4 E5:16"},
    {NULL, NULL},
};
static const SongDef SONG_PRACTICE_DEF = {
    "Practice Room", 96.0f, 0, P_PRACTICE,
    {".*4 k2*4", ".*4 chalf*4", "hoff*8", "bass*2", "pad*2", "arp*2", NULL, ".*4 lead"},
    {0, 0, 0, INS_BASS_SUB, INS_PAD_SOFT, INS_PLUCK, INS_LEAD_SQUARE, INS_BELL},
    {0.7f, 0.7f, 0.5f, 0.9f, 1.0f, 0.7f, 1.0f, 0.8f},
};

/* ------------------------------------------------------------------ */
/* Level 1: "Neon Steps" (A minor, 126 BPM)                            */
/* ------------------------------------------------------------------ */
static const PatternDef P_NEON[] = {
    {"pad", "A3+C4+E4:16 F3+A3+C4:16 E3+G3+C4:16 D3+G3+B3:16"},
    {"bassV", "A1:3 A1:3 A1:2 A2:2 A1:2 G1:2 A1:2 "
              "F1:3 F1:3 F1:2 F2:2 F1:2 E1:2 F1:2 "
              "C2:3 C2:3 C2:2 C3:2 C2:2 B1:2 C2:2 "
              "G1:3 G1:3 G1:2 G2:2 G1:2 F1:2 G1:2"},
    {"bassD", "A1:2 A2:2 A1:2 A2:2 A1:2 A2:2 A1:2 A2:2 "
              "F1:2 F2:2 F1:2 F2:2 F1:2 F2:2 F1:2 F2:2 "
              "C2:2 C3:2 C2:2 C3:2 C2:2 C3:2 C2:2 C3:2 "
              "G1:2 G2:2 G1:2 G2:2 G1:2 G2:2 G1:2 G2:2"},
    {"arp", "A4 C5 E5 A5 E5 C5 A4 C5 E5 A5 E5 C5 A4 C5 E5 C5 "
            "F4 A4 C5 F5 C5 A4 F4 A4 C5 F5 C5 A4 F4 A4 C5 A4 "
            "E4 G4 C5 E5 C5 G4 E4 G4 C5 E5 C5 G4 E4 G4 C5 G4 "
            "D4 G4 B4 D5 B4 G4 D4 G4 B4 D5 B4 G4 D4 G4 B4 G4"},
    {"verseA", "A4:2 C5:2 E5:3 D5:1 C5:2 D5:2 E5:4 "
               "F5:3 E5:1 C5:2 A4:2 C5:4 -:4 "
               "G4:2 C5:2 E5:3 D5:1 C5:2 E5:2 G5:4 "
               "F5:3 E5:1 D5:2 B4:2 D5:6 -:2"},
    {"verseB", "A4:2 C5:2 E5:3 D5:1 C5:2 D5:2 E5:4 "
               "F5:3 G5:1 A5:2 G5:2 F5:4 E5:2 C5:2 "
               "E5:3 D5:1 C5:2 D5:2 E5:4 G5:4 "
               "D5:4 B4:2 D5:2 E5:8"},
    {"dropA", "E5:2 A5:2 -:1 A5:1 G5:2 A5:2 C6:2 B5:2 G5:2 "
              "A5:3 F5:3 C5:2 F5:2 G5:2 A5:4 "
              "G5:2 C6:2 -:1 C6:1 B5:2 C6:2 E6:2 D6:2 C6:2 "
              "B5:3 G5:3 D5:2 G5:2 A5:2 B5:4"},
    {"dropB", "E5:2 A5:2 -:1 A5:1 G5:2 A5:2 C6:2 B5:2 G5:2 "
              "A5:3 C6:3 A5:2 G5:2 F5:2 E5:4 "
              "E5:3 G5:3 C6:2 B5:2 G5:2 E5:2 G5:2 "
              "D6:6 B5:2 A5:8"},
    {NULL, NULL},
};
static const SongDef SONG_NEON = {
    "Neon Steps", 126.0f, 4, P_NEON,
    {ARR_KICK, ARR_SNARE, ARR_HAT, ARR_BASS, ARR_PAD, ARR_ARP,
     ".*12 riserMin dropA dropB .*4 dropA dropB .*4", ARR_EXTRA},
    {0, 0, 0, INS_BASS_SAW, INS_PAD_SUPER, INS_PLUCK, INS_LEAD_SUPER, INS_LEAD_SQUARE},
    {1.0f, 0.9f, 0.7f, 1.0f, 0.85f, 0.6f, 0.95f, 0.9f},
};

/* ------------------------------------------------------------------ */
/* Level 2: "Skyward Pulse" (C major, 130 BPM)                         */
/* ------------------------------------------------------------------ */
static const PatternDef P_SKY[] = {
    {"pad", "C4+E4+G4:16 B3+D4+G4:16 A3+C4+E4:16 A3+C4+F4:16"},
    {"bassV", "-:2 C3:2 -:2 C3:2 -:2 C3:2 -:2 C3:2 "
              "-:2 G2:2 -:2 G2:2 -:2 G2:2 -:2 G2:2 "
              "-:2 A2:2 -:2 A2:2 -:2 A2:2 -:2 A2:2 "
              "-:2 F2:2 -:2 F2:2 -:2 F2:2 -:2 F2:2"},
    {"bassD", "C2:2 C3:2 -:2 C3:2 C2:2 C3:2 -:2 C3:2 "
              "G1:2 G2:2 -:2 G2:2 G1:2 G2:2 -:2 G2:2 "
              "A1:2 A2:2 -:2 A2:2 A1:2 A2:2 -:2 A2:2 "
              "F1:2 F2:2 -:2 F2:2 F1:2 F2:2 -:2 F2:2"},
    {"arp", "C5 G4 E5 G4 C5 G4 E5 G4 C5 G4 E5 G4 C5 G4 E5 G4 "
            "B4 G4 D5 G4 B4 G4 D5 G4 B4 G4 D5 G4 B4 G4 D5 G4 "
            "A4 E4 C5 E4 A4 E4 C5 E4 A4 E4 C5 E4 A4 E4 C5 E4 "
            "A4 F4 C5 F4 A4 F4 C5 F4 A4 F4 C5 F4 A4 F4 C5 F4"},
    {"verseA", "E5:2 G5:2 C6:2 B5:2 G5:4 E5:4 "
               "D5:2 G5:2 B5:2 A5:2 G5:4 D5:4 "
               "C5:2 E5:2 A5:2 G5:2 E5:4 C5:2 D5:2 "
               "C5:4 A4:4 C5:2 D5:2 E5:4"},
    {"verseB", "E5:2 G5:2 C6:2 B5:2 G5:4 C6:4 "
               "D6:2 B5:2 G5:2 A5:2 B5:4 D6:4 "
               "C6:3 B5:3 A5:2 E5:4 A5:4 "
               "G5:3 F5:3 E5:2 D5:2 C5:6"},
    {"dropA", "G5:3 G5:3 E5:2 G5:2 A5:2 G5:2 E5:2 "
              "D5:3 D5:3 B4:2 D5:2 G5:2 D5:2 B4:2 "
              "E5:3 E5:3 C5:2 E5:2 A5:2 G5:2 E5:2 "
              "F5:3 E5:3 C5:2 A4:4 C5:4"},
    {"dropB", "G5:3 G5:3 E5:2 G5:2 C6:2 B5:2 G5:2 "
              "B5:3 B5:3 G5:2 B5:2 D6:2 C6:2 B5:2 "
              "C6:3 B5:3 A5:2 G5:2 E5:2 G5:2 A5:2 "
              "A5:3 G5:3 F5:2 E5:4 C5:4"},
    {NULL, NULL},
};
static const SongDef SONG_SKY = {
    "Skyward Pulse", 130.0f, 4, P_SKY,
    {ARR_KICK, ARR_SNARE, ARR_HAT, ARR_BASS, ARR_PAD, ARR_ARP,
     ".*12 riserMaj dropA dropB .*4 dropA dropB .*4", ARR_EXTRA},
    {0, 0, 0, INS_BASS_SQUARE, INS_PAD_SUPER, INS_PLUCK_SAW, INS_LEAD_SUPER, INS_LEAD_PULSE},
    {1.0f, 0.9f, 0.7f, 1.0f, 0.85f, 0.6f, 0.95f, 0.9f},
};

/* ------------------------------------------------------------------ */
/* Level 3: "Gravity Garden" (E minor, 136 BPM)                        */
/* ------------------------------------------------------------------ */
static const PatternDef P_GARDEN[] = {
    {"pad", "E3+G3+B3:16 E3+G3+C4:16 D3+G3+B3:16 D3+F#3+A3:16"},
    {"bassV", "E2 - E2 E3 -:2 E2:2 - E2 E3:2 D3:2 B2:2 "
              "C2 - C2 C3 -:2 C2:2 - C2 C3:2 B2:2 G2:2 "
              "G1 - G1 G2 -:2 G1:2 - G1 G2:2 F#2:2 D2:2 "
              "D2 - D2 D3 -:2 D2:2 - D2 D3:2 C3:2 A2:2"},
    {"bassD", "E2 - E2 E3 -:2 E2:2 - E2 E3:2 D3:2 B2:2 "
              "C2 - C2 C3 -:2 C2:2 - C2 C3:2 B2:2 G2:2 "
              "G1 - G1 G2 -:2 G1:2 - G1 G2:2 F#2:2 D2:2 "
              "D2 - D2 D3 -:2 D2:2 - D2 D3:2 C3:2 A2:2"},
    {"arp", "E5 B4 G4 B4 E5 B4 G4 B4 E5 B4 G4 B4 F#5 B4 G4 B4 "
            "E5 C5 G4 C5 E5 C5 G4 C5 E5 C5 G4 C5 D5 C5 G4 C5 "
            "D5 B4 G4 B4 D5 B4 G4 B4 D5 B4 G4 B4 E5 B4 G4 B4 "
            "D5 A4 F#4 A4 D5 A4 F#4 A4 D5 A4 F#4 A4 E5 A4 F#4 A4"},
    {"verseA", "B4:2 E5:2 F#5:2 G5:4 F#5:2 E5:2 D5:2 "
               "E5:6 G5:2 E5:4 C5:4 "
               "D5:2 G5:2 A5:2 B5:4 A5:2 G5:2 D5:2 "
               "F#5:8 E5:4 D5:4"},
    {"verseB", "B4:2 E5:2 F#5:2 G5:4 A5:2 B5:4 "
               "C6:4 B5:2 A5:2 G5:4 E5:4 "
               "D5:2 G5:2 B5:2 D6:4 B5:2 A5:4 "
               "F#5:6 G5:2 A5:8"},
    {"dropA", "E6:2 B5:1 G5:1 E5:2 G5:2 B5:2 G5:2 A5:2 B5:2 "
              "C6:3 B5:3 G5:2 E5:4 G5:4 "
              "D6:2 B5:1 G5:1 D5:2 G5:2 B5:2 D6:2 C6:2 B5:2 "
              "A5:3 F#5:3 D5:2 F#5:4 A5:4"},
    {"dropB", "E6:2 B5:1 G5:1 E5:2 G5:2 B5:2 G5:2 A5:2 B5:2 "
              "C6:3 D6:3 E6:2 D6:4 C6:4 "
              "B5:3 A5:3 G5:2 D5:2 G5:2 A5:2 B5:2 "
              "A5:6 F#5:2 E5:8"},
    {NULL, NULL},
};
static const SongDef SONG_GARDEN = {
    "Gravity Garden", 136.0f, 4, P_GARDEN,
    {ARR_KICK, ARR_SNARE, ARR_HAT, ARR_BASS, ARR_PAD, ARR_ARP,
     ".*12 riserMin-5 dropA dropB .*4 dropA dropB .*4", ARR_EXTRA},
    {0, 0, 0, INS_BASS_SAW, INS_PAD_SUPER, INS_PLUCK, INS_LEAD_SUPER, INS_LEAD_SQUARE},
    {1.0f, 0.9f, 0.7f, 1.0f, 0.8f, 0.6f, 0.95f, 0.9f},
};

/* ------------------------------------------------------------------ */
/* Level 4: "Saucer Groove" (D minor, 140 BPM)                         */
/* ------------------------------------------------------------------ */
static const PatternDef P_SAUCER[] = {
    {"pad", "D3+F3+A3:16 D3+F3+Bb3:16 C3+E3+G3:16 C#3+E3+A3:16"},
    {"bassV", "D2:2 -:1 D2:1 D3:2 -:1 C3:1 -:2 A2:2 F2:2 D2:2 "
              "Bb1:2 -:1 Bb1:1 Bb2:2 -:1 A2:1 -:2 F2:2 D2:2 Bb1:2 "
              "C2:2 -:1 C2:1 C3:2 -:1 Bb2:1 -:2 G2:2 E2:2 C2:2 "
              "A1:2 -:1 A1:1 A2:2 -:1 G2:1 -:2 E2:2 C#2:2 A1:2"},
    {"bassD", "D2:2 -:1 D2:1 D3:2 -:1 C3:1 -:2 A2:2 F2:2 D2:2 "
              "Bb1:2 -:1 Bb1:1 Bb2:2 -:1 A2:1 -:2 F2:2 D2:2 Bb1:2 "
              "C2:2 -:1 C2:1 C3:2 -:1 Bb2:1 -:2 G2:2 E2:2 C2:2 "
              "A1:2 -:1 A1:1 A2:2 -:1 G2:1 -:2 E2:2 C#2:2 A1:2"},
    {"arp", "D5:2 A4:2 F5:2 A4:2 D5:2 A4:2 F5:2 A4:2 "
            "D5:2 Bb4:2 F5:2 Bb4:2 D5:2 Bb4:2 F5:2 Bb4:2 "
            "E5:2 C5:2 G5:2 C5:2 E5:2 C5:2 G5:2 C5:2 "
            "E5:2 C#5:2 A5:2 C#5:2 E5:2 C#5:2 A5:2 C#5:2"},
    {"verseA", "A5:3 F5:3 D5:2 E5:2 F5:2 G5:2 A5:2 "
               "Bb5:3 A5:3 F5:2 D5:4 F5:4 "
               "G5:3 E5:3 C5:2 D5:2 E5:2 F5:2 G5:2 "
               "A5:6 G5:2 E5:4 C#5:4"},
    {"verseB", "A5:3 F5:3 D5:2 E5:2 F5:2 A5:2 D6:2 "
               "D6:3 C6:3 Bb5:2 A5:4 F5:4 "
               "E5:3 G5:3 C6:2 Bb5:2 A5:2 G5:2 E5:2 "
               "C#6:8 A5:8"},
    {"dropA", "D6:1 -:1 D6:1 -:1 A5:2 F5:2 D6:2 C6:2 A5:2 F5:2 "
              "D6:1 -:1 D6:1 -:1 Bb5:2 F5:2 D6:2 C6:2 Bb5:2 A5:2 "
              "E6:1 -:1 E6:1 -:1 C6:2 G5:2 E6:2 D6:2 C6:2 G5:2 "
              "C#6:4 E6:4 A6:8"},
    {"dropB", "D6:1 -:1 D6:1 -:1 A5:2 F5:2 D6:2 C6:2 A5:2 F5:2 "
              "D6:1 -:1 D6:1 -:1 Bb5:2 F5:2 D6:2 C6:2 Bb5:2 A5:2 "
              "E6:1 -:1 E6:1 -:1 C6:2 G5:2 E6:2 D6:2 C6:2 G5:2 "
              "A5:4 C#6:4 E6:4 C#6:4"},
    {NULL, NULL},
};
static const SongDef SONG_SAUCER = {
    "Saucer Groove", 140.0f, 4, P_SAUCER,
    {ARR_KICK, ARR_SNARE, ARR_HAT, ARR_BASS, ARR_PAD, ARR_ARP,
     ".*12 riserMin-7 dropA dropB .*4 dropA dropB .*4", ARR_EXTRA},
    {0, 0, 0, INS_BASS_SAW, INS_PAD_SUPER, INS_PLUCK_SAW, INS_LEAD_SUPER, INS_LEAD_PULSE},
    {1.0f, 0.9f, 0.7f, 1.0f, 0.8f, 0.6f, 0.95f, 0.9f},
};

/* ------------------------------------------------------------------ */
/* Level 5: "Wave Rider" (F# minor, 150 BPM)                           */
/* ------------------------------------------------------------------ */
static const PatternDef P_WAVE[] = {
    {"pad", "F#3+A3+C#4:16 F#3+A3+D4:16 E3+A3+C#4:16 E3+G#3+B3:16"},
    {"bassV", "F#2 F#2 F#3 F#2 F#2 F#2 F#3 F#2 F#2 F#2 F#3 F#2 F#2 F#2 F#3 F#2 "
              "D2 D2 D3 D2 D2 D2 D3 D2 D2 D2 D3 D2 D2 D2 D3 D2 "
              "A1 A1 A2 A1 A1 A1 A2 A1 A1 A1 A2 A1 A1 A1 A2 A1 "
              "E2 E2 E3 E2 E2 E2 E3 E2 E2 E2 E3 E2 E2 E2 E3 E2"},
    {"bassD", "F#2 F#2 F#3 F#2 F#2 F#2 F#3 F#2 F#2 F#2 F#3 F#2 F#2 F#2 F#3 F#2 "
              "D2 D2 D3 D2 D2 D2 D3 D2 D2 D2 D3 D2 D2 D2 D3 D2 "
              "A1 A1 A2 A1 A1 A1 A2 A1 A1 A1 A2 A1 A1 A1 A2 A1 "
              "E2 E2 E3 E2 E2 E2 E3 E2 E2 E2 E3 E2 E2 E2 E3 E2"},
    {"arp", "C#5 F#5 A5 F#5 C#5 F#5 A5 F#5 C#5 F#5 A5 F#5 C#5 F#5 A5 F#5 "
            "D5 F#5 A5 F#5 D5 F#5 A5 F#5 D5 F#5 A5 F#5 D5 F#5 A5 F#5 "
            "C#5 E5 A5 E5 C#5 E5 A5 E5 C#5 E5 A5 E5 C#5 E5 A5 E5 "
            "B4 E5 G#5 E5 B4 E5 G#5 E5 B4 E5 G#5 E5 B4 E5 G#5 E5"},
    {"verseA", "C#6:4 B5:2 A5:2 G#5:2 A5:2 F#5:4 "
               "F#5:2 A5:2 D6:4 C#6:4 A5:4 "
               "E5:2 A5:2 C#6:4 B5:2 A5:2 E5:4 "
               "G#5:8 B5:8"},
    {"verseB", "C#6:4 B5:2 A5:2 B5:2 C#6:2 E6:4 "
               "F#6:4 E6:2 D6:2 C#6:4 A5:4 "
               "C#6:2 B5:2 A5:4 E5:2 A5:2 C#6:4 "
               "B5:12 G#5:4"},
    {"dropA", "F#5:2 A5:2 C#6:2 F#6:2 E6:2 C#6:2 A5:2 C#6:2 "
              "D6:2 F#6:2 A6:2 F#6:2 E6:2 D6:2 C#6:2 A5:2 "
              "C#6:2 E6:2 A6:2 E6:2 C#6:2 B5:2 A5:2 E5:2 "
              "G#5:4 B5:4 E6:4 G#6:4"},
    {"dropB", "F#5:2 A5:2 C#6:2 F#6:2 E6:2 C#6:2 A5:2 C#6:2 "
              "D6:2 F#6:2 A6:2 F#6:2 E6:2 D6:2 C#6:2 A5:2 "
              "C#6:2 E6:2 A6:2 E6:2 C#6:2 B5:2 A5:2 E5:2 "
              "B5:4 G#5:4 E5:8"},
    {NULL, NULL},
};
static const SongDef SONG_WAVE = {
    "Wave Rider", 150.0f, 4, P_WAVE,
    {ARR_KICK, ARR_SNARE, ARR_HAT, ARR_BASS, ARR_PAD, ARR_ARP,
     ".*12 riserMin-3 dropA dropB .*4 dropA dropB .*4", ARR_EXTRA},
    {0, 0, 0, INS_BASS_SQUARE, INS_PAD_SUPER, INS_PLUCK, INS_LEAD_SUPER, INS_LEAD_SQUARE},
    {1.0f, 0.9f, 0.7f, 0.9f, 0.8f, 0.55f, 0.95f, 0.9f},
};

/* ------------------------------------------------------------------ */
/* Level 6: "Prism Overdrive" (C minor, 160 BPM)                       */
/* ------------------------------------------------------------------ */
static const PatternDef P_PRISM[] = {
    {"pad", "C4+Eb4+G4:16 C4+Eb4+Ab4:16 Bb3+Eb4+G4:16 Bb3+D4+F4:16"},
    {"bassV", "C2:2 C3:2 C2:2 C3:2 C2:2 C3:2 C2:2 C3:2 "
              "Ab1:2 Ab2:2 Ab1:2 Ab2:2 Ab1:2 Ab2:2 Ab1:2 Ab2:2 "
              "Eb2:2 Eb3:2 Eb2:2 Eb3:2 Eb2:2 Eb3:2 Eb2:2 Eb3:2 "
              "Bb1:2 Bb2:2 Bb1:2 Bb2:2 Bb1:2 Bb2:2 Bb1:2 Bb2:2"},
    {"bassD", "C2 C2 C3 C2 C2 C2 C3 C2 C2 C2 C3 C2 C2 C2 C3 C2 "
              "Ab1 Ab1 Ab2 Ab1 Ab1 Ab1 Ab2 Ab1 Ab1 Ab1 Ab2 Ab1 Ab1 Ab1 Ab2 Ab1 "
              "Eb2 Eb2 Eb3 Eb2 Eb2 Eb2 Eb3 Eb2 Eb2 Eb2 Eb3 Eb2 Eb2 Eb2 Eb3 Eb2 "
              "Bb1 Bb1 Bb2 Bb1 Bb1 Bb1 Bb2 Bb1 Bb1 Bb1 Bb2 Bb1 Bb1 Bb1 Bb2 Bb1"},
    {"arp", "C5 Eb5 G5 C6 G5 Eb5 C5 Eb5 G5 C6 G5 Eb5 C5 Eb5 G5 Eb5 "
            "Ab4 C5 Eb5 Ab5 Eb5 C5 Ab4 C5 Eb5 Ab5 Eb5 C5 Ab4 C5 Eb5 C5 "
            "G4 Bb4 Eb5 G5 Eb5 Bb4 G4 Bb4 Eb5 G5 Eb5 Bb4 G4 Bb4 Eb5 Bb4 "
            "F4 Bb4 D5 F5 D5 Bb4 F4 Bb4 D5 F5 D5 Bb4 F4 Bb4 D5 Bb4"},
    {"verseA", "G5:4 Eb5:2 F5:2 G5:4 C6:4 "
               "C6:4 Bb5:2 Ab5:2 G5:4 Eb5:4 "
               "Bb5:4 G5:2 Ab5:2 Bb5:4 Eb6:4 "
               "D6:8 C6:4 Bb5:4"},
    {"verseB", "G5:4 Eb5:2 F5:2 G5:4 Bb5:2 C6:2 "
               "Eb6:4 D6:2 C6:2 Ab5:4 C6:4 "
               "Bb5:4 G5:2 Bb5:2 Eb6:4 G6:4 "
               "F6:8 D6:8"},
    {"dropA", "C6:3 C6:3 Bb5:2 C6:2 Eb6:2 D6:2 C6:2 "
              "Ab5:3 Ab5:3 G5:2 Ab5:2 C6:2 Bb5:2 Ab5:2 "
              "G5:3 G5:3 F5:2 G5:2 Bb5:2 Ab5:2 G5:2 "
              "F5:3 G5:3 Bb5:2 D6:8"},
    {"dropB", "C6:3 C6:3 Bb5:2 C6:2 Eb6:2 F6:2 G6:2 "
              "Ab6:3 G6:3 Eb6:2 C6:4 Eb6:4 "
              "G6:3 F6:3 Eb6:2 Bb5:4 G5:4 "
              "F6:6 D6:2 Bb5:8"},
    {NULL, NULL},
};
static const SongDef SONG_PRISM = {
    "Prism Overdrive", 160.0f, 4, P_PRISM,
    {".*4 k4*8 k4*4 k4*8 .*4 k4*4 k4*8 k4*3 .",
     ".*4 c24*7 cfill sbuild c24*7 cfill .*2 sbuild2 sbuild c24*7 cfill c24*3 .",
     "hoff*4 hoff*8 h8*4 hcrash h16*7 .*2 h8*2 h8*4 hcrash h16*7 hoff*4",
     ".*4 bassV*2 .*4 bassD*2 .*4 bassV bassD*2 bassV",
     "pad .*12 pad*2 pad pad pad*2 pad",
     "arp arp*2 arp arp*2 arp arp arp*2 arp",
     ".*12 riserMin+3 dropA dropB .*4 riserMin+3 dropA dropB .*4",
     ".*4 verseA verseB .*12 verseA .*16"},
    {0, 0, 0, INS_BASS_SAW, INS_PAD_SUPER, INS_PLUCK_SAW, INS_LEAD_SUPER, INS_LEAD_PULSE},
    {1.0f, 0.9f, 0.7f, 1.0f, 0.8f, 0.55f, 0.95f, 0.9f},
};

const SongDef *const g_songs[] = {
    &SONG_MENU_DEF, &SONG_PRACTICE_DEF, &SONG_NEON, &SONG_SKY, &SONG_GARDEN,
    &SONG_SAUCER, &SONG_WAVE, &SONG_PRISM,
};
const int g_song_count = (int)(sizeof(g_songs) / sizeof(g_songs[0]));
