/* Parameters, state blob and the Move contract, M0 (plan/10, plan/12). The table is hand-written
 * until tools/gen/params.py generates it at M1; the keys, ranges and mappings already follow plan/10. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drizzle.h"
#include "dsp_util.h"

static const char *const OFF_ON[] = {"Off", "On", NULL};
static const char *const WAVES[] = {"Sine", "Saw", NULL};
static const char *const WAVES_S[] = {"Sin", "Saw", NULL};

const param_desc_t DRZ_PARAMS[P_COUNT] = {
    [P_PATCH_VOL] = {"patch_vol", "Volume", "Vol", PK_FLOAT, 0, 1, 0.7f, NULL, NULL, 1},
    [P_GR_POS] = {"gr_pos", "Position", "Pos", PK_FLOAT, 0, 1, 0.5f, NULL, NULL, 1},
    [P_GR_SPRAY] = {"gr_spray", "Spray", "Spray", PK_FLOAT, 0, 1, 0.1f, NULL, NULL, 1},
    [P_GR_SIZE] = {"gr_size", "Size", "Size", PK_FLOAT, 0, 1, 0.55f, NULL, NULL, 1},
    [P_GR_DENS] = {"gr_dens", "Density", "Dens", PK_FLOAT, 0, 1, 0.5f, NULL, NULL, 1},
    [P_GR_JIT] = {"gr_jit", "Jitter", "Jit", PK_FLOAT, 0, 1, 0.5f, NULL, NULL, 1},
    [P_BUF_LEN] = {"buf_len", "Buffer Length", "Len", PK_FLOAT, 0, 1, 0.75f, NULL, NULL, 1},
    [P_BUF_FREEZE] = {"buf_freeze", "Freeze", "Frz", PK_ENUM, 0, 1, 0, OFF_ON, OFF_ON, 1},
    [P_SRC_ROOT] = {"src_root", "Source Root", "Root", PK_INT, 24, 96, 48, NULL, NULL, 1},
    [P_AN_A_WAVE] = {"an_a_wave", "Wave A", "WavA", PK_ENUM, 0, 1, AW_SAW, WAVES, WAVES_S, 1},
    [P_GR_SPREAD] = {"gr_spread", "Stereo Spread", "Sprd", PK_FLOAT, 0, 1, 0.5f, NULL, NULL, 1},
};

float map_size_s(float x) { return 0.005f * powf(400.0f, clampf(x, 0.0f, 1.0f)); }
float map_dens_hz(float x) { return 0.5f * powf(500.0f, clampf(x, 0.0f, 1.0f)); }
float map_buf_len_s(float x) { return 0.5f * powf(16.0f, clampf(x, 0.0f, 1.0f)); }

int params_find(const char *key) {
    for (int i = 0; i < P_COUNT; i++)
        if (strcmp(DRZ_PARAMS[i].key, key) == 0) return i;
    return -1;
}

void params_defaults(drz_inst_t *I) {
    for (int i = 0; i < P_COUNT; i++) I->v[i] = DRZ_PARAMS[i].def;
    snprintf(I->name, sizeof I->name, "Init");
    I->pdirty = 1;
}

/* Parse a value for parameter id; 0 when it is not a number or option name (nan and inf included). */
static int parse_value(int id, const char *val, int len, float *out) {
    const param_desc_t *d = &DRZ_PARAMS[id];
    if (d->kind == PK_ENUM) {
        for (int o = 0; d->options[o]; o++)
            if ((int)strlen(d->options[o]) == len && strncmp(d->options[o], val, (size_t)len) == 0) {
                *out = (float)o;
                return 1;
            }
    }
    char tmp[32];
    if (len <= 0 || len >= (int)sizeof tmp) return 0;
    memcpy(tmp, val, (size_t)len);
    tmp[len] = 0;
    char *end;
    float f = strtof(tmp, &end);
    if (end == tmp || !isfinite(f)) return 0;
    if (d->kind != PK_FLOAT) f = roundf(f);
    *out = clampf(f, d->min, d->max);
    return 1;
}

static int format_value(int id, float val, char *buf, int cap) {
    int n = DRZ_PARAMS[id].kind == PK_FLOAT ? snprintf(buf, (size_t)cap, "%.4f", (double)val)
                                             : snprintf(buf, (size_t)cap, "%d", (int)lrintf(val));
    return n >= cap ? -1 : n;
}

static int copy_out(const char *s, int len, char *buf, int cap) {
    if (len + 1 > cap) return -1;
    memcpy(buf, s, (size_t)len);
    buf[len] = 0;
    return len;
}

