// GameLock - persistencia em sdmc:/config/GameLock/config.json.
//
// Somente o cartao SD (RULES §4). Nada de NAND, saves ou rede.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <switch.h>

#include "pin.h"

#define CONFIG_DIR  "sdmc:/config/GameLock"
#define CONFIG_PATH CONFIG_DIR "/config.json"

typedef struct {
    u64   *ids;
    size_t count;
    size_t cap;
} IdList;

typedef struct {
    bool   has_pin;
    u8     salt[PIN_SALT_SIZE];
    u8     hash[PIN_HASH_SIZE];
    u32    iterations;

    bool   protection_enabled;
    u32    lang;         // 0 = portugues, 1 = ingles (textos da interface)

    IdList locked;       // Title IDs que o responsavel quer protegidos
    IdList authorized;   // protegidos, porem liberados ate travar de novo
    IdList blocked;      // titulos cujos stubs FORAM criados por esta skill
} Config;

// Cria sdmc:/config e sdmc:/config/GameLock se faltarem. false em falha.
bool config_ensure_dirs(void);

// Le o config.json. Sempre deixa *cfg utilizavel: arquivo ausente ou invalido
// resulta em configuracao vazia (sem PIN, sem jogo protegido).
// *out_missing = arquivo nao existe. *out_invalid = existe mas nao foi entendido.
void config_load(Config *cfg, bool *out_missing, bool *out_invalid);

// Grava via arquivo temporario + rename. false se falhou (nesse caso o
// config.json anterior continua intacto).
bool config_save(const Config *cfg);

bool config_is_locked(const Config *cfg, u64 tid);
bool config_set_locked(Config *cfg, u64 tid, bool locked);

bool config_is_authorized(const Config *cfg, u64 tid);
bool config_set_authorized(Config *cfg, u64 tid, bool authorized);
void config_clear_authorizations(Config *cfg);

// Registro de posse: so removemos stub de titulo que nos mesmos criamos.
bool config_is_blocked(const Config *cfg, u64 tid);
bool config_set_blocked(Config *cfg, u64 tid, bool blocked);

void config_set_pin(Config *cfg, const char *pin);
void config_clear_pin(Config *cfg);
bool config_check_pin(const Config *cfg, const char *pin);

void config_free(Config *cfg);
