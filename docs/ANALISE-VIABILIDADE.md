# GameLock — Análise de viabilidade técnica (RULES §6 e §9)

Pesquisa feita em 2026-11-02 sobre o código-fonte real de **libnx** (`master`) e
**Atmosphère** (`master`), baixados e lidos diretamente. Toda API citada abaixo
tem arquivo e linha. Nada aqui foi testado em console — veja §8.

Legenda usada em todo o documento:

- **[DOC]** documentado oficialmente (docs do Atmosphère / switchbrew)
- **[SRC]** verificado no código-fonte (arquivo:linha)
- **[HIP]** hipótese técnica ainda não validada
- **[TEST]** exige teste físico no Nintendo Switch

---

## 1. Como obter a lista de jogos instalados

**[SRC]** `libnx/nx/include/switch/services/ns.h`

```c
Result nsInitialize(void);                                                  // ns.h:244
Result nsListApplicationRecord(NsApplicationRecord* records, s32 count,
                               s32 entry_offset, s32* out_entrycount);      // ns.h:415
```

`nsListApplicationRecord` é paginado: chama-se em laço incrementando
`entry_offset` até `out_entrycount` vir 0.

## 2. Como identificar cada jogo pelo Title ID

**[SRC]** `ns.h:91-99`

```c
typedef struct {
    u64 application_id;   // Title ID (ApplicationId)
    u8  last_event;
    u8  attributes;
    u8  reserved[6];
    u64 last_updated;
} NsApplicationRecord;
```

`application_id` é o Title ID. É esse valor (formatado como `%016lX`) que
identifica o jogo na config e no mecanismo de bloqueio da §5.

## 3. Como exibir nomes e ícones

**[SRC]**

```c
Result nsGetApplicationControlData(NsApplicationControlSource source, u64 application_id,
                                   NsApplicationControlData* buffer, size_t size,
                                   u64* actual_size);                        // ns.h:313
Result nsGetApplicationDesiredLanguage(NacpStruct *nacp,
                                       NacpLanguageEntry **langentry);       // ns.h:335

typedef struct {
    NacpStruct nacp;      // metadados, inclui os nomes por idioma
    u8 icon[0x20000];     // ícone em JPEG
} NsApplicationControlData;                                                  // ns.h:72-75
```

Fluxo: `nsGetApplicationControlData(NsApplicationControlSource_Storage, tid, …)`
→ `nsGetApplicationDesiredLanguage(&data.nacp, &entry)` → `entry->name`.

**Consequência de projeto:** o ícone vem em **JPEG**, e o buffer é de 128 KB por
jogo (não cabe carregar tudo de uma vez — ler sob demanda conforme a rolagem).
O console de texto do libnx (`consoleInit`) **não desenha imagem**. Para cumprir
o requisito "exibir o ícone" da §3 do RULES é preciso um renderizador:

| Caminho | Custo | Observação |
|---|---|---|
| SDL2 + SDL2_image + SDL2_ttf | baixo | pacotes prontos do devkitPro (`switch-sdl2*`); JPEG já resolvido pelo SDL2_image; fonte do sistema via `pl` (`plGetSharedFontByType`) |
| deko3d / framebuffer puro | alto | escrever decodificador e renderizador próprios — contraria a §8 do RULES ("não crie frameworks próprios") |

**Recomendação:** SDL2 + SDL2_image + SDL2_ttf na Etapa 2. **Pergunta aberta**
antes de começar a Etapa 2 (ver §9).

## 4. Como armazenar os jogos protegidos e o PIN

Tudo no SD, como manda a §4 do RULES: `sdmc:/config/GameLock/config.json`.

PIN com PBKDF2-HMAC-SHA256 é viável **sem dependência externa**:

**[SRC]** `libnx/nx/include/switch/crypto/hmac.h:41`

```c
void hmacSha256CalculateMac(void *dst, const void *key, size_t key_size,
                            const void *src, size_t size);
```

PBKDF2 em cima disso são ~20 linhas (é só o laço de iterações do RFC 2898).

**[SRC]** salt aleatório: `services/csrng.h:20` `csrngGetRandomBytes()`
(ou `kernel/random.h:15` `randomGet()`).

Comparação dos hashes em tempo constante: laço de XOR acumulado, ~5 linhas.

## 5. Como impedir que um jogo protegido seja iniciado pelo HOME

Esta é a §6 do RULES — o requisito mais importante. Investiguei quatro
mecanismos. Só um se sustenta.

### 5.1 Opção A — `pm:dmnt`, hook de criação de processo

**[SRC]** `libnx/nx/include/switch/services/pm.h`

```c
Result pmdmntHookToCreateProcess(Event* out, u64 program_id);
Result pmdmntHookToCreateApplicationProcess(Event* out);
Result pmdmntStartProcess(u64 pid);
Result pmdmntGetApplicationProcessId(u64* pid_out);
```