/* ------------------------------------------------------------------ state (plan/12 section 2) */

static int state_write(const drz_inst_t *I, char *buf, int cap) {
    int n = snprintf(buf, (size_t)cap, "{\"fmt\":\"drizzle\",\"v\":\"1\",\"name\":\"");
    if (n < 0 || n >= cap) return -1;
    for (const char *c = I->name; *c; c++) {
        if (n + 3 >= cap) return -1;
        if (*c == '"' || *c == '\\') buf[n++] = '\\';
        buf[n++] = *c;
    }
    if (n + 2 >= cap) return -1;
    buf[n++] = '"';
    for (int i = 0; i < P_COUNT; i++) {
        char val[32];
        if (format_value(i, I->v[i], val, sizeof val) < 0) return -1;
        int m = snprintf(buf + n, (size_t)(cap - n), ",\"%s\":\"%s\"", DRZ_PARAMS[i].key, val);
        if (m < 0 || m >= cap - n) return -1;
        n += m;
    }
    if (n + 2 > cap) return -1;
    buf[n++] = '}';
    buf[n] = 0;
    return n;
}

/* Read the next "key":"value" pair from *p (bounded by end). Returns 0 at the end or on junk. */
static int next_pair(const char **p, const char *end, const char **k, int *kl, const char **v, int *vl) {
    const char *s = *p;
    while (s < end && *s != '"') s++;
    if (s >= end) return 0;
    *k = ++s;
    while (s < end && *s != '"') s++;
    if (s >= end) return 0;
    *kl = (int)(s - *k);
    s++;
    while (s < end && (*s == ' ' || *s == ':')) s++;
    if (s >= end || *s != '"') return 0;
    *v = ++s;
    while (s < end && *s != '"') {
        if (*s == '\\' && s + 1 < end) s++;
        s++;
    }
    if (s >= end) return 0;
    *vl = (int)(s - *v);
    *p = s + 1;
    return 1;
}

static void state_load(drz_inst_t *I, const char *blob) {
    const char *end = blob + strlen(blob);
    const char *p = blob, *k, *v;
    int kl, vl, ok = 0;
    /* The format tag must be there and be ours, or the patch is left alone (plan/12 section 3). */
    while (next_pair(&p, end, &k, &kl, &v, &vl))
        if (kl == 3 && strncmp(k, "fmt", 3) == 0) {
            ok = vl == 7 && strncmp(v, "drizzle", 7) == 0;
            break;
        }
    if (!ok) return;
    params_defaults(I); /* missing keys fall back to INIT */
    p = blob;
    while (next_pair(&p, end, &k, &kl, &v, &vl)) {
        if (kl == 4 && strncmp(k, "name", 4) == 0) {
            int o = 0;
            for (int i = 0; i < vl && o < (int)sizeof I->name - 1; i++) {
                if (v[i] == '\\' && i + 1 < vl) i++;
                if (v[i] >= 32 && v[i] < 127) I->name[o++] = v[i];
            }
            I->name[o] = 0;
            continue;
        }
        char key[32];
        if (kl <= 0 || kl >= (int)sizeof key) continue;
        memcpy(key, k, (size_t)kl);
        key[kl] = 0;
        int id = params_find(key);
        float f;
        if (id >= 0 && parse_value(id, v, vl, &f)) I->v[id] = f;
    }
    I->pdirty = 1;
    /* A new patch: voices and grains fade, the ring starts over, and a saved freeze is redone once
     * the source has filled buf_len (plan/05 section 3). */
    for (int vi = 0; vi < DRZ_VOICES; vi++) {
        I->voice[vi].gate = 0;
        voice_kill_grains(I, vi);
    }
    buffer_reset(I);
    if (I->v[P_BUF_FREEZE] >= 0.5f) I->buf.wait_full = 1;
}

/* ------------------------------------------------------------------ contract (plan/11) */

static char g_chain_params[4096];
static int g_chain_len;
static char g_ui_hierarchy[1024];
static int g_ui_len;

