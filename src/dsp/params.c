/* Parameter store: get/set, state blob, factory bank and browser, display keys (plan/10, 11, 12).
 * The table, the contract strings and the bank are generated from tools/gen/params.py. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drizzle.h"
#include "dsp_util.h"

float map_size_s(float x) { return 0.005f * powf(400.0f, clampf(x, 0.0f, 1.0f)); }
float map_dens_hz(float x) { return 0.5f * powf(500.0f, clampf(x, 0.0f, 1.0f)); }
float map_buf_len_s(float x) { return 0.5f * powf(16.0f, clampf(x, 0.0f, 1.0f)); }
float map_scan(float x) { return x * x * x * (1.0f / 16.0f); }

/* Binary search over the generated key order. */
int params_find(const char *key) {
    int lo = 0, hi = P_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2, id = DRZ_PARAMS_SORTED[mid];
        int c = strcmp(key, DRZ_PARAMS[id].key);
        if (c == 0) return id;
        if (c < 0) hi = mid - 1;
        else lo = mid + 1;
    }
    return -1;
}

/* Keys whose change alters what the source renders: in Scan mode they restart the tape (plan/05 4). */
static int is_source_key(int id) {
    const char *k = DRZ_PARAMS[id].key;
    return strncmp(k, "src_", 4) == 0 || strncmp(k, "an_", 3) == 0 || strncmp(k, "wt_", 3) == 0 ||
           strncmp(k, "sw_", 3) == 0 || strncmp(k, "sl_", 3) == 0 || id == P_BUF_LEN;
}

void params_defaults(drz_inst_t *I) {
    for (int i = 0; i < P_COUNT; i++) I->v[i] = DRZ_PARAMS[i].def;
    snprintf(I->name, sizeof I->name, "%s", DRZ_FACTORY_NAMES[0]);
    I->preset_idx = 0;
    I->pdirty = 1;
}