**[SRC]** `Atmosphere/stratosphere/pm/source/impl/pm_process_manager.cpp:215-223`

```cpp
/* Process hooks/signaling. */
if (fixed_location.program_id == g_program_id_hook) {
    os::SignalSystemEvent(std::addressof(g_hook_to_create_process_event));
    g_program_id_hook = ncm::InvalidProgramId;
} else if (is_application && g_application_hook) {
    os::SignalSystemEvent(std::addressof(g_hook_to_create_application_process_event));
    g_application_hook = false;
} else if (!ShouldStartSuspended(flags)) {
    R_TRY(StartProcess(process_info, std::addressof(program_info)));
}
```

Isto é **interceptação preventiva real**: quando o hook casa, o PM cria o
processo e **não** chama `StartProcess` — o código do jogo não executa até
alguém chamar `pm:dmnt StartProcess(pid)`.

**Por que não serve, ainda assim:**

- O hook de aplicação é **ocupado permanentemente pelo próprio Atmosphère**.
  **[SRC]** `stratosphere/dmnt/source/cheat/impl/dmnt_cheat_api.cpp:679-688` — a
  thread `DetectLaunchThread` fica em laço infinito re-armando
  `HookToCreateApplicationProcess()`; e ela é criada sem condição alguma
  (`dmnt_cheat_api.cpp:271`, chamada por `InitializeCheatManager()` em
  `dmnt_main.cpp:156`).
- O PM só admite **um** dono: `g_application_hook.compare_exchange_strong(...)`
  devolve `pm::ResultDebugHookInUse()` (`pm_process_manager.cpp:457-465`). Pior:
  o dmnt chama com `R_ABORT_UNLESS` (`dmnt_cheat_api.cpp:248`), ou seja, disputar
  o hook **derruba o dmnt**.
- O hook por `program_id` (`g_program_id_hook`) está livre e tem precedência no
  `if`, mas é **um único Title ID por vez e one-shot** (é limpo ao disparar).
  Não cobre N jogos protegidos.

**Veredito: inviável** com Atmosphère de fábrica, sem modificar o dmnt.

### 5.2 Opção B — `dmnt:cht` (evento de processo de cheat)

Descartada. **[SRC]** `dmnt_cheat_api.cpp:874-882`: o dmnt chama
`StartProcess(pid)` **antes** de sinalizar `m_cheat_process_event`, e quando o
título não tem cheat habilitado o `SCOPE_GUARD` (linhas 799-804) inicia o
processo e nem sinaliza. Não existe janela antes da execução.

**Veredito: inviável** (é notificação pós-início, não interceptação).

### 5.3 Opção C — MITM de `pctl` (controle parental nativo)

**[DOC]** switchbrew, *Parental Control services*: `IParentalControlService`
comando **1002 `ConfirmLaunchApplicationPermission`**.

**[HIP]** o HOME menu (qlaunch) chamaria 1002 antes de iniciar um título; um
sysmodule que fizesse MITM de `pctl:a` poderia devolver erro só para os Title IDs
protegidos, e o firmware exibiria o diálogo nativo de restrição.

Evidência parcial de que o caminho do `pctl` funciona na prática: o projeto
**PlayGuard** (JigSawFr/PlayGuard, GPLv3, homebrew real para Atmosphère)
configura o controle parental nativo por `pctl` — inclusive comandos `*ForDebug` —
e consegue "bloquear todos os jogos". Mapa de comandos confirmado no código dele
(`source/core/pctl_ops.h`): 1031 `IsRestrictionEnabled`, 1033 `SetSafetyLevel`,
1036 custom settings, 1201 `UnlockRestrictionTemporarily`, 1451/1452 play timer.

**Mas:** o controle parental nativo restringe por **faixa de idade** e por
**tempo de jogo**, não por título escolhido a dedo. Bloqueio individual exigiria
o MITM do comando 1002, que é **[HIP]** + **[TEST]** e depende do framework de
MITM da libstratosphere (`mitm.lst`, **[SRC]**
`libstratosphere/source/boot2/boot2_api.board.nintendo_nx.cpp:264`).

**Veredito: possível, não comprovado.** Custo alto, e o PIN passaria a ser o do
controle parental da Nintendo, não o nosso.

### 5.4 Opção D — stub de exefs do loader do Atmosphère ← **recomendada**

**[DOC]** `Atmosphere/docs/components/modules/loader.md`, seção *File Stubbing*:

> In order to prevent an NSO from being loaded even if it exists in the exefs,
> loader will also check if a stub file exists. If such a file exists, the NSO
> will not be loaded. The files should be named like `rtld.stub`, `main.stub`,
> etc. and may be empty.

E, na seção *Technical Semantics*: *"Otherwise, if a stub file exists, a
'Not Found' error is returned."*