static int append(char *buf, int cap, int n, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static int append(char *buf, int cap, int n, const char *fmt, ...) {
    if (n < 0 || n >= cap) return -1;
    va_list ap;
    va_start(ap, fmt);
    int m = vsnprintf(buf + n, (size_t)(cap - n), fmt, ap);
    va_end(ap);
    return m < 0 || m >= cap - n ? -1 : n + m;
}

static const char *const MAIN_KNOBS[8] = {"gr_pos", "gr_spray", "gr_size", "gr_dens",
                                          "gr_jit", "buf_len", "buf_freeze", "patch_vol"};
static const char *const SOURCE_KNOBS[3] = {"src_root", "an_a_wave", "gr_spread"};

void params_build_contract(void) {
    if (g_chain_len > 0) return;
    char *b = g_chain_params;
    int cap = (int)sizeof g_chain_params, n = 0;
    n = append(b, cap, n, "[");
    for (int i = 0; i < P_COUNT; i++) {
        const param_desc_t *d = &DRZ_PARAMS[i];
        if (!d->cp) continue;
        n = append(b, cap, n, "%s{\"key\":\"%s\",\"name\":\"%s\",\"short_name\":\"%s\"", n > 1 ? "," : "", d->key,
                   d->name, d->short_name);
        if (d->kind == PK_ENUM) {
            n = append(b, cap, n, ",\"type\":\"enum\",\"options\":[");
            for (int o = 0; d->options[o]; o++) n = append(b, cap, n, "%s\"%s\"", o ? "," : "", d->options[o]);
            n = append(b, cap, n, "],\"short_options\":[");
            for (int o = 0; d->short_options[o]; o++) n = append(b, cap, n, "%s\"%s\"", o ? "," : "", d->short_options[o]);
            n = append(b, cap, n, "],\"default\":%d}", (int)d->def);
        } else if (d->kind == PK_INT) {
            n = append(b, cap, n, ",\"type\":\"int\",\"min\":%d,\"max\":%d,\"step\":1,\"default\":%d}", (int)d->min,
                       (int)d->max, (int)d->def);
        } else {
            n = append(b, cap, n, ",\"type\":\"float\",\"min\":%g,\"max\":%g,\"default\":%g}", (double)d->min,
                       (double)d->max, (double)d->def);
        }
    }
    n = append(b, cap, n, "]");
    g_chain_len = n;

    b = g_ui_hierarchy;
    cap = (int)sizeof g_ui_hierarchy;
    n = append(b, cap, 0, "{\"levels\":{\"root\":{\"label\":\"Drizzle\",\"knobs\":[");
    for (int i = 0; i < 8; i++) n = append(b, cap, n, "%s\"%s\"", i ? "," : "", MAIN_KNOBS[i]);
    n = append(b, cap, n, "],\"params\":[");
    for (int i = 0; i < 8; i++) n = append(b, cap, n, "\"%s\",", MAIN_KNOBS[i]);
    n = append(b, cap, n, "{\"level\":\"source\",\"label\":\"Source\"}]},\"source\":{\"label\":\"Source\",\"knobs\":[");
    for (int i = 0; i < 3; i++) n = append(b, cap, n, "%s\"%s\"", i ? "," : "", SOURCE_KNOBS[i]);
    n = append(b, cap, n, "],\"params\":[");
    for (int i = 0; i < 3; i++) n = append(b, cap, n, "%s\"%s\"", i ? "," : "", SOURCE_KNOBS[i]);
    n = append(b, cap, n, "]}}}");
    g_ui_len = n;
}

/* ------------------------------------------------------------------ get / set */

void params_set(drz_inst_t *I, const char *key, const char *val) {
    if (!key || !val) return;
    if (strcmp(key, "state") == 0) {
        state_load(I, val);
        return;
    }
    int id = params_find(key);
    float f;
    if (id < 0 || !parse_value(id, val, (int)strlen(val), &f)) return;
    if (f == I->v[id]) return; /* Scenes resend unchanged values every block */
    I->v[id] = f;
    I->pdirty = 1;
}

int params_get(drz_inst_t *I, const char *key, char *buf, int buf_len) {
    if (!key || !buf || buf_len <= 0) return -1;
    if (strcmp(key, "chain_params") == 0) return copy_out(g_chain_params, g_chain_len, buf, buf_len);
    if (strcmp(key, "ui_hierarchy") == 0) return copy_out(g_ui_hierarchy, g_ui_len, buf, buf_len);
    if (strcmp(key, "state") == 0) return state_write(I, buf, buf_len);
    if (strcmp(key, "name") == 0 || strcmp(key, "preset_name") == 0)
        return copy_out(I->name, (int)strlen(I->name), buf, buf_len);
#ifdef DRZ_PROFILE
    if (strcmp(key, "cpu_us") == 0) {
        int n = snprintf(buf, (size_t)buf_len, "%.1f", (double)I->cpu_us);
        return n >= buf_len ? -1 : n;
    }
#endif
    int id = params_find(key);
    if (id < 0) return -1;
    return format_value(id, I->v[id], buf, buf_len);
}
