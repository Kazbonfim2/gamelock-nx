# GameLock

**Controle parental por PIN para Nintendo Switch com Atmosphère.**

Homebrew `.nro` que deixa o dono do console escolher, jogo a jogo, quais títulos
só abrem com autorização. Jogo protegido simplesmente **não inicia** pelo menu
HOME — nem com o cartucho na gaveta, nem depois de reiniciar o console.

Desenvolvido por **@Kazbonfim2 | CamorraDevs**. Licença GPL-3.0.

---

## O problema

O controle parental nativo do Switch é tudo-ou-nada por faixa etária. Ele bloqueia
por classificação indicativa, não por título, e a senha mora na conta Nintendo —
o que, num console com Atmosphère, já não é lá grande barreira.

O que normalmente se quer é mais específico:

> *"Meu filho de 9 anos pode jogar Mario Kart e Minecraft à vontade.
> God of War e GTA, só comigo por perto."*

Isso o controle nativo não faz. Ou você trava a faixa etária inteira (e derruba
jogos inofensivos junto), ou não trava nada.

## O que o GameLock resolve

Você marca **quais** jogos ficam protegidos. O resto do console continua normal.

### Exemplo 1 — a biblioteca mista

O console é compartilhado entre um adulto e duas crianças. Na mesma lista
convivem Animal Crossing, Zelda, Doom Eternal e Mortal Kombat.

No GameLock você marca só os dois últimos com `A`, cadastra um PIN, e pronto.
Tocar no ícone do Doom no HOME devolve erro do firmware e o jogo não abre.
Zelda e Animal Crossing seguem abrindo normalmente, sem pedir nada.

### Exemplo 2 — liberar para a sessão de hoje

Fim de semana, o adulto quer jogar Mortal Kombat com o filho mais velho junto.

Abre o GameLock, aperta `Y` em cima do jogo, digita o PIN: o título fica
**LIBERADO**. Abre quantas vezes quiser. Na segunda de manhã, `Y` de novo (ou
*Travar de volta os liberados* no menu) e ele volta a ficar bloqueado. A
marcação nunca se perde — liberar não é desmarcar.

### Exemplo 3 — férias acabaram

Período de provas: o responsável vai em Configurações e desliga a chave geral
*Proteção dos jogos marcados*. Todos os marcados voltam a abrir, as marcações
continuam gravadas. Semana seguinte, religa — tudo volta a valer exatamente como
estava, sem remarcar jogo por jogo.

### Exemplo 4 — depois do reboot

Criança descobre que basta reiniciar o console. Não basta: o bloqueio é um
arquivo no cartão SD, lido pelo loader do Atmosphère a cada tentativa de abrir o
jogo. Sobrevive a reboot, a suspender/retomar e a desligar na tomada.

### O que ele **não** resolve

Quem tira o cartão SD e apaga os arquivos no PC desfaz tudo. Quem boota sem o
Atmosphère também. Isto é uma barreira contra acesso casual de criança — **não**
contra alguém tecnicamente capaz. Se a ameaça do seu caso é um adolescente que
sabe o que é um sigpatch, nenhum homebrew em `.nro` vai te atender.

---

## Como funciona por dentro

Proteger um jogo cria arquivos vazios no cartão:

```text
sdmc:/atmosphere/contents/<TITLEID>/exefs/main.stub
                                          rtld.stub  sdk.stub  subsdk0..9.stub
```

O loader do Atmosphère, ao ver `<nso>.stub`, devolve `fs::ResultPathNotFound()`
em vez de abrir aquele NSO — o processo do jogo **não chega a ser criado** e o
código do jogo não executa. Liberar o jogo apaga esses arquivos.

Documentado em `Atmosphere/docs/components/modules/loader.md` (seção *File
Stubbing*) e implementado em `libstratosphere/source/fs/fs_code.cpp:294-332`
(`IsFileStubbed` / `DoOpenFile`). A verificação do stub **não** depende da
combinação de botões de content redirection.

Sem sysmodule, sem IPC, sem MITM, sem alterar NAND, saves ou arquivos do sistema.
A comparação com os outros três mecanismos possíveis, e a justificativa da
escolha, está em [`docs/ANALISE-VIABILIDADE.md`](docs/ANALISE-VIABILIDADE.md) —
toda API citada lá tem arquivo e linha do código-fonte real.

**Posse.** O GameLock só apaga `.stub` de Title ID que ele mesmo registrou em
`blocked_titles`. Stub que você tenha colocado à mão por outro motivo nunca é
removido por aqui, e `romfs`, `cheats` ou um `exefs` modificado seu ficam
intactos (a remoção dos diretórios usa `rmdir`, que falha se houver algo dentro).

---

## Instalação

