#include "block.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Ordem e nomes conforme ldr_process_creation.cpp:63-85.
static const char *const kNsoNames[] = {
    "rtld", "main",
    "subsdk0", "subsdk1", "subsdk2", "subsdk3", "subsdk4",
    "subsdk5", "subsdk6", "subsdk7", "subsdk8", "subsdk9",
    "sdk",
};
#define NSO_COUNT (sizeof(kNsoNames) / sizeof(kNsoNames[0]))

#define CONTENTS_ROOT "sdmc:/atmosphere/contents"

static void path_title(char *out, size_t size, u64 tid)
{
    snprintf(out, size, CONTENTS_ROOT "/%016lX", tid);
}

static void path_exefs(char *out, size_t size, u64 tid)
{
    snprintf(out, size, CONTENTS_ROOT "/%016lX/exefs", tid);
}

static void path_nso(char *out, size_t size, u64 tid, const char *nso, const char *suffix)
{
    snprintf(out, size, CONTENTS_ROOT "/%016lX/exefs/%s%s", tid, nso, suffix);
}

static bool mkdir_ok(const char *path)
{
    return (mkdir(path, 0777) == 0) || (errno == EEXIST);
}

static bool file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static bool touch_empty(const char *path)
{
    if (file_exists(path)) {
        return true;
    }

    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return false;
    }
    return fclose(f) == 0;
}

bool block_apply(u64 tid)
{
    char path[256];

    // Trava de seguranca: nunca bloquear o processo em que o GameLock roda (o
    // forwarder do HOME). Isso trancaria o usuario fora do proprio aplicativo.
    const u64 self_tid = games_self_program_id();
    if (self_tid != 0 && tid == self_tid) {
        return false;
    }

    if (!mkdir_ok("sdmc:/atmosphere") || !mkdir_ok(CONTENTS_ROOT)) {
        return false;
    }

    path_title(path, sizeof(path), tid);
    if (!mkdir_ok(path)) {
        return false;
    }

    path_exefs(path, sizeof(path), tid);
    if (!mkdir_ok(path)) {
        return false;
    }

    bool ok = true;
    for (size_t i = 0; i < NSO_COUNT; i++) {
        path_nso(path, sizeof(path), tid, kNsoNames[i], ".stub");
        if (!touch_empty(path)) {
            ok = false;
        }
    }

    return ok;
}

bool block_release(u64 tid)
{
    char path[256];
    bool ok = true;

    for (size_t i = 0; i < NSO_COUNT; i++) {
        path_nso(path, sizeof(path), tid, kNsoNames[i], ".stub");
        if (file_exists(path) && remove(path) != 0) {
            ok = false;
        }
    }

    // rmdir falha se o diretorio nao estiver vazio -- e isso que queremos:
    // mod de LayeredFS, cheats ou exefs do usuario ficam intactos.
    path_exefs(path, sizeof(path), tid);
    rmdir(path);

    path_title(path, sizeof(path), tid);
    rmdir(path);

    return ok;
}

bool block_has_loose_nso(u64 tid)
{
    char path[256];

    for (size_t i = 0; i < NSO_COUNT; i++) {
        path_nso(path, sizeof(path), tid, kNsoNames[i], "");
        if (file_exists(path)) {
            return true;
        }
    }
    return false;
}

static bool should_block(const Config *cfg, u64 tid)
{
    return cfg->protection_enabled &&
           config_is_locked(cfg, tid) &&
           !config_is_authorized(cfg, tid);
}

void block_sync(Config *cfg, BlockSyncResult *out)
{
    memset(out, 0, sizeof(*out));

    // 1) Aplicar nos titulos marcados que devem ficar bloqueados.
    for (size_t i = 0; i < cfg->locked.count; i++) {
        const u64 tid = cfg->locked.ids[i];
        if (!should_block(cfg, tid)) {
            continue;
        }

        if (block_has_loose_nso(tid)) {
            out->conflicts++;
            if (out->first_conflict == 0) {
                out->first_conflict = tid;
            }
        }

        if (!block_apply(tid)) {
            out->failures++;
            continue;
        }

        out->applied++;
        if (!config_is_blocked(cfg, tid)) {
            config_set_blocked(cfg, tid, true);
            out->config_changed = true;
        }
    }

    // 2) Liberar os titulos cujo stub e nosso e que nao devem mais ficar
    //    bloqueados. De tras para frente porque a lista encolhe durante o laco.
    for (size_t i = cfg->blocked.count; i > 0; i--) {
        const u64 tid = cfg->blocked.ids[i - 1];
        if (should_block(cfg, tid)) {
            continue;
        }

        if (!block_release(tid)) {
            out->failures++;
            continue;
        }

        out->released++;
        config_set_blocked(cfg, tid, false);
        out->config_changed = true;
    }
}