/* Parse a value for parameter id: a number, or an enum option's name. 0 for junk, nan and inf. */
static int parse_value(int id, const char *val, int len, float *out) {
    const param_desc_t *d = &DRZ_PARAMS[id];
    if (d->kind == PK_ENUM) {
        for (int o = 0; o < d->nopts; o++)
            if ((int)strlen(d->opts[o]) == len && strncmp(d->opts[o], val, (size_t)len) == 0) {
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
    *out = clampf(f, d->mn, d->mx);
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

static int put_int(int v, char *buf, int cap) {
    int n = snprintf(buf, (size_t)cap, "%d", v);
    return n >= cap ? -1 : n;
}

/* ------------------------------------------------------------------ matrix proxies (plan/08 section 4) */

static int sel_slot(const drz_inst_t *I) { return clampi((int)I->v[P_MM_SEL], 0, DRZ_MM_SLOTS - 1); }

/* The proxies show the selected slot; refresh them after the selection or the slots changed. */
static void proxies_refresh(drz_inst_t *I) {
    int base = P_MM01_SRC + 4 * sel_slot(I);
    I->v[P_MM_SLOT] = (float)(sel_slot(I) + 1);
    I->v[P_MM_SRC] = I->v[base];
    I->v[P_MM_VIA] = I->v[base + 1];
    I->v[P_MM_DST] = I->v[base + 2];
    I->v[P_MM_DEP] = I->v[base + 3];
    I->v[P_MM_CLEAR] = 0.0f;
}

/* A write to a proxy lands in the slot it stands for. */
static void proxy_written(drz_inst_t *I, int id) {
    int base = P_MM01_SRC + 4 * sel_slot(I);
    switch (id) {
    case P_MM_SEL: break;
    case P_MM_SLOT: I->v[P_MM_SEL] = I->v[P_MM_SLOT] - 1.0f; break;
    case P_MM_SRC: I->v[base] = I->v[P_MM_SRC]; break;
    case P_MM_VIA: I->v[base + 1] = I->v[P_MM_VIA]; break;
    case P_MM_DST: I->v[base + 2] = I->v[P_MM_DST]; break;
    case P_MM_DEP: I->v[base + 3] = I->v[P_MM_DEP]; break;
    case P_MM_CLEAR:
        if (I->v[P_MM_CLEAR] >= 0.5f)
            for (int i = 0; i < 4; i++) I->v[base + i] = 0.0f;
        break;
    default:
        if (id >= P_MM01_SRC && id <= P_MM12_DEP) break; /* a raw slot: the proxies follow */
        return;
    }
    proxies_refresh(I);
}

/* A new patch: every voice and grain fades, the buffer starts over (a Scan tape renders at once, a
 * saved Live freeze waits for the ring to refill), and the effect tails are dropped. */
static void patch_loaded(drz_inst_t *I) {
    I->pdirty = 1;
    proxies_refresh(I);
    voices_all_off(I, 1);
    buffer_reset(I);
    if (I->v[P_BUF_FREEZE] >= 0.5f) I->buf.wait_full = 1;
    fx_reset(&I->fx);
}

/* ------------------------------------------------------------------ factory bank and browser */

static void preset_load(drz_inst_t *I, int idx) {
    if (idx < 0 || idx >= DRZ_FACTORY_COUNT) return;
    memcpy(I->v, DRZ_FACTORY_VALUES[idx], sizeof I->v);
    snprintf(I->name, sizeof I->name, "%s", DRZ_FACTORY_NAMES[idx]);
    I->preset_idx = idx;
    I->browse_last = idx;
    patch_loaded(I);
}

/* Move's Factory Patches page steps through one category (pb_cat), as Crest's does. */
static int browse_count(const drz_inst_t *I) {
    return I->browse_cat <= 0 ? DRZ_FACTORY_COUNT : DRZ_BROWSE_CATS[I->browse_cat - 1].count;
}

static int browse_preset(const drz_inst_t *I, int pos) {
    return I->browse_cat <= 0 ? pos : DRZ_BROWSE_ORDER[DRZ_BROWSE_CATS[I->browse_cat - 1].first + pos];
}

static int browse_pos(const drz_inst_t *I) {
    int cur = I->preset_idx >= 0 ? I->preset_idx : I->browse_last;
    if (cur < 0 || cur >= DRZ_FACTORY_COUNT) return -1;
    if (I->browse_cat <= 0) return cur;
    const drz_browse_cat_t *c = &DRZ_BROWSE_CATS[I->browse_cat - 1];
    for (int i = 0; i < c->count; i++)
        if (DRZ_BROWSE_ORDER[c->first + i] == cur) return i;
    return -1;
}

/* ------------------------------------------------------------------ state (plan/12 section 2) */

typedef struct {
    char *b;
    int cap, n, ok;
} wr_t;

static void w_str(wr_t *w, const char *s) {
    int l = (int)strlen(s);
    if (!w->ok || w->n + l + 1 > w->cap) {
        w->ok = 0;
        return;
    }
    memcpy(w->b + w->n, s, (size_t)l + 1);
    w->n += l;
}

static int state_save(const drz_inst_t *I, char *buf, int cap) {
    wr_t w = {buf, cap, 0, cap > 0};
    if (cap > 0) buf[0] = 0;
    w_str(&w, "{\"fmt\":\"drizzle\",\"v\":\"1\",\"name\":\"");
    for (const char *p = I->name; *p; p++) {
        char c[3] = {0, 0, 0};
        if (*p == '"' || *p == '\\') {
            c[0] = '\\';
            c[1] = *p;
        } else if ((unsigned char)*p >= 32 && (unsigned char)*p < 127) {
            c[0] = *p;
        }
        w_str(&w, c);
    }
    w_str(&w, "\"");
    for (int i = 0; i < P_COUNT; i++) {
        if (!(DRZ_PARAMS[i].flags & PF_STATE)) continue;
        char val[24];
        if (format_value(i, I->v[i], val, sizeof val) < 0) return -1;
        w_str(&w, ",\"");
        w_str(&w, DRZ_PARAMS[i].key);
        w_str(&w, "\":\"");
        w_str(&w, val);
        w_str(&w, "\"");
    }
    w_str(&w, "}");
    return w.ok ? w.n : -1;
}

/* The next "key":"value" pair from *p, bounded by end; 0 at the end or on junk. */
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

/* Tolerant single pass (plan/12 sections 1-3): the blob must carry "fmt":"drizzle", unknown keys are
 * ignored, missing ones keep their INIT value. Never reads past the terminating NUL. */
static void state_load(drz_inst_t *I, const char *blob) {
    const char *end = blob + strlen(blob);
    const char *p = blob, *k, *v;
    int kl, vl, ok = 0;
    while (next_pair(&p, end, &k, &kl, &v, &vl))
        if (kl == 3 && strncmp(k, "fmt", 3) == 0) {
            ok = vl == 7 && strncmp(v, "drizzle", 7) == 0;
            break;
        }
    if (!ok) return;
    for (int i = 0; i < P_COUNT; i++)
        if (DRZ_PARAMS[i].flags & PF_STATE) I->v[i] = DRZ_PARAMS[i].def;
    snprintf(I->name, sizeof I->name, "Init");
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
        char key[40];
        if (kl <= 0 || kl >= (int)sizeof key) continue;
        memcpy(key, k, (size_t)kl);
        key[kl] = 0;
        int id = params_find(key);
        float f;
        if (id >= 0 && (DRZ_PARAMS[id].flags & PF_STATE) && parse_value(id, v, vl, &f)) I->v[id] = f;
    }
    I->preset_idx = -1;
    patch_loaded(I);
}

/* ------------------------------------------------------------------ display (plan/11 section 4) */

/* "<mode><frozen><progress><head>" then 128 bins, all hex: mode L/S, frozen 0/1, progress 00..FF of a
 * running tape render (FF = none), head position 00..FF along the readable range. */
static int buf_view(const drz_inst_t *I, char *buf, int cap) {
    static const char HEX[] = "0123456789ABCDEF";
    if (cap < 6 + 2 * DRZ_BINS + 1) return -1;
    const buffer_t *b = &I->buf;
    int scan = b->mode == BUF_MODE_SCAN;
    int prog = scan && b->rendering && b->render_len > 0 ? (int)(255LL * b->render_k / b->render_len) : 255;
    if (prog > 254 && scan && b->rendering) prog = 254;
    int head = 255; /* Live: the newest sample is at the right edge */
    buf[0] = scan ? 'S' : 'L';
    buf[1] = b->frozen ? '1' : '0';
    buf[2] = HEX[prog >> 4];
    buf[3] = HEX[prog & 15];
    buf[4] = HEX[head >> 4];
    buf[5] = HEX[head & 15];
    const uint8_t *bins = b->bins[scan && b->front >= 0 ? b->front : 0];
    for (int i = 0; i < DRZ_BINS; i++) {
        buf[6 + 2 * i] = HEX[bins[i] >> 4];
        buf[7 + 2 * i] = HEX[bins[i] & 15];
    }
    buf[6 + 2 * DRZ_BINS] = 0;
    return 6 + 2 * DRZ_BINS;
}

/* ------------------------------------------------------------------ set / get */

void params_set(drz_inst_t *I, const char *key, const char *val) {
    if (!key || !val) return;
    if (strcmp(key, "state") == 0) {
        state_load(I, val);
        return;
    }
    if (strcmp(key, "preset") == 0) {
        preset_load(I, atoi(val));
        return;
    }
    if (strcmp(key, "pb_cat") == 0) { /* browse state, not sound: never in the blob */
        I->browse_cat = clampi(atoi(val), 0, DRZ_BROWSE_CAT_COUNT);
        return;
    }
    if (strcmp(key, "pb_index") == 0) {
        int pos = atoi(val);
        if (pos >= 0 && pos < browse_count(I)) preset_load(I, browse_preset(I, pos));
        return;
    }
    if (strcmp(key, "name") == 0) {
        snprintf(I->name, sizeof I->name, "%s", val);
        return;
    }
    int id = params_find(key);
    float f;
    if (id < 0 || !parse_value(id, val, (int)strlen(val), &f)) return;
    if (f == I->v[id] && id != P_MM_CLEAR) return; /* Scenes resend unchanged values every block */
    I->v[id] = f;
    I->pdirty = 1;
    proxy_written(I, id);
    if (is_source_key(id)) buffer_source_changed(I);
}

int params_get(drz_inst_t *I, const char *key, char *buf, int buf_len) {
    if (!key || !buf || buf_len < 1) return -1;
    if (strcmp(key, "chain_params") == 0) return copy_out(DRZ_CHAIN_PARAMS, DRZ_CHAIN_PARAMS_LEN, buf, buf_len);
    if (strcmp(key, "ui_hierarchy") == 0) return copy_out(DRZ_UI_HIERARCHY, DRZ_UI_HIERARCHY_LEN, buf, buf_len);
    if (strcmp(key, "state") == 0) return state_save(I, buf, buf_len);
    if (strcmp(key, "preset") == 0) return put_int(I->preset_idx, buf, buf_len);
    if (strcmp(key, "preset_count") == 0) return put_int(DRZ_FACTORY_COUNT, buf, buf_len);
    if (strcmp(key, "preset_name") == 0 || strcmp(key, "name") == 0)
        return copy_out(I->name, (int)strlen(I->name), buf, buf_len);
    if (strcmp(key, "pb_cats") == 0)
        return copy_out(DRZ_BROWSE_CATS_JSON, (int)strlen(DRZ_BROWSE_CATS_JSON), buf, buf_len);
    if (strcmp(key, "pb_cat") == 0) return put_int(I->browse_cat, buf, buf_len);
    if (strcmp(key, "pb_count") == 0) return put_int(browse_count(I), buf, buf_len);
    if (strcmp(key, "pb_index") == 0) return put_int(browse_pos(I), buf, buf_len);
    if (strcmp(key, "buf_view") == 0) return buf_view(I, buf, buf_len);
    if (strcmp(key, "mm_items") == 0) return mm_items(I, buf, buf_len);
    if (strcmp(key, "grain_view") == 0) return grains_view(I, buf, buf_len);
    if (strcmp(key, "smp_status") == 0) return copy_out("none", 4, buf, buf_len);
    if (strcmp(key, "split_voices") == 0) return copy_out("", 0, buf, buf_len);
#ifdef DRZ_PROFILE
    if (strcmp(key, "cpu_us") == 0) {
        int n = snprintf(buf, (size_t)buf_len, "%.1f", (double)I->cpu_us);
        return n >= buf_len ? -1 : n;
    }
#endif
    const char *colon = strchr(key, ':');
    if (colon && strcmp(colon, ":effective") == 0) { /* the newest voice's modulated value (mod.c) */
        char base[40];
        int kl = (int)(colon - key);
        if (kl <= 0 || kl >= (int)sizeof base) return -1;
        memcpy(base, key, (size_t)kl);
        base[kl] = 0;
        int id = params_find(base);
        if (id < 0 || !(DRZ_PARAMS[id].flags & PF_LIVE)) return -1;
        int e = id == P_GR_POS ? 0 : id == P_GR_SIZE ? 1 : id == P_GR_DENS ? 2 : 3, any = 0;
        for (int i = 0; i < DRZ_VOICES; i++) any |= I->voice[i].active;
        return format_value(id, any ? I->eff[e] : I->v[id], buf, buf_len);
    }
    int id = params_find(key);
    if (id < 0) return -1;
    return format_value(id, I->v[id], buf, buf_len);
}