**[SRC]** `libstratosphere/source/fs/fs_code.cpp:294-332`

```cpp
bool IsFileStubbed(const fs::Path &path) {
    if (!m_sd_content_fs) { return false; }
    fs::Path stub_path;
    stub_path.InitializeWithFormat("%s.stub", path.GetString());
    …
}
…
virtual Result DoOpenFile(...) override final {
    if (m_is_redirect) { R_SUCCEED_IF(R_SUCCEEDED(m_sd_content_fs->OpenFile(...))); }
    /* Otherwise, check if the file is stubbed. */
    R_UNLESS(!this->IsFileStubbed(path), fs::ResultPathNotFound());
    R_RETURN(m_code_fs.OpenFile(out_file, path, mode));
}
```

Dois detalhes que importam, ambos **[SRC]**:

1. O diretório lido é `/atmosphere/contents/<program id>/exefs`
   (`fs_code.cpp:286`) — a mesma convenção do resto do Atmosphère.
2. A checagem do stub **não** depende da combinação de botões de content
   redirection (`m_is_redirect` só decide se arquivo solto do SD tem
   preferência). O stub vale sempre que o SD estiver montado.

**Como o GameLock usa isso**

Proteger um jogo = criar arquivos vazios:

```text
sdmc:/atmosphere/contents/<TITLEID16>/exefs/main.stub
sdmc:/atmosphere/contents/<TITLEID16>/exefs/rtld.stub
sdmc:/atmosphere/contents/<TITLEID16>/exefs/sdk.stub
sdmc:/atmosphere/contents/<TITLEID16>/exefs/subsdk0.stub … subsdk9.stub
```

Desproteger/autorizar = apagar esses arquivos.

Por que isto satisfaz a §6 do RULES, nas distinções que ela exige:

| Distinção exigida pela §6 | Opção D |
|---|---|
| Identificação da tentativa de inicialização | o loader identifica, ao abrir o código do título |
| Notificação de processo iniciado | não se aplica — nada é iniciado |
| **Interceptação preventiva da solicitação** | sim: `fs::ResultPathNotFound()` na leitura do NSO |
| **Bloqueio efetivo** | sim: sem NSO não há processo; o código do jogo não executa |
| Autorização temporária após o PIN | apagar os `.stub` (e recriar ao re-travar) |

Vantagens sobre A, B e C: **zero sysmodule**, zero IPC, zero disputa com o dmnt
ou com cheats, zero modificação de NAND/saves/sistema, efeito por título, e o
"código" do mecanismo são duas chamadas (`fopen` e `remove`).

**Limites honestos desta opção** (nenhum deles simulado como resolvido):

- **[TEST]** a mensagem que o HOME menu mostra ao falhar o launch é um diálogo de
  erro do firmware (tipo "o software foi encerrado porque ocorreu um erro", com
  código `2168-xxxx`), **não** um pedido de PIN. O bloqueio é real; a experiência
  é de erro, não de cadeado.
- **[TEST]** se stubar só o `main` basta, ou se o loader precisa também dos stubs
  de `rtld`/`sdk`/`subsdk*` para falhar de forma limpa. Stubar todos torna a
  falha determinística — é o que o GameLock vai fazer.
- **[TEST]** se a mudança vale na hora (esperado: sim, o loader lê o SD a cada
  criação de processo) ou se exige reinício do console.
- Re-travar depende de alguém recriar os `.stub`. Sem sysmodule, isso é um ato
  explícito do responsável dentro do GameLock. Um sysmodule mínimo de boot2 que
  recrie os stubs no boot (**[DOC]** flag
  `/atmosphere/contents/<pid>/flags/boot2.flag`, **[SRC]**
  `cfg_flags.board.nintendo_nx.inc:84`) seria uma Etapa 4b **opcional**, não
  necessária para o bloqueio funcionar.
- Jogo em cartucho ou jogo já rodando: o stub só age no próximo launch.

## 6. Arquitetura mínima necessária

```text
GameLock.nro  (homebrew, roda sob o hbmenu)
 ├── UI (lista de jogos, seleção, telas de PIN)
 ├── ns  → nsListApplicationRecord / nsGetApplicationControlData
 ├── PIN → PBKDF2-HMAC-SHA256 (hmacSha256CalculateMac) + salt (csrng)
 ├── config → sdmc:/config/GameLock/config.json
 └── bloqueio → cria/apaga sdmc:/atmosphere/contents/<TID>/exefs/*.stub
```

Um processo, um binário, nada em background. O bloqueio não precisa de sysmodule —
e é por isso que esta é a arquitetura mais simples e confiável que encontrei que
ainda cumpre o bloqueio real. A §6 do RULES pede justificativa técnica para mexer
no loader: aqui **não** se modifica o loader, usa-se uma funcionalidade
documentada dele.

