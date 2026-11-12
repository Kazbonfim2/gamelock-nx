# GameLock

**Controle parental por PIN para Nintendo Switch com Atmosphère.**
*Parental control by PIN for Nintendo Switch with Atmosphère — interface in
Portuguese and English, switchable in Settings.*

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

### Exemplo 2 — o adulto quer jogar agora

Fim de semana, o adulto quer jogar Mortal Kombat. Abre o GameLock, digita o PIN
da abertura, seleciona o jogo e aperta `B`: o jogo abre, mesmo bloqueado. Nada
fica marcado como liberado, então não há nada para lembrar de desfazer depois —
o bloqueio é reposto sozinho.

Abrir o jogo exige apagar os `.stub`, e o `.nro` morre junto com o pedido de
launch, sem poder recriá-los. Com o **sysmodule de relock** instalado (opcional,
[abaixo](#relock-imediato-sysmodule-opcional)) o stub volta cerca de um segundo
depois, com o jogo já rodando. Sem ele, o jogo fica solto no HOME até a próxima
abertura do GameLock.

### Exemplo 3 — liberar para a criança jogar sozinha

Caso diferente: o filho mais velho vai jogar sem o adulto por perto.

`Y` em cima do jogo e ele fica **LIBERADO** — abre pelo HOME, quantas vezes
quiser, até ser travado de novo (`Y` outra vez, ou *Travar de volta os
liberados* no menu). A marcação nunca se perde: liberar não é desmarcar.

Resumindo os dois botões: **`B` é "eu jogo agora"**, não deixa rastro.
**`Y` é "liberado para o HOME"**, e fica até você travar.

### Exemplo 4 — férias acabaram

Período de provas: o responsável vai em Configurações e desliga a chave geral
*Proteção dos jogos marcados*. Todos os marcados voltam a abrir, as marcações
continuam gravadas. Semana seguinte, religa — tudo volta a valer exatamente como
estava, sem remarcar jogo por jogo.

### Exemplo 5 — depois do reboot

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

O bloqueio em si é só isso: sem IPC, sem MITM, sem alterar NAND, saves ou
arquivos do sistema. A comparação com os outros três mecanismos possíveis, e a
justificativa da escolha, está em
[`docs/ANALISE-VIABILIDADE.md`](docs/ANALISE-VIABILIDADE.md) — toda API citada lá
tem arquivo e linha do código-fonte real.

O sysmodule de relock é um acessório **opcional** dessa arquitetura, não parte do
bloqueio: ele só reescreve stub que o `.nro` já tinha criado. Nada do que está
acima depende dele.

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

### Relock imediato (sysmodule, opcional)

Fecha a janela do `B`: depois de abrir um jogo bloqueado por aqui, o stub volta
sozinho, sem ninguém precisar reabrir o GameLock.

**Pelo menu (recomendado).** O sysmodule vem embutido no `.nro`. Em Configurações
(`X`), no item *Relock imediato*: `A` instala ou reinstala, `Y` remove. Depois
**reinicie o console** — isso não tem como contornar, quem carrega o sysmodule é
o Atmosphère no boot, e quando o GameLock abre essa decisão já foi tomada.

O item mostra o estado: `não instalado`, `INSTALADO`, `INSTALADO (versão
diferente desta)` — esse último é o aviso de que você atualizou o `.nro` e o
cartão ficou com o sysmodule antigo; `A` resolve. Se o `.nro` tiver sido montado
sem romfs, o item não aparece.

**À mão**, se preferir mexer no cartão pelo PC:

```text
sdmc:/atmosphere/contents/0100000000474C4B/exefs.nsp    <- GameLockRelock.nsp
sdmc:/atmosphere/contents/0100000000474C4B/flags/boot2.flag   <- arquivo vazio
```

Reiniciar o console. O `boot2.flag` é o que faz o Atmosphère carregar o
sysmodule no boot (`cfg_flags.board.nintendo_nx.inc:84`). O nome da pasta tem que
ser igual ao `title_id` do `sysmodule/GameLockRelock.json`: o loader usa a pasta
para achar o `exefs.nsp` e depois confere o program id declarado no npdm.

**O que ele faz.** Dois gatilhos, e nada além disso:

- **no boot**, repõe o stub de tudo que deveria estar bloqueado — cobre desligar
  o console no meio da brincadeira;
- **quando uma aplicação entra em foreground**, repõe o stub *desse* título.

O segundo gatilho é seguro porque chega atrasado de propósito: o loader lê o
cartão durante a criação do processo, e `pm:dmnt` só reporta um pid depois que o
processo existe. Quando o stub volta, os NSOs do jogo já estão carregados — a
partida em curso não é afetada, o stub vale para o próximo launch. O custo é um
poll de um segundo no core 3.

**O que ele não faz.** Não cria stub novo, não escreve no `config.json`, não
bloqueia launch nenhum. Só reaplica em título que já está em `blocked_titles`,
isto é, stub que o `.nro` criou e que o `.nro` sabe remover — então desmarcar um
jogo nunca deixa stub órfão, e não há corrida de escrita com a interface.

Também **não** usa `pmdmntHookToCreateApplicationProcess`, que seguraria a
criação do processo e permitiria bloquear na hora: é recurso de dono único,
disputado com o `dmnt` do Atmosphère, e um sysmodule que esqueça de liberar o
hook trava o console em qualquer jogo.

Permissões no `sysmodule/GameLockRelock.json`: `fsp-srv` (cartão), `pm:dmnt` +
`pm:info` (qual aplicação está em foreground e qual o Title ID dela) e `set:sys`
(versão de firmware, que decide o command id das chamadas de `pm`). Mais nada.

Desinstalar é o `Y` no item do menu, ou apagar a pasta `0100000000474C4B` à mão,
e reiniciar — o GameLock volta a funcionar como antes, com a janela do `B` de
volta. Enquanto não reiniciar, o sysmodule já carregado em memória continua
repondo stub; a remoção só vale do próximo boot em diante.

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
| Lista | `A` | proteger / desproteger |
| Lista | `Y` | liberar o jogo protegido / travar de novo |
| Lista | `B` | abrir o jogo selecionado, bloqueado ou não, sem passar pelo HOME |
| Lista | `X` | Configurações |
| Lista | `+` | sair |

Estados mostrados em cada linha: `livre`, `BLOQUEADO`, `LIBERADO` (protegido mas
autorizado até travar de novo) e `marcado (proteção off)`.

Com PIN cadastrado, o GameLock pede o PIN **na abertura**: 3 tentativas e fecha.
Esse é o portão — ver *Um portão de PIN só* abaixo.

**Abrir o jogo daqui (`B`).** Evita o ida-e-volta de sair do GameLock e procurar
o ícone no HOME. Usa `appletRequestLaunchApplication` (`applet.h:1455`), que vale
em modo application e, desde o FW 5.0.0, também em modo applet — os dois jeitos
de abrir o GameLock. Os stubs são reconciliados **antes** do pedido, porque o
loader lê o cartão no momento do launch. Aceito o pedido, o GameLock sai sozinho
em vez de ficar pendurado atrás do jogo.

**Jogo bloqueado também abre com `B`** (desde a 1.0.1). Antes era preciso liberar
com `Y` primeiro, e isso criava o hábito ruim: liberar, jogar, fechar e esquecer
o jogo liberado para a criança. Agora quem passou do portão de PIN abre direto,
e **nada de autorização é gravado** — o `B` não deixa rastro para alguém lembrar
de desfazer.

Por dentro: o stub do título sai só para este launch e a posse em
`blocked_titles` fica como está, então a próxima abertura do GameLock reconcilia
e o jogo volta a não subir pelo HOME. O preço está nas *Limitações conhecidas*:
entre esse `B` e a próxima abertura do GameLock, o título fica sem stub.

### Configurações (`X`)

| Opção | O que faz |
|---|---|
| Cadastrar PIN / Trocar o PIN / Apagar o PIN | define, substitui (pede o atual) ou remove o PIN |
| Proteção dos jogos marcados: LIGADA/desligada | chave geral; desligada, todos voltam a abrir sem perder as marcações |
| Travar de volta os N jogos liberados | cancela as liberações feitas com `Y` |
| Idioma / Language: PORTUGUES/ENGLISH | troca o idioma dos textos; grava no `config.json` |
| Conferir e corrigir o cartão | compara o cartão com a configuração e corrige o que estiver fora; não muda marcação |
| Regravar o arquivo de configuração | reescreve o `config.json`; útil só se a gravação automática falhou |
| Voltar para a lista | fecha as configurações |

Painel de estado no topo à direita: `PIN DEFINIDO`, `PROTEÇÃO ATIVADA`,
`MARCADOS: n`, `LIBERADOS: n`. Cada opção em foco traz uma frase explicando o que
faz — os rótulos sozinhos eram ambíguos entre si.

### Fluxo de autorização

Liberar com `Y` serve para **o jogo abrir pelo HOME** — é o caso de deixar a
criança jogar sozinha. Fica liberado **até ser travado de novo** (`Y` no jogo,
ou *Travar de volta os liberados*). Não há expiração por tempo — foi a opção
aprovada. A alternativa seria um sysmodule de boot2 que retrava a cada boot,
descrita como Etapa 4b opcional na análise.

Para o próprio adulto jogar não é preciso liberar nada: `B` abre direto
(abaixo).

### Um portão de PIN só

O PIN é pedido **na abertura do GameLock** e mais nada — exceto *Trocar o PIN* e
*Apagar o PIN*, que pedem o atual porque mexem na própria credencial.

Até a 0.9.0, desproteger com `A`, liberar com `Y` e desligar a chave geral
pediam o PIN de novo. Saiu na 1.0.1: pedir o mesmo segredo a quem acabou de
digitá-lo não protege nada, e cada um desses efeitos já era alcançável por um
caminho livre — desligar a chave geral dá no mesmo que desmarcar os jogos um a
um. Um portão, uma sessão autenticada, duas exceções sobre a senha.

O que isso assume: quem passou do portão é o responsável. Não há expiração de
sessão, então GameLock deixado aberto é GameLock destravado — feche ao sair.

---

## Idioma

Dois idiomas: **português** e **inglês**. Troca em Configurações (`X`), na linha
*Idioma / Language* — o rótulo dela aparece sempre nos dois idiomas, porque quem
cai no idioma errado precisa reconhecer essa linha sem saber ler as outras. A
escolha vai para o `config.json` (`"lang": 0` português, `1` inglês) e vale na
próxima abertura.

O padrão é português. `config.json` gravado por versão anterior à 1.0.1 não tem a
chave `lang` e continua válido — a ausência vale como português.

Na implementação não há tabela de strings nem IDs: um macro `T(pt, en)`
(`main.c`) devolve um lado ou o outro conforme `g_cfg.lang`, com a tradução
colada no original. Sem ID, não há como um lado desencontrar o outro — e um
`grep` acha os dois textos de uma vez. O custo é que todo texto vive no `.c`:
trocar uma frase recompila. Para dois idiomas num app desse tamanho, é o preço
mais barato.

As mensagens técnicas que só aparecem no rodapé de Configurações seguem a mesma
regra, exceto as que são puro nome de API e código de erro
(`appletRequestLaunchApplication(...): 0x...`) — essas não têm o que traduzir.

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
  "lang": 0,
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

O `make` da raiz **já compila o sysmodule** e embute o `.nsp` no romfs do `.nro`
(é assim que o menu consegue instalá-lo). Para mexer só no sysmodule:

```sh
cd gamelock/sysmodule && make   # saída: GameLockRelock.nsp
```

O `.nro` ficou ~90 KB maior por causa do `.nsp` embutido. `make clean` na raiz
limpa os dois e apaga o `romfs/`.

O ícone do `.nro` é o `icon.jpg` da raiz (256×256 JPEG, exigência do formato),
apontado por `ICON` no `Makefile`. O original em alta resolução fica em
`source/icon.png` — trocou na 1.0.2, e o `icon.jpg` é gerado dele por
redimensionamento direto, sem corte (as duas artes são quadradas).

### Arquivos

```text
gamelock/
├── Makefile          # template oficial do switch-examples + TARGET/APP_* e pkg-config do SDL2
├── icon.jpg          # ícone do .nro (256×256)
├── source/
│   ├── main.c        # UI SDL2, navegação, menu, telas de PIN, textos pt/en, orquestração
│   ├── games.c/.h    # ns: lista de títulos, nome e ícone
│   ├── pin.c/.h      # PBKDF2-HMAC-SHA256, salt, comparação em tempo constante, KATs
│   ├── config.c/.h   # sdmc:/config/GameLock/config.json
│   ├── block.c/.h    # stubs de exefs: aplicar, liberar, reconciliar, política
│   └── relock.c/.h   # instalar/remover o sysmodule no cartão (romfs -> contents)
├── romfs/            # gerado pelo make: só o exefs.nsp do sysmodule, fora do git
└── sysmodule/        # relock opcional (Etapa 4b)
    ├── Makefile      # template oficial de sysmodule + os fontes compartilhados
    ├── GameLockRelock.json   # npdm: Title ID e as quatro permissões
    └── source/main.c # boot + pm:dmnt, chama block_apply
```

O sysmodule **compila os mesmos** `block.c`, `config.c` e `pin.c` do `.nro`
(`SHARED_CFILES` no `sysmodule/Makefile`), sem cópia: a lista de NSOs e o formato
do `config.json` existem numa única implementação. Por isso `block.h` não depende
mais de `games.h` — o sysmodule não tem o que fazer com `ns`.

---

## Testes

Autoteste do PBKDF2 roda **no console, a cada abertura**: KATs `c=1`, `c=2`,
`c=4096` (`P="password"`, `S="salt"`, `dkLen=32`), mais a sanidade da comparação
em tempo constante e da validação de dígitos. Falhando, o cadastro de PIN é
bloqueado e a tela avisa.

| Teste | Como | Status |
|---|---|---|
| Compila sem warning | `make` | **OK** — build limpo, 0 avisos, `GameLock.nro` com 8,98 MB, ícone embutido (2026-11-06) |
| Sysmodule compila | `cd sysmodule && make` | **OK** — `GameLockRelock.nsp` com 77 KB, Title ID e as quatro permissões conferidas no `.npdm` (2026-11-12) |
| `make` da raiz embute o `.nsp` | `make clean && make` | **OK** — build do zero, 0 avisos, `.nro` com 9,07 MB e o `.nsp` no romfs (2026-11-12) |
| Instalar relock pelo menu | Configurações, *Relock imediato*, `A` | **OK** — validado em console em 2026-11-12 |
| Remover relock pelo menu | no mesmo item, `Y` | **OK** — validado em console em 2026-11-12 |
| Estado do item confere com o cartão | instalar, sair, reabrir e ler o rótulo | **OK** — validado em console em 2026-11-12 |
| Aviso de versão diferente | trocar o `.nsp` do cartão por outro de tamanho diferente | **OK** — validado em console em 2026-11-12 |
| Sysmodule carrega no boot | instalar com o `boot2.flag` e reiniciar | **OK** — validado em console em 2026-11-12 |
| **Stub volta depois do `B`** | `B` num jogo bloqueado, esperar o jogo abrir, fechar e tentar pelo HOME | **OK** — validado em console em 2026-11-12 |
| Partida em curso não quebra | depois do `B`, jogar alguns minutos no jogo aberto | **OK** — validado em console em 2026-11-12 |
| Jogo liberado com `Y` continua abrindo | `Y`, abrir pelo HOME duas vezes seguidas | **OK** — validado em console em 2026-11-12 |
| Stub volta depois de desligar na tomada | `B`, desligar no meio, religar, tentar pelo HOME | **OK** — validado em console em 2026-11-12 |
| Forwarder do GameLock não é bloqueado | abrir pelo forwarder com o sysmodule ativo | **OK** — validado em console em 2026-11-12 |
| Sem `config.json` o sysmodule não faz nada | apagar o config e reiniciar | **OK** — validado em console em 2026-11-12 |
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
| `B` abre jogo **bloqueado** | apertar `B` sem liberar: deve abrir | **OK** |
| Bloqueio volta sozinho | depois do `B` acima, reabrir o GameLock e conferir o HOME | **OK** |
| Tela de espera aparece | abrir com biblioteca grande | **OK** |
| Troca de idioma | alternar em Configurações, sair, reabrir | **OK** |
| Portão de PIN único | `A`, `Y` e a chave geral devem agir sem pedir teclado | **OK** |

Validado em console pelo autor em 2026-11-06, 0.8.0 e 0.9.0: proteger, bloquear
de verdade no HOME, liberar com `Y`, travar de volta, sobreviver ao reboot, PIN,
teclado próprio, `B` abrindo o jogo nos dois modos e a tela de espera.

Validado em console pelo autor em 2026-11-07, 1.0.1: o portão de PIN único (`A`,
`Y` e a chave geral agindo direto, sem teclado), a troca de idioma sobrevivendo a
fechar e reabrir, `B` abrindo jogo bloqueado, e — o que mais importava — reabrir
o GameLock depois disso repondo o stub, com o jogo voltando a não subir pelo
HOME. A promessa de que o `B` não deixa rastro está confirmada no hardware.

A 1.0.2 troca só a arte do ícone embutido no `.nro`. Nenhuma linha de
comportamento mudou.

A 1.1.0 acrescenta o sysmodule de relock (`sysmodule/`, opcional), a instalação
dele pelo menu (`relock.c`, com o `.nsp` embutido no romfs) e move o
`games_self_program_id` para dentro do `block.c` para que os dois binários
compartilhem `block.c`/`config.c`/`pin.c`. O bloqueio em si é o mesmo da 1.0.2.

Validado em console pelo autor em 2026-11-12, 1.1.0: instalar e remover o relock
pelo menu, o sysmodule carregando no boot, e — o que a versão existe para
resolver — o stub voltando depois do `B`, com o jogo em curso intacto e o título
parando de subir pelo HOME sem ninguém reabrir o GameLock. Também conferidos o
jogo liberado com `Y` seguindo aberto, o retrave depois de desligar na tomada, o
forwarder não sendo bloqueado e o sysmodule ficando quieto sem `config.json`.

Os `não executado` que restam são casos de borda — SD cheio, `config.json`
corrompido, jogo reinstalado, conflito com LayeredFS.

---

## Limitações conhecidas

- **O bloqueio vale no próximo launch.** Jogo já rodando, ou em suspensão, não é
  afetado até fechar.
- **Depois de abrir um jogo bloqueado com `B`, ele fica sem stub até a próxima
  abertura do GameLock** — nessa janela ele sobe pelo HOME. O `.nro` não tem como
  fechar o stub depois do pedido de launch: o sistema assume o foreground e o
  processo morre antes de poder gravar. Isso é o que o
  [sysmodule de relock](#relock-imediato-sysmodule-opcional) resolve, reduzindo a
  janela a cerca de um segundo **com o jogo já em foreground** — na prática,
  ninguém está no HOME nesse instante. Sem o sysmodule, o retrave é abrir o
  GameLock uma vez ao terminar de jogar, ou usar `Y`/`Y` em vez de `B`.
- **O relock reage a uma aplicação em foreground, não ao fim da partida.** Se o
  jogo cair direto para o HOME sem nunca ter criado processo (erro de launch, por
  exemplo), não há gatilho — o stub volta no próximo boot.
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
