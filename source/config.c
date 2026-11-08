#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CONFIG_TMP     CONFIG_PATH ".tmp"
#define CONFIG_VERSION 2        // v1 (sem authorized_titles) ainda e lido
#define CONFIG_MAX_LEN (256 * 1024)

// ponytail: leitor de JSON por busca de chave, nao um parser generico. Le
// exatamente o formato que config_save escreve; qualquer coisa fora disso conta
// como invalida e cai no padrao. Trocar por uma lib so se o formato crescer.

// ------------------------------------------------------------------ IdList

static bool idlist_has(const IdList *list, u64 tid)
{
    for (size_t i = 0; i < list->count; i++) {
        if (list->ids[i] == tid) {
            return true;
        }
    }
    return false;
}

static bool idlist_set(IdList *list, u64 tid, bool present)
{
    for (size_t i = 0; i < list->count; i++) {
        if (list->ids[i] != tid) {
            continue;
        }
        if (!present) {
            list->ids[i] = list->ids[list->count - 1];
            list->count--;
        }
        return true;
    }

    if (!present) {
        return true;
    }

    if (list->count == list->cap) {
        const size_t cap = (list->cap == 0) ? 16 : list->cap * 2;
        u64 *grown = realloc(list->ids, cap * sizeof(*grown));
        if (grown == NULL) {
            return false;
        }
        list->ids = grown;
        list->cap = cap;
    }

    list->ids[list->count++] = tid;
    return true;
}

static void idlist_free(IdList *list)
{
    free(list->ids);
    list->ids = NULL;
    list->count = 0;
    list->cap = 0;
}

// -------------------------------------------------------------- leitura JSON

