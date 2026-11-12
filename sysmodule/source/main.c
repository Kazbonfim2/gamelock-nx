// GameLock Relock - Etapa 4b: sysmodule que repoe o .stub depois do launch.
//
// O problema que ele resolve: abrir um jogo protegido com B dentro do GameLock
// precisa apagar os .stub, senao o loader nao cria o processo. O .nro nao pode
// recria-los depois -- appletRequestLaunchApplication tira o foreground dele e
// o processo morre antes. Sem ninguem para refazer o stub, o jogo fica solto no
// menu HOME ate a proxima abertura do GameLock.
//
// Este sysmodule fecha essa janela em dois momentos:
//
//   1) no boot, repondo o stub de tudo que deveria estar bloqueado (cobre
//      desligar o console no meio da brincadeira);
//   2) quando uma aplicacao entra em foreground, repondo o stub DESSE titulo.
//
// (2) e seguro justamente porque chega atrasado: o loader le o cartao durante a
// criacao do processo, e pmdmnt so reporta um pid depois que o processo existe.
// Ou seja, quando reagimos os NSOs do jogo ja estao carregados -- o stub de
// volta nao interrompe a partida em curso, so vale para o proximo launch.
//
// APIs (libnx, nx/include/switch/services/pm.h):
//   pmdmntInitialize                pm.h:70
//   pminfoInitialize                pm.h:79
//   pmdmntGetApplicationProcessId   pm.h:120
//   pminfoGetProgramId              pm.h:125
//
// O program id vem de pm:info, nao de pmdmntGetProgramId (pm.h:123): o comando
// equivalente em pm:dmnt e mais novo, e aqui uma falha dele passaria calada --
// o relock por launch simplesmente nao aconteceria no console de alguem.
//
// Deliberadamente NAO usado: pmdmntHookToCreateApplicationProcess (pm.h:121).
// O hook segura a criacao do processo ate pmdmntStartProcess, o que permitiria
// bloquear de verdade na hora -- mas e um recurso de dono unico, disputado com o
// dmnt do Atmosphere (debugger), e um sysmodule que esqueca de liberar o hook
// trava o console em qualquer jogo. Poll de um segundo resolve sem disputa.
//
// Posse: este sysmodule nunca cria stub novo. Ele so reaplica em titulo que ja
// esta em Config::blocked -- stub que o .nro criou e que o .nro sabe remover.
// Com isso o sysmodule nunca escreve no config.json: zero corrida com a UI, e
// nenhum stub orfao se o responsavel desmarcar o jogo depois.

#include <switch.h>

#include "block.h"
#include "config.h"

#define INNER_HEAP_SIZE 0x80000      // config.json cabe em 256 KB (CONFIG_MAX_LEN)
#define POLL_INTERVAL   1000000000ULL  // 1 s em ns

#ifdef __cplusplus
extern "C" {
#endif

u32 __nx_applet_type = AppletType_None;
u32 __nx_fs_num_sessions = 1;

void __libnx_initheap(void)
{
    static u8 inner_heap[INNER_HEAP_SIZE];
    extern void *fake_heap_start;
    extern void *fake_heap_end;

    fake_heap_start = inner_heap;
    fake_heap_end   = inner_heap + sizeof(inner_heap);
}

void __appInit(void)
{
    Result rc = smInitialize();
    if (R_FAILED(rc)) {
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));
    }

    // hosversion decide o command id de varias chamadas de pm; sem isso as
    // chamadas de pmdmnt usariam o formato errado em firmwares antigos.
    rc = setsysInitialize();
    if (R_SUCCEEDED(rc)) {
        SetSysFirmwareVersion fw;
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw))) {
            hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
        }
        setsysExit();
    }

    rc = fsInitialize();
    if (R_FAILED(rc)) {
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_FS));
    }

    // O proprio exefs.nsp deste sysmodule vem do cartao, entao em boot2 o SD ja
    // esta montavel -- nao ha por que esperar por ele aqui.
    fsdevMountSdmc();

    rc = pmdmntInitialize();
    if (R_FAILED(rc)) {
        diagAbortWithResult(rc);
    }

    rc = pminfoInitialize();
    if (R_FAILED(rc)) {
        diagAbortWithResult(rc);
    }

    smExit();
}

void __appExit(void)
{
    pminfoExit();
    pmdmntExit();
    fsdevUnmountAll();
    fsExit();
}

#ifdef __cplusplus
}
#endif

// Reaplica o stub do que deve estar bloqueado e cuja posse e do GameLock.
// only_tid == 0 age em todos os titulos marcados.
//
// config.json ausente ou ilegivel: nada a fazer. Nao da para distinguir isso de
// uma leitura pega no meio do rename do config_save, mas a UI so grava com ela
// mesma em foreground -- nunca com um jogo subindo -- entao na pratica a unica
// causa e config ausente ou corrompido, e nesse caso quem reconcilia e o .nro.
static void relock(u64 only_tid)
{
    Config cfg;
    bool missing = false;
    bool invalid = false;

    config_load(&cfg, &missing, &invalid);
    if (missing || invalid) {
        config_free(&cfg);
        return;
    }

    for (size_t i = 0; i < cfg.locked.count; i++) {
        const u64 tid = cfg.locked.ids[i];

        if (only_tid != 0 && tid != only_tid) {
            continue;
        }
        if (!block_should_block(&cfg, tid) || !config_is_blocked(&cfg, tid)) {
            continue;
        }

        block_apply(tid);  // idempotente; falha de SD so adia para o proximo launch
    }

    config_free(&cfg);
}

int main(void)
{
    relock(0);

    u64 handled_pid = 0;

    for (;;) {
        u64 pid = 0;

        if (R_SUCCEEDED(pmdmntGetApplicationProcessId(&pid)) && pid != 0) {
            // Mesmo pid: jogo continua aberto (ou suspenso) e ja foi tratado.
            if (pid != handled_pid) {
                handled_pid = pid;

                u64 tid = 0;
                if (R_SUCCEEDED(pminfoGetProgramId(&tid, pid))) {
                    relock(tid);
                }
            }
        } else {
            handled_pid = 0;  // nenhuma aplicacao: o proximo launch e novidade
        }

        svcSleepThread(POLL_INTERVAL);
    }

    return 0;
}
