// GameLock - Etapa 4: bloqueio efetivo da inicializacao de um jogo.
//
// Mecanismo: File Stubbing do loader do Atmosphere. Um arquivo vazio
//   sdmc:/atmosphere/contents/<TITLEID>/exefs/<nso>.stub
// faz o loader devolver fs::ResultPathNotFound() ao abrir aquele NSO, entao o
// processo do jogo nao chega a ser criado -- o codigo do jogo nao executa.
//
// Documentado em Atmosphere/docs/components/modules/loader.md, secao
// "File Stubbing", e implementado em libstratosphere/source/fs/fs_code.cpp:294-332
// (IsFileStubbed / DoOpenFile). A checagem do stub nao depende da combinacao de
// botoes de content redirection.
//
// A lista de NSOs e a mesma que o loader procura, na ordem de
// stratosphere/loader/source/ldr_process_creation.cpp:63-85.
//
// Posse: o GameLock remove stub apenas de titulo registrado em
// Config::blocked -- ou seja, stub que ele mesmo criou. Stub colocado a mao por
// outro motivo nunca e apagado por aqui.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <switch.h>

#include "config.h"

// Onde o Atmosphere procura conteudo por Title ID: os stubs dos jogos, e
// tambem o exefs.nsp do sysmodule de relock (ver relock.h).
#define BLOCK_CONTENTS_ROOT "sdmc:/atmosphere/contents"

// Cria os stubs do titulo. Idempotente.
bool block_apply(u64 tid);

// Politica: este titulo deve estar bloqueado agora? Vale para o .nro e para o
// sysmodule de relock, que precisam concordar sobre isso.
bool block_should_block(const Config *cfg, u64 tid);

// Remove apenas os arquivos *.stub da lista de NSOs, e tenta remover os
// diretorios se ficarem vazios. Nunca mexe em romfs, cheats ou exefs do usuario.
bool block_release(u64 tid);

// true se existe algum NSO solto no SD para este titulo (mod de LayeredFS).
// Nesse caso o arquivo solto tem precedencia sobre o stub e o bloqueio NAO pega.
bool block_has_loose_nso(u64 tid);

typedef struct {
    size_t applied;         // titulos que ficaram bloqueados
    size_t released;        // titulos que ficaram liberados
    size_t failures;        // operacoes de arquivo que falharam
    size_t conflicts;       // titulos com NSO solto no SD (stub sem efeito)
    u64    first_conflict;
    bool   config_changed;  // a lista de posse mudou: chamador deve salvar
} BlockSyncResult;

// Reconcilia o cartao com a configuracao:
//   bloqueado  <=>  protection_enabled && marcado && nao autorizado
// Age so sobre os titulos que a config conhece (marcados ou com posse de stub).
void block_sync(Config *cfg, BlockSyncResult *out);