static const char *value_of(const char *json, const char *key)
{
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);

    const char *at = strstr(json, needle);
    if (at == NULL) {
        return NULL;
    }

    at = strchr(at + strlen(needle), ':');
    if (at == NULL) {
        return NULL;
    }
    at++;

    while (*at == ' ' || *at == '\t' || *at == '\n' || *at == '\r') {
        at++;
    }
    return at;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool read_hex_string(const char *at, u8 *out, size_t want)
{
    if (at == NULL || *at != '"') {
        return false;
    }
    at++;

    for (size_t i = 0; i < want; i++) {
        const int hi = hex_digit(at[i * 2]);
        const int lo = hex_digit(at[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out[i] = (u8)((hi << 4) | lo);
    }

    return at[want * 2] == '"';
}

static bool read_bool(const char *at, bool *out)
{
    if (at == NULL) {
        return false;
    }
    if (strncmp(at, "true", 4) == 0) {
        *out = true;
        return true;
    }
    if (strncmp(at, "false", 5) == 0) {
        *out = false;
        return true;
    }
    return false;
}

static bool read_u32(const char *at, u32 *out)
{
    if (at == NULL || *at < '0' || *at > '9') {
        return false;
    }
    *out = (u32)strtoul(at, NULL, 10);
    return true;
}

// Le ["0100...", "0100..."] para a lista. Ausencia da chave = lista vazia, para
// o config v1 continuar valido.
static bool read_id_array(const char *json, const char *key, IdList *out, bool optional)
{
    const char *at = value_of(json, key);
    if (at == NULL) {
        return optional;
    }
    if (*at != '[') {
        return false;
    }
    at++;

    for (;;) {
        while (*at == ' ' || *at == '\t' || *at == '\n' || *at == '\r' || *at == ',') {
            at++;
        }
        if (*at == ']') {
            return true;
        }
        if (*at != '"') {
            return false;
        }
        at++;

        u64 tid = 0;
        for (int i = 0; i < 16; i++) {
            const int d = hex_digit(at[i]);
            if (d < 0) {
                return false;
            }
            tid = (tid << 4) | (u64)d;
        }
        if (at[16] != '"') {
            return false;
        }
        at += 17;

        if (!idlist_set(out, tid, true)) {
            return false;
        }
    }
}

// ------------------------------------------------------------------ arquivo

bool config_ensure_dirs(void)
{
    if (mkdir("sdmc:/config", 0777) != 0 && errno != EEXIST) {
        return false;
    }
    if (mkdir(CONFIG_DIR, 0777) != 0 && errno != EEXIST) {
        return false;
    }
    return true;
}

static char *read_whole_file(const char *path, long *out_len)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    const long len = ftell(f);
    rewind(f);

    if (len <= 0 || len > CONFIG_MAX_LEN) {
        fclose(f);
        return NULL;
    }

    char *buf = malloc((size_t)len + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }

    const size_t read = fread(buf, 1, (size_t)len, f);
    fclose(f);

    if (read != (size_t)len) {
        free(buf);
        return NULL;
    }

    buf[len] = '\0';
    *out_len = len;
    return buf;
}

static void config_reset(Config *cfg)
{
    config_free(cfg);
    memset(cfg, 0, sizeof(*cfg));
    cfg->protection_enabled = true;
    cfg->iterations = PIN_ITERATIONS;
}

void config_load(Config *cfg, bool *out_missing, bool *out_invalid)
{
    memset(cfg, 0, sizeof(*cfg));
    config_reset(cfg);

    *out_missing = false;
    *out_invalid = false;

    long len = 0;
    char *json = read_whole_file(CONFIG_PATH, &len);
    if (json == NULL) {
        *out_missing = true;
        return;
    }

    u32 version = 0;
    bool enabled = true;
    bool has_pin = false;

    if (!read_u32(value_of(json, "version"), &version) ||
        version < 1 || version > CONFIG_VERSION ||
        !read_bool(value_of(json, "protection_enabled"), &enabled) ||
        !read_bool(value_of(json, "has_pin"), &has_pin)) {
        free(json);
        *out_invalid = true;
        return;
    }

    cfg->protection_enabled = enabled;

    // Chave ausente = portugues: config gravado por versao anterior a 1.0.0
    // nao tem "lang" e continua valido.
    u32 lang = 0;
    if (read_u32(value_of(json, "lang"), &lang) && lang <= 1) {
        cfg->lang = lang;
    }

    if (has_pin) {
        u32 iterations = 0;
        if (!read_u32(value_of(json, "pin_iterations"), &iterations) || iterations == 0 ||
            !read_hex_string(value_of(json, "pin_salt"), cfg->salt, PIN_SALT_SIZE) ||
            !read_hex_string(value_of(json, "pin_hash"), cfg->hash, PIN_HASH_SIZE)) {
            free(json);
            config_reset(cfg);
            *out_invalid = true;
            return;
        }
        cfg->iterations = iterations;
        cfg->has_pin = true;
    }

    if (!read_id_array(json, "locked_titles", &cfg->locked, false) ||
        !read_id_array(json, "authorized_titles", &cfg->authorized, true) ||
        !read_id_array(json, "blocked_titles", &cfg->blocked, true)) {
        free(json);
        config_reset(cfg);
        *out_invalid = true;
        return;
    }

    free(json);
}

static void write_hex(FILE *f, const u8 *data, size_t size)
{
    for (size_t i = 0; i < size; i++) {
        fprintf(f, "%02x", data[i]);
    }
}

static void write_id_array(FILE *f, const char *key, const IdList *list, bool last)
{
    fprintf(f, "  \"%s\": [", key);
    for (size_t i = 0; i < list->count; i++) {
        fprintf(f, "%s\n    \"%016lX\"", (i == 0) ? "" : ",", list->ids[i]);
    }
    fprintf(f, "%s]%s\n", (list->count == 0) ? "" : "\n  ", last ? "" : ",");
}

bool config_save(const Config *cfg)
{
    if (!config_ensure_dirs()) {
        return false;
    }

    FILE *f = fopen(CONFIG_TMP, "wb");
    if (f == NULL) {
        return false;
    }

    fprintf(f, "{\n");
    fprintf(f, "  \"version\": %d,\n", CONFIG_VERSION);
    fprintf(f, "  \"protection_enabled\": %s,\n", cfg->protection_enabled ? "true" : "false");
    fprintf(f, "  \"has_pin\": %s,\n", cfg->has_pin ? "true" : "false");
    fprintf(f, "  \"lang\": %u,\n", cfg->lang);

    if (cfg->has_pin) {
        fprintf(f, "  \"pin_iterations\": %u,\n", cfg->iterations);
        fprintf(f, "  \"pin_salt\": \"");
        write_hex(f, cfg->salt, PIN_SALT_SIZE);
        fprintf(f, "\",\n");
        fprintf(f, "  \"pin_hash\": \"");
        write_hex(f, cfg->hash, PIN_HASH_SIZE);
        fprintf(f, "\",\n");
    }

    write_id_array(f, "locked_titles", &cfg->locked, false);
    write_id_array(f, "authorized_titles", &cfg->authorized, false);
    write_id_array(f, "blocked_titles", &cfg->blocked, true);
    fprintf(f, "}\n");

    // Falha de escrita so aparece no fflush/fclose; sem isso daria "salvo" com
    // o cartao cheio ou removido no meio.
    const bool write_ok = (fflush(f) == 0) && (ferror(f) == 0);
    const bool close_ok = (fclose(f) == 0);

    if (!write_ok || !close_ok) {
        remove(CONFIG_TMP);
        return false;
    }

    remove(CONFIG_PATH);  // FAT: rename sobre arquivo existente falha
    if (rename(CONFIG_TMP, CONFIG_PATH) != 0) {
        return false;
    }

    return true;
}

// ------------------------------------------------------------------- estado

bool config_is_locked(const Config *cfg, u64 tid)
{
    return idlist_has(&cfg->locked, tid);
}

bool config_set_locked(Config *cfg, u64 tid, bool locked)
{
    if (!locked) {
        // Jogo que deixa de ser protegido nao guarda autorizacao pendente.
        idlist_set(&cfg->authorized, tid, false);
    }
    return idlist_set(&cfg->locked, tid, locked);
}

bool config_is_authorized(const Config *cfg, u64 tid)
{
    return idlist_has(&cfg->authorized, tid);
}

bool config_set_authorized(Config *cfg, u64 tid, bool authorized)
{
    return idlist_set(&cfg->authorized, tid, authorized);
}

void config_clear_authorizations(Config *cfg)
{
    idlist_free(&cfg->authorized);
}

bool config_is_blocked(const Config *cfg, u64 tid)
{
    return idlist_has(&cfg->blocked, tid);
}

bool config_set_blocked(Config *cfg, u64 tid, bool blocked)
{
    return idlist_set(&cfg->blocked, tid, blocked);
}

void config_set_pin(Config *cfg, const char *pin)
{
    pin_random_salt(cfg->salt);
    cfg->iterations = PIN_ITERATIONS;
    pin_derive(pin, cfg->salt, PIN_SALT_SIZE, cfg->iterations, cfg->hash);
    cfg->has_pin = true;
}

void config_clear_pin(Config *cfg)
{
    memset(cfg->salt, 0, sizeof(cfg->salt));
    memset(cfg->hash, 0, sizeof(cfg->hash));
    cfg->iterations = PIN_ITERATIONS;
    cfg->has_pin = false;
}

bool config_check_pin(const Config *cfg, const char *pin)
{
    if (!cfg->has_pin) {
        return true;
    }

    u8 got[PIN_HASH_SIZE];
    pin_derive(pin, cfg->salt, PIN_SALT_SIZE, cfg->iterations, got);
    return pin_hash_equals(got, cfg->hash);
}

void config_free(Config *cfg)
{
    idlist_free(&cfg->locked);
    idlist_free(&cfg->authorized);
    idlist_free(&cfg->blocked);
}
