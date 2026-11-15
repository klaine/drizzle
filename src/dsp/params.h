/* Drizzle M0 parameters. A hand-written table until tools/gen/params.py takes over at M1
 * (plan/14). Keys and mappings follow plan/10 so that M1's generated table can keep them. */
#ifndef DRZ_PARAMS_H
#define DRZ_PARAMS_H

enum {
    P_PATCH_VOL,
    P_GR_POS,
    P_GR_SPRAY,
    P_GR_SIZE,
    P_GR_DENS,
    P_GR_JIT,
    P_BUF_LEN,
    P_BUF_FREEZE,
    P_SRC_ROOT,
    P_AN_A_WAVE,
    P_GR_SPREAD,
    P_COUNT
};

typedef enum { PK_FLOAT, PK_INT, PK_ENUM } param_kind_t;

typedef struct {
    const char *key, *name, *short_name;
    param_kind_t kind;
    float min, max, def;
    const char *const *options;       /* enum only, NULL-terminated */
    const char *const *short_options; /* enum only */
    int cp;                           /* 1: in chain_params */
} param_desc_t;

extern const param_desc_t DRZ_PARAMS[P_COUNT];

/* M0 voice of the Analog source: plan/04 section 3 has Sine, Tri, Saw, Pulse; M0 builds Sine and Saw. */
enum { AW_SINE = 0, AW_SAW = 1 };

struct drz_inst;
void params_defaults(struct drz_inst *I);
void params_set(struct drz_inst *I, const char *key, const char *val);
int params_get(struct drz_inst *I, const char *key, char *buf, int buf_len);
int params_find(const char *key);
void params_build_contract(void); /* once, from move_plugin_init_v2 */

/* Physical mappings (plan/10). */
float map_size_s(float x);   /* 5 ms .. 2 s, exponential */
float map_dens_hz(float x);  /* 0.5 .. 250 grains/s, exponential */
float map_buf_len_s(float x); /* 0.5 .. 8 s, exponential */

#endif
