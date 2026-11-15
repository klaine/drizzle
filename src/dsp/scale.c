/* Pitch quantise Scale (plan/06 section 4): the Move set's root and scale, read through Schwung's
 * move_info.h as Crest's arp does. The first read resolves the export (dlsym, shm_open, mmap) and runs
 * in create_instance; later reads are a lock-free copy. Chromatic until a scale is known. */
#if defined(__linux__) && !defined(DRZ_WASM)
#define _GNU_SOURCE
#endif
#include <string.h>

#include "drizzle.h"
#include "dsp_util.h"

#if defined(__linux__) && !defined(DRZ_WASM)
#include "move_info.h"
#else
typedef struct {
    uint8_t valid;
    int8_t root_note;
    char scale[24];
} move_info_t;
static int move_info_read(move_info_t *out) {
    (void)out;
    return 0;
}
#endif

static int name_is(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        char ca = *a >= 'A' && *a <= 'Z' ? (char)(*a - 'A' + 'a') : *a;
        if (ca != *b) return 0;
    }
    return *a == 0 && *b == 0;
}

void scale_update(drz_inst_t *I) {
    static const struct {
        const char *name;
        uint16_t mask;
    } NAMES[] = {
        {"major", 0x0AB5}, {"minor", 0x05AD}, {"dorian", 0x06AD}, {"phrygian", 0x05AB}, {"phryg", 0x05AB},
        {"lydian", 0x0AD5}, {"mixolydian", 0x06B5}, {"mixo", 0x06B5}, {"major pentatonic", 0x0295},
        {"pentatonic", 0x0295}, {"minor pentatonic", 0x04A9}, {"min pentatonic", 0x04A9}, {"blues", 0x04E9},
        {"chromatic", 0x0FFF},
    };
    move_info_t mi;
    memset(&mi, 0, sizeof mi);
    if (!I->scale_mask) I->scale_mask = 0x0FFF;
    if (!move_info_read(&mi) || !mi.valid) return;
    if (mi.root_note >= 0 && mi.root_note < 12) I->scale_root = mi.root_note;
    mi.scale[sizeof mi.scale - 1] = 0;
    for (size_t i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++)
        if (name_is(mi.scale, NAMES[i].name)) I->scale_mask = NAMES[i].mask;
}

/* The nearest scale note to `note` (ties go down). */
float scale_quantise(const drz_inst_t *I, float note) {
    int n = (int)(note + 0.5f);
    unsigned mask = I->scale_mask ? I->scale_mask : 0x0FFF;
    for (int d = 0; d < 12; d++) {
        int lo = n - d, hi = n + d;
        if (mask >> (((lo - I->scale_root) % 12 + 12) % 12) & 1u) return (float)lo;
        if (mask >> (((hi - I->scale_root) % 12 + 12) % 12) & 1u) return (float)hi;
    }
    return (float)n;
}
