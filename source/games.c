#include "games.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define RECORDS_PER_CALL 64

// NsApplicationControlData tem 0x24000 bytes (NacpStruct 0x4000 + icon 0x20000,
// ns.h:72-75). Uma unica instancia reaproveitada em todas as chamadas: alocar
// isso por jogo estouraria a memoria do processo em modo applet.
static NsApplicationControlData *g_ctrl = NULL;

static Result ctrl_buffer_ensure(void)
{
    if (g_ctrl == NULL) {
        g_ctrl = malloc(sizeof(*g_ctrl));
        if (g_ctrl == NULL) {
            return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
        }
    }
    return 0;
}

static Result ctrl_read(u64 tid, u64 *out_size)
{
    return nsGetApplicationControlData(NsApplicationControlSource_Storage, tid,
                                       g_ctrl, sizeof(*g_ctrl), out_size);
}

// Preenche dst com o nome do jogo no idioma do console. Se o NACP nao trouxer
// nome legivel, cai para o Title ID -- nunca fica em branco.
static void fill_name(char *dst, size_t dst_size, u64 tid)
{
    NacpLanguageEntry *entry = NULL;

    if (R_SUCCEEDED(nsGetApplicationDesiredLanguage(&g_ctrl->nacp, &entry)) &&
        entry != NULL && entry->name[0] != '\0') {
        snprintf(dst, dst_size, "%s", entry->name);
        return;
    }

    snprintf(dst, dst_size, "%016lX", tid);
}

// Program ID do processo em que o GameLock esta rodando. Quando ele e aberto
// pelo forwarder do menu HOME, esse valor e justamente o Title ID do forwarder --
// que portanto nunca pode entrar na lista, ou o usuario se trancaria fora do
// proprio aplicativo. svc.h:204 (InfoType_ProgramId) e svc.h:11.
u64 games_self_program_id(void)
{
    u64 program_id = 0;
    if (R_FAILED(svcGetInfo(&program_id, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0))) {
        return 0;
    }
    return program_id;
}

// O proprio GameLock nunca pode ser bloqueado: o responsavel se trancaria fora
// do unico lugar onde se desbloqueia.
//
// O criterio e o NOME do titulo, nao o Program ID. Program ID nao serve aqui:
// aberto pelo hbmenu, o processo herda o ID do titulo que o hbloader substituiu
// (o Album, ou o jogo em que se segurou R), entao o forwarder do GameLock
// continuaria listado -- e, pior, o jogo hospedeiro desapareceria da lista.
//
// O criterio e frouxo de proposito: o nome precisa conter "game" E "lock", em
// qualquer ordem, caixa ou espacamento. Pega "GameLock", "Game Lock",
// "GAMELOCK", "GameLock Forwarder", "Lock de Games". Falso positivo aqui custa
// um jogo que nao pode ser bloqueado; falso negativo trancaria o responsavel
// fora do aplicativo.
bool games_is_self(const Game *game)
{
    char lower[0x200];
    size_t i = 0;
    for (; game->name[i] != '\0' && i + 1 < sizeof(lower); i++) {
        const unsigned char c = (unsigned char)game->name[i];
        lower[i] = (char)((c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c);
    }
    lower[i] = '\0';

    return strstr(lower, "game") != NULL && strstr(lower, "lock") != NULL;
}

Result games_load(Game **out_games, size_t *out_count)
{
    *out_games = NULL;
    *out_count = 0;

    Result rc = ctrl_buffer_ensure();
    if (R_FAILED(rc)) {
        return rc;
    }


    NsApplicationRecord records[RECORDS_PER_CALL];
    Game  *games = NULL;
    size_t count = 0;
    s32    offset = 0;

    for (;;) {
        s32 read = 0;
        rc = nsListApplicationRecord(records, RECORDS_PER_CALL, offset, &read);
        if (R_FAILED(rc)) {
            free(games);
            return rc;
        }
        if (read <= 0) {
            break;
        }

        Game *grown = realloc(games, (count + (size_t)read) * sizeof(*games));
        if (grown == NULL) {
            free(games);
            return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
        }
        games = grown;

        for (s32 i = 0; i < read; i++) {
            Game g;
            memset(&g, 0, sizeof(g));
            g.tid = records[i].application_id;

            // Segunda leitura do control data acontece depois, so para o icone
            // dos jogos visiveis (games_read_icon). Aqui queremos so o nome.
            u64 ctrl_size = 0;
            if (R_SUCCEEDED(ctrl_read(g.tid, &ctrl_size))) {
                fill_name(g.name, sizeof(g.name), g.tid);
            } else {
                snprintf(g.name, sizeof(g.name), "%016lX", g.tid);
            }

            if (games_is_self(&g)) {
                continue;
            }

            games[count++] = g;
        }

        offset += read;
    }

    *out_games = games;
    *out_count = count;
    return 0;
}

void games_unload(Game *games)
{
    free(games);
    free(g_ctrl);
    g_ctrl = NULL;
}

Result games_read_icon(u64 tid, void **out_jpeg, size_t *out_size)
{
    *out_jpeg = NULL;
    *out_size = 0;

    Result rc = ctrl_buffer_ensure();
    if (R_FAILED(rc)) {
        return rc;
    }

    u64 ctrl_size = 0;
    rc = ctrl_read(tid, &ctrl_size);
    if (R_FAILED(rc)) {
        return rc;
    }
    if (ctrl_size <= sizeof(NacpStruct)) {
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);  // sem icone
    }

    const size_t icon_size = (size_t)ctrl_size - sizeof(NacpStruct);
    void *jpeg = malloc(icon_size);
    if (jpeg == NULL) {
        return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    }
    memcpy(jpeg, g_ctrl->icon, icon_size);

    *out_jpeg = jpeg;
    *out_size = icon_size;
    return 0;
}
