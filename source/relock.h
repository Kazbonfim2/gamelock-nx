// GameLock - Etapa 4b (lado do .nro): instalar o sysmodule de relock pelo menu.
//
// O sysmodule que repoe os .stub depois do B nao pode ser carregado por este
// aplicativo: quem o carrega e o Atmosphere, no boot, varrendo
// atmosphere/contents/<TID>/flags/boot2.flag. Quando o GameLock abre, essa
// decisao foi tomada minutos antes.
//
// O que da para fazer, e e o que esta aqui: deixar os arquivos no lugar certo
// para o PROXIMO boot. O exefs.nsp vem embutido no romfs deste .nro
// (ROMFS no Makefile), entao instalar e copiar de romfs:/ para o cartao e criar
// a flag. Reiniciar o console continua sendo necessario -- nao ha como contornar.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <switch.h>

// Title ID do sysmodule. Tem de ser igual ao "title_id" de
// sysmodule/GameLockRelock.json: o loader usa o nome da pasta para achar o
// exefs.nsp e depois confere o program id declarado dentro do npdm.
#define RELOCK_TID "0100000000474C4B"

typedef enum {
    RELOCK_ABSENT = 0,  // nem o nsp nem a flag no cartao
    RELOCK_READY,       // os dois no lugar: carrega no proximo boot
    RELOCK_PARTIAL,     // so um dos dois -- instalacao interrompida ou mexida a mao
} RelockState;

RelockState relock_state(void);

// true se este .nro traz o sysmodule embutido (romfs montado e arquivo presente).
// Falso desabilita o item de menu: nao ha o que instalar.
bool relock_embedded(void);

// Heuristica de "o cartao tem a versao deste .nro": compara o tamanho do nsp.
// Tamanho igual com conteudo diferente passa batido, e e aceitavel -- isto
// serve para avisar quem atualizou o .nro e esqueceu o cartao, nao para
// garantir integridade.
// ponytail: comparar hash se um dia o nsp mudar sem mudar de tamanho.
bool relock_up_to_date(void);

// Copia o nsp do romfs para o cartao e cria o boot2.flag. Idempotente: rodar
// de novo atualiza o nsp no lugar. Grava em arquivo temporario e renomeia, para
// uma falha no meio nao deixar um nsp truncado que o boot tentaria carregar.
bool relock_install(void);

// Apaga a flag (primeiro: desarma o boot), depois o nsp, depois as pastas se
// ficarem vazias. Nao toca em mais nada dentro de atmosphere/contents.
bool relock_uninstall(void);
