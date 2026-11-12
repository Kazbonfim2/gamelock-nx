#include "relock.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "block.h"   // BLOCK_CONTENTS_ROOT

#define RELOCK_DIR   BLOCK_CONTENTS_ROOT "/" RELOCK_TID
#define RELOCK_FLAGS RELOCK_DIR "/flags"
#define RELOCK_NSP   RELOCK_DIR "/exefs.nsp"
#define RELOCK_TMP   RELOCK_DIR "/exefs.nsp.tmp"
#define RELOCK_BOOT2 RELOCK_FLAGS "/boot2.flag"

// O nome dentro do romfs e o do arquivo que o Makefile copia para romfs/.
#define RELOCK_SOURCE "romfs:/GameLockRelock.nsp"

#define COPY_CHUNK 8192

static bool mkdir_ok(const char *path)
{
    return (mkdir(path, 0777) == 0) || (errno == EEXIST);
}

static bool file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static long file_size(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return -1;
    }
    return (long)st.st_size;
}

RelockState relock_state(void)
{
    const bool nsp  = file_exists(RELOCK_NSP);
    const bool flag = file_exists(RELOCK_BOOT2);

    if (nsp && flag) {
        return RELOCK_READY;
    }
    if (nsp || flag) {
        return RELOCK_PARTIAL;
    }
    return RELOCK_ABSENT;
}

bool relock_embedded(void)
{
    return file_size(RELOCK_SOURCE) > 0;
}

bool relock_up_to_date(void)
{
    const long mine = file_size(RELOCK_SOURCE);
    const long card = file_size(RELOCK_NSP);

    return mine > 0 && mine == card;
}

static bool copy_to_tmp(void)
{
    FILE *in = fopen(RELOCK_SOURCE, "rb");
    if (in == NULL) {
        return false;
    }

    FILE *out = fopen(RELOCK_TMP, "wb");
    if (out == NULL) {
        fclose(in);
        return false;
    }

    bool ok = true;
    char buf[COPY_CHUNK];
    size_t n;

    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            ok = false;
            break;
        }
    }

    if (ferror(in) != 0) {
        ok = false;
    }

    // Cartao cheio ou removido no meio so aparece no fflush/fclose.
    ok = ok && (fflush(out) == 0) && (ferror(out) == 0);
    ok = (fclose(out) == 0) && ok;
    fclose(in);

    if (!ok) {
        remove(RELOCK_TMP);
    }
    return ok;
}

bool relock_install(void)
{
    if (!relock_embedded()) {
        return false;
    }

    if (!mkdir_ok("sdmc:/atmosphere") || !mkdir_ok(BLOCK_CONTENTS_ROOT) ||
        !mkdir_ok(RELOCK_DIR) || !mkdir_ok(RELOCK_FLAGS)) {
        return false;
    }

    if (!copy_to_tmp()) {
        return false;
    }

    remove(RELOCK_NSP);  // FAT: rename sobre arquivo existente falha
    if (rename(RELOCK_TMP, RELOCK_NSP) != 0) {
        remove(RELOCK_TMP);
        return false;
    }

    // A flag so entra com o nsp inteiro no lugar: na ordem inversa, um boot
    // entre as duas etapas tentaria carregar um exefs.nsp que nao existe.
    FILE *f = fopen(RELOCK_BOOT2, "wb");
    if (f == NULL) {
        return false;
    }
    return fclose(f) == 0;
}

bool relock_uninstall(void)
{
    bool ok = true;

    // A flag primeiro: ela e o que arma o boot. Se a remocao parar aqui, o
    // resultado e um nsp inerte no cartao, nao um sysmodule meio instalado.
    if (file_exists(RELOCK_BOOT2) && remove(RELOCK_BOOT2) != 0) {
        ok = false;
    }
    if (file_exists(RELOCK_NSP) && remove(RELOCK_NSP) != 0) {
        ok = false;
    }

    // rmdir falha de proposito se sobrou algo dentro.
    rmdir(RELOCK_FLAGS);
    rmdir(RELOCK_DIR);

    return ok;
}
