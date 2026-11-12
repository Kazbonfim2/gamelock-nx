// GameLock - Etapa 2: leitura da lista de jogos instalados via ns.
//
// APIs (libnx, nx/include/switch/services/ns.h):
//   nsInitialize                     ns.h:244
//   nsListApplicationRecord          ns.h:415
//   nsGetApplicationControlData      ns.h:313
//   nsGetApplicationDesiredLanguage  ns.h:335
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <switch.h>

typedef struct {
    u64   tid;             // NsApplicationRecord::application_id (ns.h:92)
    char  name[0x200];     // NacpLanguageEntry::name (nacp.h:11)
    bool  locked;          // marcado para protecao (vem do config.json)
    bool  authorized;      // protegido, porem liberado ate travar de novo
    void *icon_tex;        // SDL_Texture*, carregado sob demanda pela UI
} Game;

// true se este registro e o proprio GameLock (ou seu forwarder): nome contendo
// "game" e "lock". Nunca entra na lista, nunca pode ser bloqueado.
bool games_is_self(const Game *game);

// Le todos os jogos instalados, menos o proprio GameLock/forwarder.
// *out_games vem de malloc; libere com games_unload.
Result games_load(Game **out_games, size_t *out_count);

void games_unload(Game *games);

// Le o icone (JPEG) de um titulo. *out_jpeg vem de malloc; libere com free.
Result games_read_icon(u64 tid, void **out_jpeg, size_t *out_size);