A Etapa 4b (`gamelock/sysmodule/`, implementada em 2026-11-12) acrescenta um
sysmodule **opcional** que só refaz stub já existente. Ele não participa do
bloqueio: sem ele tudo acima continua valendo, com a janela do `B` descrita na
§5.4.

## 7. Serviços e interfaces investigados

| Serviço / interface | Para quê | Situação |
|---|---|---|
| `ns:am2` (`nsInitialize`) | listar jogos, nome, ícone | **[SRC]** usado |
| `pm:dmnt` | hook de criação de processo | **[SRC]** inviável para bloquear (§5.1) |
| `pm:dmnt` | qual aplicação está em foreground | usado no relock da Etapa 4b (`GetApplicationProcessId` + `GetProgramId`) |
| `pm:shell` | `TerminateProcess` / `TerminateProgram` | só encerra depois de iniciar — descartado |
| `dmnt:cht` | evento de processo | **[SRC]** inviável (§5.2) |
| `pctl` / `pctl:a` | controle parental nativo | **[HIP]** §5.3, não usado |
| `ldr` / stub de exefs | bloqueio efetivo | **[DOC]+[SRC]** escolhido (§5.4) |
| `csrng`, `crypto/hmac.h` | salt e PBKDF2 | **[SRC]** usado |

## 8. Limitações técnicas e de compatibilidade

- **Nada foi compilado nem testado.** Não há devkitPro/devkitA64 nesta máquina
  (`DEVKITPRO` vazio, sem `make`), e não há console no circuito. Todo item
  **[TEST]** continua aberto.
- Segurança: a config e os stubs vivem no SD. Quem tiver acesso físico ao cartão
  (ou rodar outro homebrew, ou bootar sem o Atmosphère) remove a proteção em
  segundos. O hash do PIN protege o PIN, não o bloqueio. Isto atende ao que a §5
  do RULES pede — barrar o acesso casual de criança — e **não** é proteção contra
  usuário técnico; não será apresentado como se fosse.
- Compatibilidade: `nsListApplicationRecord` e `nsGetApplicationControlData` são
  estáveis desde 1.0.0 (ver as anotações de versão em `ns.h`). O stub de exefs é
  recurso do loader do Atmosphère, documentado no `master`; depende do
  Atmosphère, não da versão de firmware. O Switch Lite não muda nada aqui — só
  obriga a UI a funcionar em modo portátil.
- Terceiros: nenhum código de terceiros foi copiado, exceto o template de
  Makefile. Fontes consultadas: libnx (ISC), Atmosphère (GPLv2), switch-examples
  (template do Makefile), PlayGuard (GPLv3, apenas lido como evidência do
  caminho `pctl`).

## 9. Decisões tomadas (perguntas desta análise, já respondidas)

1. **UI:** SDL2 + SDL2_image + SDL2_ttf, com a fonte compartilhada do sistema
   (`plGetSharedFontByType`). Aprovado em 2026-11-02.
2. **Autorização:** liberar até travar de novo, sem sysmodule. Aprovado. A Etapa
   4b saiu do "em aberto" em 2026-11-12 e virou `gamelock/sysmodule/`: continua
   opcional, mas o gatilho não é só o boot — é boot **e** aplicação entrando em
   foreground, que é o que fecha a janela do `B` (§5.4). Reage depois que o
   processo do jogo já existe, de propósito: assim o relock nunca corre com o
   loader lendo o cartão. O hook de `pm:dmnt` da §5.1 segue descartado, agora
   também por disputa com o `dmnt` do Atmosphère.

## 10. Estado da implementação (2026-11-06)

A Opção D da §5.4 está implementada em `gamelock/source/block.c`: stubs de
`rtld`, `main`, `subsdk0`–`subsdk9` e `sdk` em
`sdmc:/atmosphere/contents/<TID>/exefs/`.

Duas decisões que não estavam nesta análise e foram tomadas na implementação:

- **Registro de posse.** O `config.json` guarda `blocked_titles`, a lista dos
  Title IDs em que o GameLock criou stub. A remoção só age sobre essa lista, para
  nunca apagar um `.stub` colocado à mão pelo usuário por outro motivo. A remoção
  dos diretórios usa `rmdir`, que falha se houver `romfs`, `cheats` ou `exefs`
  modificado dentro — então nada do usuário é perdido.
- **Detecção de conflito.** Se existir um NSO solto (`exefs/main` etc.) no SD para
  um título protegido, o arquivo solto tem precedência sobre o stub e o bloqueio
  não pega (`fs_code.cpp:325-331`). O GameLock verifica isso e avisa na tela qual
  Title ID está nessa situação, em vez de apresentar uma proteção que não existe.

Os itens **[TEST]** da §5.4 e da §8 continuam todos abertos: nada foi compilado
nem testado — sem devkitPro nesta máquina e sem console no circuito.