1. Console com **Atmosphère** já instalado e funcionando.
2. Copiar `GameLock.nro` para `sdmc:/switch/GameLock/GameLock.nro`.
3. Abrir pelo hbmenu (Álbum).

O PIN usa um teclado numérico próprio, então funciona tanto em modo applet
(hbmenu pelo Álbum) quanto em modo application (hbmenu com **R**).

### Ícone no menu HOME (forwarder)

Um `.nro` não aparece no HOME. Para ter ícone lá é preciso **instalar um título
forwarder** no console — um registro de aplicação, com Title ID próprio, cujo
código abre o `.nro`. Isso existe e funciona (o [sphaira](https://github.com/ITotalJustice/sphaira)
tem `forwarder_editor`, e o PlayGuard cria forwarders assim), mas tem duas
condições e um custo:

- **Precisa de signature patches** (sys-patch ou arquivos de sigpatch) no
  console. Sem eles o forwarder instalado não inicia.
- Criar o forwarder de dentro do GameLock significa gerar NCA + CNMT em runtime,
  pegar as chaves pelo `spl` e instalar por `ncm`/`ns` — um subsistema do tamanho
  de tudo que já existe aqui.

**Caminho recomendado:** criar o forwarder com o sphaira apontando para
`sdmc:/switch/GameLock/GameLock.nro`. Dois minutos, nada de código novo.

Aberto pelo forwarder, o GameLock roda em **modo application** (mais memória
disponível para os ícones).

**O GameLock se exclui da lista** (`games_is_self`, `games.c:60`): qualquer título
cujo nome contenha **"game" e "lock"** — em qualquer ordem, caixa ou espaçamento
(`GameLock`, `Game Lock`, `GAMELOCK`, `GameLock Forwarder`) — nunca aparece como
bloqueável. Program ID não serve de critério: aberto pelo hbmenu, o processo
herda o ID do título que o hbloader substituiu (o Álbum, ou o jogo em que se
segurou R), então o forwarder continuaria listado e, pior, o jogo hospedeiro
desapareceria da lista. Como segunda trava, `block_apply` recusa o Program ID do
processo atual.

---

## Como usar

| Tela | Tecla | Ação |
|---|---|---|
| Lista | `↑` `↓` / `ZL` `ZR` | navegar / paginar |
| Lista | `A` | proteger / desproteger (desproteger pede o PIN) |
| Lista | `Y` | liberar o jogo protegido (pede o PIN) / travar de novo |
| Lista | `B` | abrir o jogo selecionado, sem passar pelo HOME |
| Lista | `X` | Configurações |
| Lista | `+` | sair |

Estados mostrados em cada linha: `livre`, `BLOQUEADO`, `LIBERADO` (protegido mas
autorizado até travar de novo) e `marcado (proteção off)`.

Com PIN cadastrado, o GameLock pede o PIN **na abertura**: 3 tentativas e fecha.

**Abrir o jogo daqui (`B`).** Evita o ida-e-volta de liberar, sair e procurar o
ícone no HOME: libera com `Y`, aperta `B`, o jogo abre. Usa
`appletRequestLaunchApplication` (`applet.h:1455`), que vale em modo application
e, desde o FW 5.0.0, também em modo applet — os dois jeitos de abrir o GameLock.
Jogo ainda bloqueado recusa e avisa para liberar antes; os stubs são
reconciliados **antes** do pedido, porque o loader lê o cartão no momento do
launch. Aceito o pedido, o GameLock sai sozinho em vez de ficar pendurado atrás
do jogo.

### Configurações (`X`)

| Opção | O que faz |
|---|---|
| Cadastrar PIN / Trocar o PIN / Apagar o PIN | define, substitui (pede o atual) ou remove o PIN |
| Proteção dos jogos marcados: LIGADA/desligada | chave geral; desligada, todos voltam a abrir sem perder as marcações |
| Travar de volta os N jogos liberados | cancela as liberações feitas com `Y` |
| Conferir e corrigir o cartão | compara o cartão com a configuração e corrige o que estiver fora; não muda marcação |
| Regravar o arquivo de configuração | reescreve o `config.json`; útil só se a gravação automática falhou |
| Voltar para a lista | fecha as configurações |

Painel de estado no topo à direita: `PIN DEFINIDO`, `PROTEÇÃO ATIVADA`,
`MARCADOS: n`, `LIBERADOS: n`. Cada opção em foco traz uma frase explicando o que
faz — os rótulos sozinhos eram ambíguos entre si.

### Fluxo de autorização

O responsável digita o PIN e libera o jogo; ele fica liberado **até ser travado
de novo** (`Y` no jogo, ou *Travar de volta os liberados*). Não há expiração por
tempo — foi a opção aprovada. A alternativa seria um sysmodule de boot2 que
retrava a cada boot, descrita como Etapa 4b opcional na análise.

---

## Decisões de interface

**A tela inicial mostra só o necessário**: ícone, nome, estado e a legenda dos
botões. Erro aparece (falha silenciosa seria pior); sucesso se vê no próprio
estado da linha. Nada de PIN, proteção, contagens, Title ID, caminho de arquivo
ou código de erro — isso está em Configurações, atrás do PIN. Quem não deveria
aprender como a proteção funciona não aprende olhando a primeira tela.

**Glifos de botão, não letras.** As legendas de todas as telas usam os símbolos
reais do console (Ⓐ, Ⓑ, Ⓧ, Ⓨ, ZL, ZR, +), lidos da fonte compartilhada
*Nintendo Extended* (`PlSharedFontType_NintendoExt`, `pl.h:23`). Os codepoints
são os mapeados em [libtesla](https://github.com/WerWolv/libtesla)
(`tesla.hpp:186-192`): `U+E0A0` A, `U+E0A1` B, `U+E0A2` X, `U+E0A3` Y, `U+E0A6`
ZL, `U+E0A7` ZR, `U+E0B5` +. Essa fonte é aberta à parte (`open_icon_font`); a
fonte padrão não tem esses glifos.

**Nome comprido** (`draw_clipped`, `main.c:220`): na linha com o cursor, o nome
rola de ida e volta, com pausa nas pontas, dentro de um `SDL_RenderSetClipRect`;
nas outras linhas, é truncado com `...`. O corte começa por estimativa
proporcional e só recua, para não medir byte a byte a cada quadro, e respeita
fronteira de caractere UTF-8 (nome acentuado não quebra no meio).

**Tela de espera** (`draw_loading`): `games_load` pergunta nome e nacp ao `ns`
título a título, e a reconciliação mexe no cartão — com biblioteca grande são
alguns segundos de tela parada. Dois quadros cobrem isso. O que **não** dá para
cobrir é o trecho anterior: o hbloader lendo os 9 MB do `.nro` do cartão SD
acontece antes do nosso código existir, e aquela tela é do hbmenu. Reduzir o
`.nro` seria o único jeito de encurtar essa parte, e o peso é do SDL2 + TTF +
image ligados estaticamente (`--gc-sections` já vem no `switch.specs`, não há o
que ganhar ali).

**Teclado de PIN próprio** (`main.c:draw_keypad`), em grade 3×4: `1`–`9`, depois
`<` (apaga), `0` e `OK`. O que você digita aparece mascarado. Direcionais movem,
`A` aciona a tecla, `B` apaga um dígito, `+` cancela.

Por que não o teclado do sistema (`swkbd`): o layout das teclas do applet **não é
configurável** — só os textos, o rótulo do OK e as duas teclas opcionais de canto
(`swkbd.h:22`) —, e o applet podia não abrir com o homebrew em modo applet. Com
teclado próprio, a organização das teclas é nossa e o PIN funciona nos dois modos.

---

## config.json

`sdmc:/config/GameLock/config.json`, versão 2 (lê a versão 1 também). Gravado em
`.tmp` e renomeado, então falha no meio preserva o arquivo anterior.

```json
{
  "version": 2,
  "protection_enabled": true,
  "has_pin": true,
  "pin_iterations": 20000,
  "pin_salt": "7b1c…",
  "pin_hash": "c9a4…",
  "locked_titles":     [ "0100000000010000" ],
  "authorized_titles": [],
  "blocked_titles":    [ "0100000000010000" ]
}
```

- `locked_titles` — o que o responsável quer protegido.
- `authorized_titles` — protegidos, porém liberados agora.
- `blocked_titles` — onde o GameLock criou stub (registro de posse).

Regra aplicada: **stub existe ⟺ `protection_enabled` e marcado e não autorizado**.
A reconciliação roda na abertura e após cada mudança.

O PIN não é gravado: só salt (16 bytes de `randomGet`), iterações e o hash
PBKDF2-HMAC-SHA256 de 32 bytes sobre `hmacSha256CalculateMac`.

---

## Compilar

```sh
dkp-pacman -S switch-dev switch-sdl2 switch-sdl2_image switch-sdl2_ttf
cd gamelock && make        # saída: GameLock.nro
```

`DEVKITPRO` precisa estar no ambiente (abrir pelo `msys2_shell.cmd` do devkitPro
já resolve).

O ícone do `.nro` é o `icon.jpg` da raiz (256×256 JPEG, exigência do formato),
apontado por `ICON` no `Makefile`. O original em alta resolução fica em
`source/icon.png`.

### Arquivos

```text
gamelock/
├── Makefile          # template oficial do switch-examples + TARGET/APP_* e pkg-config do SDL2
├── icon.jpg          # ícone do .nro (256×256)
└── source/
    ├── main.c        # UI SDL2, navegação, menu, telas de PIN, orquestração
    ├── games.c/.h    # ns: lista de títulos, nome e ícone
    ├── pin.c/.h      # PBKDF2-HMAC-SHA256, salt, comparação em tempo constante, KATs
    ├── config.c/.h   # sdmc:/config/GameLock/config.json
    └── block.c/.h    # stubs de exefs: aplicar, liberar, reconciliar
```

---

## Testes

Autoteste do PBKDF2 roda **no console, a cada abertura**: KATs `c=1`, `c=2`,
`c=4096` (`P="password"`, `S="salt"`, `dkLen=32`), mais a sanidade da comparação
em tempo constante e da validação de dígitos. Falhando, o cadastro de PIN é
bloqueado e a tela avisa.

| Teste | Como | Status |
|---|---|---|
| Compila sem warning | `make` | **OK** — build limpo, 0 avisos, `GameLock.nro` com 8,98 MB, ícone embutido (2026-11-06) |
| Autoteste PBKDF2 | abrir e ler o rodapé | **OK** |
| **Jogo protegido não inicia pelo HOME** | proteger, sair, tocar no ícone | **OK** |
| Qual erro o HOME mostra | observar o diálogo e o código | **não executado** |
| Jogo não protegido continua normal | abrir outro jogo | **OK** |
| Liberar e abrir | `Y` + PIN, sair, abrir o jogo | **OK** |
| Travar de novo | `Y`, ou *Travar de volta os liberados*, e tentar abrir | **OK** |
| Vale sem reiniciar | proteger e tentar abrir sem reboot | **OK** |
| Sobrevive ao reboot | proteger, reiniciar, tentar abrir | **OK** |
| Suspender/retomar | suspender com o GameLock aberto | **não executado** |
| Jogo com mod de LayeredFS | proteger um jogo com `exefs` solto: deve avisar conflito | **não executado** |
| Jogo desinstalado e reinstalado | stub deve ter sido reconciliado | **não executado** |
| PIN correto/incorreto, alterar, remover | 3 erros fecham o app | **OK** |
| config.json ausente / corrompido | apagar e editar à mão | **não executado** |
| SD cheio ou protegido | marcar deve falhar e desfazer | **não executado** |
| Teclado de PIN próprio | digitar, apagar com `B`, cancelar com `+`, OK com menos de 4 dígitos | **OK** |
| Glifos de botão | as legendas devem mostrar os símbolos do console, não quadrados vazios | **OK** |
| Nome comprido | instalar/escolher um jogo de nome longo: rola na linha do cursor, trunca nas outras | **OK** |
| GameLock fora da lista | com o forwarder instalado, ele não deve aparecer | **não executado** |
| Ícone no hbmenu/forwarder | o cadeado deve aparecer, não o ícone padrão do libnx | **OK** |
| `B` abre o jogo (modo application) | pelo forwarder, liberar e apertar `B` | **OK** |
| `B` abre o jogo (modo applet) | pelo Álbum, liberar e apertar `B` | **OK** |
| `B` recusa jogo bloqueado | apertar `B` sem liberar: deve avisar, não lançar | **OK** |
| Tela de espera aparece | abrir com biblioteca grande | **OK** |

Validado em console pelo autor em 2026-11-06, 0.8.0 e 0.9.0: proteger, bloquear
de verdade no HOME, liberar com `Y`, travar de volta, sobreviver ao reboot, PIN,
teclado próprio, `B` abrindo o jogo nos dois modos e a tela de espera. Os `não
executado` que restam são casos de borda — SD cheio, `config.json` corrompido,
conflito com LayeredFS.

---

## Limitações conhecidas

- **O bloqueio vale no próximo launch.** Jogo já rodando, ou em suspensão, não é
  afetado até fechar.
- **O HOME mostra erro, não pedido de PIN.** O bloqueio é real, mas a criança vê
  um diálogo de falha do firmware, não um cadeado. Essa é a cara da solução.
- **Mod de LayeredFS com `exefs` solto no SD vence o stub.** O GameLock detecta e
  avisa na tela qual Title ID está nessa situação, mas não resolve sozinho.
- **Quem tem o cartão SD remove a proteção**: apagando o `config.json` ou os
  `.stub`, ou bootando sem o Atmosphère.
- Cartucho de jogo: o stub age por Title ID, então vale igual; não testado.
- A lista inclui tudo que o `ns` reporta como aplicação, inclusive itens que não
  são jogo.
- O leitor de `config.json` entende o formato que o próprio GameLock escreve, não
  JSON arbitrário (comentário `ponytail:` em `source/config.c`).

---

## Aviso

Software de terceiros para console modificado. Usar Atmosphère e homebrew é
responsabilidade de quem usa, e pode implicar banimento dos serviços online da
Nintendo. O GameLock não altera NAND, saves ou arquivos de sistema, mas não há
garantia de nenhum tipo — veja a GPL-3.0.
