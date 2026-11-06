// GameLock - Etapa 4: bloqueio efetivo dos jogos protegidos.
//
// Interface, PIN e persistencia das etapas anteriores, mais o bloqueio real:
// os jogos marcados ganham stubs de exefs no cartao (ver block.c) e nao sobem
// mais pelo menu HOME. Liberar um jogo pede o PIN e remove os stubs.
//
// Entrada pelos controles via libnx (pad*, services/hid.h); o SDL2 cuida do
// video. O teclado numerico do PIN e desenhado aqui, nao e o applet do sistema.
//
// APIs, todas conferidas no header:
//   nsInitialize / nsExit                    services/ns.h:244
//   plInitialize / plGetSharedFontByType     services/pl.h:36,48
//   hmacSha256CalculateMac                   crypto/hmac.h:41   (via pin.c)
//   randomGet                                kernel/random.h:15 (via pin.c)
//
// O bloqueio usa File Stubbing do loader do Atmosphere -- documentado, nao
// inventado. Ver o cabecalho de block.h.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <switch.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#include "block.h"
#include "config.h"
#include "games.h"
#include "pin.h"

#define GAMELOCK_VERSION "0.8.0"

#define SCREEN_W 1280
#define SCREEN_H 720

#define ROW_H        86
#define LIST_TOP     104
#define ROWS_VISIBLE 6
#define ICON_PX      72
#define ICON_KEEP    (ROWS_VISIBLE * 2)

#define HINT_Y  (SCREEN_H - 70)   // legenda dos botoes, abaixo da ultima linha
#define MSG_Y   (SCREEN_H - 38)   // mensagem de erro, abaixo da legenda

#define MENU_TOP      112
#define MENU_ROW_H    44

#define PIN_ATTEMPTS 3

// Teclado numerico proprio (ver pin_prompt): 3 colunas x 4 linhas.
#define KEY_COLS 3
#define KEY_ROWS 4
#define KEY_W    108
#define KEY_H     76
#define KEY_GAP   14
#define KEY_X0   ((SCREEN_W - (KEY_COLS * KEY_W + (KEY_COLS - 1) * KEY_GAP)) / 2)
#define KEY_Y0   248

static const SDL_Color kWhite  = { 235, 235, 235, 255 };
static const SDL_Color kDim    = { 150, 150, 150, 255 };
static const SDL_Color kLocked = { 255, 170,  60, 255 };
static const SDL_Color kFree   = { 120, 220, 140, 255 };
static const SDL_Color kError  = { 255,  95,  95, 255 };

typedef enum {
    MODE_LIST = 0,
    MODE_MENU,
} Mode;

typedef enum {
    ITEM_PIN_CREATE,
    ITEM_PIN_CHANGE,
    ITEM_PIN_REMOVE,
    ITEM_PROTECTION,
    ITEM_LOCK_ALL,
    ITEM_APPLY,
    ITEM_SAVE,
    ITEM_BACK,
} MenuItemId;

// Glifos dos botoes do console, da fonte compartilhada Nintendo Extended
// (PlSharedFontType_NintendoExt, pl.h:23). Os codepoints sao os mapeados em
// WerWolv/libtesla (include/tesla.hpp:186-192), nao chutados.
// Cada glifo fica em seu proprio literal: escape \x em C come todos os digitos
// hex seguintes, entao colar texto depois quebraria o caractere.
#define GLYPH_A     "\xEE\x82\xA0"   // U+E0A0
#define GLYPH_B     "\xEE\x82\xA1"   // U+E0A1
#define GLYPH_X     "\xEE\x82\xA2"   // U+E0A2
#define GLYPH_Y     "\xEE\x82\xA3"   // U+E0A3
#define GLYPH_ZL    "\xEE\x82\xA6"   // U+E0A6
#define GLYPH_ZR    "\xEE\x82\xA7"   // U+E0A7
#define GLYPH_PLUS  "\xEE\x82\xB5"   // U+E0B5
#define GLYPH_UP    "\xEE\x81\xB9"   // U+E079
#define GLYPH_DOWN  "\xEE\x81\xBA"   // U+E07A

typedef struct {
    const char *glyph;
    const char *label;
} Legend;

static SDL_Window   *g_win;
static SDL_Renderer *g_ren;
static TTF_Font     *g_font;
static TTF_Font     *g_font_small;
static TTF_Font     *g_font_icon;   // Nintendo Extended, so para os glifos
static u64           g_frame;

static Config g_cfg;
static Game  *g_games;
static size_t g_count;

static PadState g_pad;
static bool g_crypto_ok;

// Duas mensagens separadas de proposito: a tela inicial e vista por quem nao
// deveria aprender como a protecao funciona, entao detalhe tecnico (caminhos no
// cartao, Title ID, codigos de erro, estado do PIN) fica so em Configuracoes.
static char g_msg[160];      // curta, tela da lista
static bool g_msg_error;
static char g_diag[224];     // tecnica, so no menu
static bool g_diag_error;

static void msg_set(bool error, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_msg, sizeof(g_msg), fmt, args);
    va_end(args);
    g_msg_error = error;
}

static void diag_set(bool error, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_diag, sizeof(g_diag), fmt, args);
    va_end(args);
    g_diag_error = error;
}

// ponytail: textura de texto recriada a cada frame. Sao ~15 strings por quadro;
// se engasgar, cachear uma textura por linha.
static void text(TTF_Font *font, int x, int y, SDL_Color color, const char *str)
{
    if (str == NULL || str[0] == '\0') {
        return;
    }

    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, str, color);
    if (surf == NULL) {
        return;
    }

    SDL_Texture *tex = SDL_CreateTextureFromSurface(g_ren, surf);
    SDL_Rect dst = { x, y, surf->w, surf->h };
    SDL_FreeSurface(surf);

    if (tex != NULL) {
        SDL_RenderCopy(g_ren, tex, NULL, &dst);
        SDL_DestroyTexture(tex);
    }
}

// Texto centrado em x_center.
static void text_center(TTF_Font *font, int x_center, int y, SDL_Color color, const char *str)
{
    if (str == NULL || str[0] == '\0') {
        return;
    }

    int w = 0, h = 0;
    if (TTF_SizeUTF8(font, str, &w, &h) != 0) {
        w = 0;
    }
    text(font, x_center - w / 2, y, color, str);
}

// Texto alinhado a direita de x_right.
static void text_right(TTF_Font *font, int x_right, int y, SDL_Color color, const char *str)
{
    if (str == NULL || str[0] == '\0') {
        return;
    }

    int w = 0, h = 0;
    if (TTF_SizeUTF8(font, str, &w, &h) != 0) {
        w = 0;
    }
    text(font, x_right - w, y, color, str);
}

// Linha de ajuda com os glifos dos botoes, em vez de letras entre parenteses.
static void draw_legend(int x, int y, const Legend *items, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        int w = 0, h = 0;

        if (TTF_SizeUTF8(g_font_icon, items[i].glyph, &w, &h) != 0) {
            w = 24;
        }
        text(g_font_icon, x, y, kWhite, items[i].glyph);
        x += w + 8;

        if (TTF_SizeUTF8(g_font_small, items[i].label, &w, &h) != 0) {
            w = 80;
        }
        text(g_font_small, x, y, kDim, items[i].label);
        x += w + 26;
    }
}

// Desenha um texto que pode nao caber em max_w: trunca com "..." e, quando
// selected, rola de ida e volta para mostrar o resto.
//
// ponytail: o corte comeca por estimativa proporcional e so recua -- medir byte
// a byte seriam dezenas de TTF_SizeUTF8 por linha, por quadro.
static void draw_clipped(TTF_Font *font, int x, int y, int max_w, SDL_Color color,
                         const char *str, bool selected)
{
    int w = 0, h = 0;
    if (TTF_SizeUTF8(font, str, &w, &h) != 0 || w <= max_w) {
        text(font, x, y, color, str);
        return;
    }

    if (selected) {
        const int over = w - max_w;
        const int hold = 40;                       // quadros parados em cada ponta
        const int span = over + hold * 2;
        const int t = (int)((g_frame / 2) % (u64)(span * 2));
        int off = (t < span) ? (t - hold) : ((span * 2 - 1 - t) - hold);

        if (off < 0)    { off = 0; }
        if (off > over) { off = over; }

        SDL_Rect clip = { x, y, max_w, h };
        SDL_RenderSetClipRect(g_ren, &clip);
        text(font, x - off, y, color, str);
        SDL_RenderSetClipRect(g_ren, NULL);
        return;
    }

    char buf[0x210];
    size_t len = strlen(str);
    if (len >= sizeof(buf)) {
        len = sizeof(buf) - 1;
    }
    memcpy(buf, str, len);
    buf[len] = '\0';

    size_t cut = (size_t)((double)len * max_w / (double)w);
    if (cut > len) {
        cut = len;
    }

    while (cut > 0) {
        // Nao cortar no meio de um caractere UTF-8.
        while (cut > 0 && ((unsigned char)buf[cut] & 0xC0) == 0x80) {
            cut--;
        }

        char probe[0x214];
        const char saved = buf[cut];
        buf[cut] = '\0';
        snprintf(probe, sizeof(probe), "%s...", buf);
        buf[cut] = saved;

        if (TTF_SizeUTF8(font, probe, &w, &h) == 0 && w <= max_w) {
            text(font, x, y, color, probe);
            return;
        }
        cut--;
    }

    text(font, x, y, color, "...");
}

static TTF_Font *open_system_font(int ptsize)
{
    PlFontData fd;
    if (R_FAILED(plGetSharedFontByType(&fd, PlSharedFontType_Standard))) {
        return NULL;
    }

    SDL_RWops *rw = SDL_RWFromConstMem(fd.address, (int)fd.size);
    if (rw == NULL) {
        return NULL;
    }

    // freesrc = 1: o SDL fecha o RWops. A memoria e do pl, nunca liberada aqui.
    return TTF_OpenFontRW(rw, 1, ptsize);
}

// Fonte dos glifos de botao (pl.h:23, PlSharedFontType_NintendoExt).
static TTF_Font *open_icon_font(int ptsize)
{
    PlFontData fd;
    if (R_FAILED(plGetSharedFontByType(&fd, PlSharedFontType_NintendoExt))) {
        return NULL;
    }

    SDL_RWops *rw = SDL_RWFromConstMem(fd.address, (int)fd.size);
    if (rw == NULL) {
        return NULL;
    }
    return TTF_OpenFontRW(rw, 1, ptsize);
}

// --------------------------------------------- PIN (teclado proprio, em SDL2)

// Teclado numerico desenhado pelo GameLock, nao o applet swkbd do sistema.
// Duas razoes: o layout de teclas do applet nao e configuravel (so os textos, o
// botao OK e as duas teclas opcionais de canto -- swkbd.h:22,394-432), e o
// applet de teclado pode nao abrir quando o homebrew roda em modo applet.
static const char *const kKeys[KEY_ROWS][KEY_COLS] = {
    { "1", "2", "3" },
    { "4", "5", "6" },
    { "7", "8", "9" },
    { "<", "0", "OK" },
};

static void draw_keypad(const char *header, size_t len, int row, int col, const char *warn)
{
    SDL_SetRenderDrawColor(g_ren, 20, 22, 28, 255);
    SDL_RenderClear(g_ren);

    text_center(g_font, SCREEN_W / 2, 96, kWhite, header);

    // Digitos digitados, mascarados.
    char masked[PIN_MAX_DIGITS * 2 + 1];
    masked[0] = '\0';
    for (size_t i = 0; i < len && i < PIN_MAX_DIGITS; i++) {
        strcat(masked, "* ");
    }
    text_center(g_font, SCREEN_W / 2, 170, kFree, masked);

    for (int r = 0; r < KEY_ROWS; r++) {
        for (int c = 0; c < KEY_COLS; c++) {
            const int x = KEY_X0 + c * (KEY_W + KEY_GAP);
            const int y = KEY_Y0 + r * (KEY_H + KEY_GAP);
            const bool focused = (r == row && c == col);

            SDL_Rect cell = { x, y, KEY_W, KEY_H };
            if (focused) {
                SDL_SetRenderDrawColor(g_ren, 70, 95, 135, 255);
            } else {
                SDL_SetRenderDrawColor(g_ren, 38, 42, 52, 255);
            }
            SDL_RenderFillRect(g_ren, &cell);

            text_center(g_font, x + KEY_W / 2, y + KEY_H / 2 - 18, kWhite, kKeys[r][c]);
        }
    }

    const int grid_bottom = KEY_Y0 + KEY_ROWS * KEY_H + (KEY_ROWS - 1) * KEY_GAP;

    if (warn != NULL && warn[0] != '\0') {
        text_center(g_font_small, SCREEN_W / 2, grid_bottom + 20, kError, warn);
    }

    char hint[96];
    snprintf(hint, sizeof(hint), "%d a %d digitos", PIN_MIN_DIGITS, PIN_MAX_DIGITS);
    text_center(g_font_small, SCREEN_W / 2, grid_bottom + 52, kDim, hint);
    static const Legend keys[] = {
        { GLYPH_A,    "escolhe"  },
        { GLYPH_B,    "apaga"    },
        { GLYPH_PLUS, "cancela"  },
    };
    draw_legend(SCREEN_W / 2 - 190, grid_bottom + 78, keys, 3);

    SDL_RenderPresent(g_ren);
}

// Le um PIN. false se o usuario cancelou.
static bool ask_pin(const char *header, char *out, size_t out_size)
{
    size_t len = 0;
    out[0] = '\0';

    int row = 0, col = 0;
    char warn[96];
    warn[0] = '\0';

    while (appletMainLoop()) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                return false;
            }
        }

        padUpdate(&g_pad);
        const u64 down = padGetButtonsDown(&g_pad);

        if (down & HidNpadButton_Plus) {
            return false;
        }

        if ((down & HidNpadButton_Up) && row > 0)             { row--; }
        if ((down & HidNpadButton_Down) && row < KEY_ROWS - 1) { row++; }
        if ((down & HidNpadButton_Left) && col > 0)            { col--; }
        if ((down & HidNpadButton_Right) && col < KEY_COLS - 1) { col++; }

        bool backspace = (down & HidNpadButton_B) != 0;
        bool confirm = false;
        char digit = '\0';

        if (down & HidNpadButton_A) {
            const char *key = kKeys[row][col];
            if (key[0] == '<') {
                backspace = true;
            } else if (key[0] == 'O') {
                confirm = true;
            } else {
                digit = key[0];
            }
        }

        if (digit != '\0' && len + 1 < out_size && len < PIN_MAX_DIGITS) {
            out[len++] = digit;
            out[len] = '\0';
            warn[0] = '\0';
        }

        if (backspace && len > 0) {
            out[--len] = '\0';
            warn[0] = '\0';
        }

        if (confirm) {
            if (pin_digits_valid(out)) {
                return true;
            }
            snprintf(warn, sizeof(warn), "O PIN precisa ter de %d a %d digitos.",
                     PIN_MIN_DIGITS, PIN_MAX_DIGITS);
        }

        draw_keypad(header, len, row, col, warn);
    }

    return false;
}

static bool require_pin(const char *header)
{
    if (!g_cfg.has_pin) {
        return true;
    }

    for (int attempt = PIN_ATTEMPTS; attempt > 0; attempt--) {
        char entered[PIN_MAX_DIGITS + 2];
        if (!ask_pin(header, entered, sizeof(entered))) {
            memset(entered, 0, sizeof(entered));
            return false;
        }

        const bool ok = config_check_pin(&g_cfg, entered);
        memset(entered, 0, sizeof(entered));

        if (ok) {
            return true;
        }

        msg_set(true, "PIN incorreto. Tentativas restantes: %d", attempt - 1);
    }

    return false;
}

// ------------------------------------------------------- gravacao + bloqueio

static bool save_config(void)
{
    if (config_save(&g_cfg)) {
        return true;
    }
    msg_set(true, "Nao foi possivel gravar no cartao.");
    diag_set(true, "FALHA ao salvar. O %s anterior foi mantido.", CONFIG_PATH);
    return false;
}

// Reconcilia o cartao com a configuracao e conta o resultado na tela.
static void apply_blocks(const char *prefix)
{
    BlockSyncResult r;
    block_sync(&g_cfg, &r);

    // A lista de posse dos stubs mudou: gravar, senao na proxima abertura o
    // GameLock nao sabe mais quais stubs sao dele.
    if (r.config_changed && !config_save(&g_cfg)) {
        msg_set(true, "Nao foi possivel gravar no cartao.");
        diag_set(true, "%s stubs mexidos, mas FALHOU gravar a lista de posse.", prefix);
        return;
    }

    if (r.failures > 0) {
        msg_set(true, "Algo deu errado ao aplicar no cartao.");
        diag_set(true, "%s %zu bloqueado(s), %zu liberado(s), %zu FALHA(S) de arquivo no SD.",
                 prefix, r.applied, r.released, r.failures);
        return;
    }

    if (r.conflicts > 0) {
        msg_set(true, "Um dos jogos nao pode ser protegido. Veja Configuracoes.");
        diag_set(true, "%s %zu bloqueado(s), %zu liberado(s). ATENCAO: %016lX tem exefs solto no SD, o stub nao pega.",
                 prefix, r.applied, r.released, r.first_conflict);
        return;
    }

    diag_set(false, "%s %zu bloqueado(s), %zu liberado(s).", prefix, r.applied, r.released);
}

static void refresh_flags(void)
{
    for (size_t i = 0; i < g_count; i++) {
        g_games[i].locked = config_is_locked(&g_cfg, g_games[i].tid);
        g_games[i].authorized = config_is_authorized(&g_cfg, g_games[i].tid);
    }
}

// ------------------------------------------------------------------- acoes

static void action_pin_create(void)
{
    if (!g_crypto_ok) {
        msg_set(true, "Erro interno de seguranca: cadastro de PIN desativado.");
        return;
    }

    char first[PIN_MAX_DIGITS + 2];
    char again[PIN_MAX_DIGITS + 2];

    if (!ask_pin("Novo PIN", first, sizeof(first))) {
        return;
    }
    if (!ask_pin("Repita o novo PIN", again, sizeof(again))) {
        memset(first, 0, sizeof(first));
        return;
    }

    if (strcmp(first, again) != 0) {
        memset(first, 0, sizeof(first));
        memset(again, 0, sizeof(again));
        msg_set(true, "Os PINs digitados nao sao iguais. Nada foi alterado.");
        return;
    }

    config_set_pin(&g_cfg, first);
    memset(first, 0, sizeof(first));
    memset(again, 0, sizeof(again));

    if (save_config()) {
        msg_set(false, "PIN cadastrado e salvo.");
    }
}

static void action_pin_change(void)
{
    if (!require_pin("PIN atual")) {
        return;
    }
    action_pin_create();
}

static void action_pin_remove(void)
{
    if (!require_pin("PIN atual")) {
        return;
    }

    config_clear_pin(&g_cfg);
    if (save_config()) {
        msg_set(false, "PIN removido. O GameLock abre sem PIN agora.");
    }
}

static void action_toggle_protection(void)
{
    if (!require_pin("PIN para mudar a protecao")) {
        return;
    }

    g_cfg.protection_enabled = !g_cfg.protection_enabled;
    if (!save_config()) {
        g_cfg.protection_enabled = !g_cfg.protection_enabled;
        return;
    }

    apply_blocks(g_cfg.protection_enabled ? "Protecao ativada:" : "Protecao desativada:");
    if (!g_msg_error) {
        msg_set(false, g_cfg.protection_enabled ? "Protecao ativada." : "Protecao desativada.");
    }
}

static void action_lock_all(void)
{
    if (g_cfg.authorized.count == 0) {
        msg_set(false, "Nenhuma autorizacao pendente.");
        return;
    }

    config_clear_authorizations(&g_cfg);
    if (!save_config()) {
        return;
    }

    refresh_flags();
    apply_blocks("Autorizacoes removidas:");
    if (!g_msg_error) {
        msg_set(false, "Todos os jogos protegidos foram travados.");
    }
}

// A: marca ou desmarca a protecao do jogo.
static void action_toggle_lock(Game *game)
{
    const bool target = !game->locked;

    if (!target && !require_pin("PIN para desproteger")) {
        return;
    }

    if (!config_set_locked(&g_cfg, game->tid, target)) {
        msg_set(true, "Sem memoria para registrar o jogo.");
        return;
    }

    if (!config_save(&g_cfg)) {
        // Reverte para a tela nao mentir sobre o que esta no cartao.
        config_set_locked(&g_cfg, game->tid, !target);
        msg_set(true, "Nao foi possivel gravar no cartao. A marcacao foi desfeita.");
        return;
    }

    refresh_flags();
    apply_blocks(target ? "Protegido:" : "Desprotegido:");
    if (!g_msg_error) {
        msg_set(false, "%s: %s", game->name, target ? "protegido" : "liberado para todos");
    }
}

// Y: libera um jogo protegido (pede PIN) ou trava de novo.
static void action_toggle_authorization(Game *game)
{
    if (!game->locked) {
        msg_set(false, "Esse jogo nao esta protegido: nada a liberar.");
        return;
    }

    const bool target = !game->authorized;

    if (target && !require_pin("PIN para liberar o jogo")) {
        return;
    }

    if (!config_set_authorized(&g_cfg, game->tid, target)) {
        msg_set(true, "Sem memoria para registrar a autorizacao.");
        return;
    }

    if (!config_save(&g_cfg)) {
        config_set_authorized(&g_cfg, game->tid, !target);
        msg_set(true, "Nao foi possivel gravar no cartao. A liberacao foi desfeita.");
        return;
    }

    refresh_flags();
    apply_blocks(target ? "Liberado ate travar de novo:" : "Travado de novo:");
    if (!g_msg_error) {
        msg_set(false, "%s: %s", game->name,
                target ? "liberado ate travar de novo" : "travado");
    }
}

// -------------------------------------------------------------------- render

static void icons_sync(Game *games, size_t count, size_t first)
{
    const size_t lo = (first > ICON_KEEP) ? first - ICON_KEEP : 0;
    size_t hi = first + ROWS_VISIBLE + ICON_KEEP;
    if (hi > count) {
        hi = count;
    }

    for (size_t i = 0; i < count; i++) {
        const bool want = (i >= lo && i < hi);

        if (want && games[i].icon_tex == NULL) {
            void  *jpeg = NULL;
            size_t size = 0;
            if (R_FAILED(games_read_icon(games[i].tid, &jpeg, &size))) {
                continue;
            }

            SDL_RWops *rw = SDL_RWFromConstMem(jpeg, (int)size);
            SDL_Surface *surf = (rw != NULL) ? IMG_Load_RW(rw, 1) : NULL;
            free(jpeg);

            if (surf != NULL) {
                games[i].icon_tex = SDL_CreateTextureFromSurface(g_ren, surf);
                SDL_FreeSurface(surf);
            }
        } else if (!want && games[i].icon_tex != NULL) {
            SDL_DestroyTexture((SDL_Texture *)games[i].icon_tex);
            games[i].icon_tex = NULL;
        }
    }
}

static const char *state_label(const Game *game, SDL_Color *out_color)
{
    if (!game->locked) {
        *out_color = kDim;
        return "livre";
    }
    if (game->authorized) {
        *out_color = kFree;
        return "LIBERADO";
    }
    if (!g_cfg.protection_enabled) {
        *out_color = kDim;
        return "marcado (protecao off)";
    }
    *out_color = kLocked;
    return "BLOQUEADO";
}

static void draw_row(const Game *game, int y, bool selected)
{
    if (selected) {
        SDL_SetRenderDrawColor(g_ren, 45, 60, 85, 255);
        SDL_Rect hl = { 40, y - 6, SCREEN_W - 80, ROW_H - 8 };
        SDL_RenderFillRect(g_ren, &hl);
    }

    SDL_Rect icon = { 56, y, ICON_PX, ICON_PX };
    if (game->icon_tex != NULL) {
        SDL_RenderCopy(g_ren, (SDL_Texture *)game->icon_tex, NULL, &icon);
    } else {
        SDL_SetRenderDrawColor(g_ren, 70, 70, 70, 255);
        SDL_RenderFillRect(g_ren, &icon);
    }

    SDL_Color state_color;
    const char *state = state_label(game, &state_color);

    // Sem Title ID aqui: a tela inicial mostra apenas nome e estado. Detalhe
    // tecnico fica em Configuracoes.
    draw_clipped(g_font, 148, y + 20, SCREEN_W - 320 - 148, game->locked ? kLocked : kWhite,
                 game->name, selected);
    text(g_font_small, SCREEN_W - 300, y + 24, state_color, state);
}

static size_t menu_items(MenuItemId *out)
{
    size_t n = 0;
    if (g_cfg.has_pin) {
        out[n++] = ITEM_PIN_CHANGE;
        out[n++] = ITEM_PIN_REMOVE;
    } else {
        out[n++] = ITEM_PIN_CREATE;
    }
    out[n++] = ITEM_PROTECTION;
    out[n++] = ITEM_LOCK_ALL;
    out[n++] = ITEM_APPLY;
    out[n++] = ITEM_SAVE;
    out[n++] = ITEM_BACK;
    return n;
}

static const char *menu_label(MenuItemId id, char *scratch, size_t scratch_size)
{
    switch (id) {
        case ITEM_PIN_CREATE: return "Cadastrar PIN";
        case ITEM_PIN_CHANGE: return "Trocar o PIN";
        case ITEM_PIN_REMOVE: return "Apagar o PIN";
        case ITEM_PROTECTION:
            snprintf(scratch, scratch_size, "Protecao dos jogos marcados: %s",
                     g_cfg.protection_enabled ? "LIGADA" : "desligada");
            return scratch;
        case ITEM_LOCK_ALL:
            if (g_cfg.authorized.count == 0) {
                return "Travar de volta os jogos liberados (nenhum agora)";
            }
            snprintf(scratch, scratch_size, "Travar de volta os %zu jogo(s) liberado(s)",
                     g_cfg.authorized.count);
            return scratch;
        case ITEM_APPLY:      return "Conferir e corrigir o cartao";
        case ITEM_SAVE:       return "Regravar o arquivo de configuracao";
        case ITEM_BACK:       return "Voltar para a lista";
    }
    return "?";
}

// Uma frase do que o item em foco faz. Rotulo curto nao basta aqui: travar,
// conferir e regravar sao tres coisas diferentes e pareciam a mesma.
static const char *menu_hint(MenuItemId id)
{
    switch (id) {
        case ITEM_PIN_CREATE:
            return "Define o PIN. Depois dele, o GameLock pede o PIN para abrir.";
        case ITEM_PIN_CHANGE:
            return "Pede o PIN atual e grava um novo no lugar.";
        case ITEM_PIN_REMOVE:
            return "Tira o PIN: o GameLock volta a abrir sem pedir nada.";
        case ITEM_PROTECTION:
            return "Chave geral. Desligada, todos os jogos voltam a abrir, sem perder as marcacoes.";
        case ITEM_LOCK_ALL:
            return "Cancela as liberacoes feitas com Y: os jogos marcados voltam a ficar bloqueados.";
        case ITEM_APPLY:
            return "Compara o cartao com esta configuracao e corrige o que estiver fora. Nada muda de marcacao.";
        case ITEM_SAVE:
            return "Escreve o config.json de novo. Util so se a gravacao automatica falhou antes.";
        case ITEM_BACK:
            return "Fecha as configuracoes.";
    }
    return "";
}

// Tela inicial: so as legendas dos botoes. Erro aparece porque falha silenciosa
// seria pior; sucesso se ve no proprio estado da linha.
static void draw_footer_list(void)
{
    static const Legend list[] = {
        { GLYPH_UP GLYPH_DOWN, "navegar" },
        { GLYPH_A,    "proteger"      },
        { GLYPH_Y,    "liberar"       },
        { GLYPH_ZL,   ""              },
        { GLYPH_ZR,   "pagina"        },
        { GLYPH_X,    "configuracoes" },
        { GLYPH_PLUS, "sair"          },
    };
    draw_legend(48, HINT_Y, list, 7);

    if (g_msg_error) {
        text(g_font_small, 48, MSG_Y, kError, g_msg);
    }
}

// Topo a direita do menu: o painel de estado.
static void draw_status_panel(void)
{
    size_t locked_count = 0;
    for (size_t i = 0; i < g_count; i++) {
        if (g_games[i].locked) {
            locked_count++;
        }
    }

    const int x = SCREEN_W - 48;
    char line[64];

    text_right(g_font_small, x, 36, g_cfg.has_pin ? kFree : kDim,
               g_cfg.has_pin ? "PIN DEFINIDO" : "PIN NAO DEFINIDO");

    text_right(g_font_small, x, 62, g_cfg.protection_enabled ? kLocked : kDim,
               g_cfg.protection_enabled ? "PROTECAO ATIVADA" : "PROTECAO DESATIVADA");

    snprintf(line, sizeof(line), "MARCADOS: %zu", locked_count);
    text_right(g_font_small, x, 88, kWhite, line);

    snprintf(line, sizeof(line), "LIBERADOS: %zu", g_cfg.authorized.count);
    text_right(g_font_small, x, 114, kWhite, line);
}

// Rodape de Configuracoes: detalhe tecnico, atras do PIN.
static void draw_footer_menu(size_t sel)
{
    if (sel < g_count) {
        char detail[256];
        snprintf(detail, sizeof(detail), "Jogo selecionado: %016lX   |   %s",
                 g_games[sel].tid, CONFIG_PATH);
        text(g_font_small, 48, SCREEN_H - 92, kDim, detail);
    }

    text(g_font_small, 48, SCREEN_H - 64, g_diag_error ? kError : kFree, g_diag);
    text(g_font_small, 48, SCREEN_H - 38, kDim, "Desenvolvido por @Kazbonfim2 | CamorraDevs");
}

static void draw(size_t sel, size_t first, Result load_rc, Mode mode, size_t menu_sel)
{
    SDL_SetRenderDrawColor(g_ren, 20, 22, 28, 255);
    SDL_RenderClear(g_ren);

    char header[160];
    snprintf(header, sizeof(header), "GameLock %s  -  %zu jogo(s)", GAMELOCK_VERSION, g_count);
    text(g_font, 48, 36, kWhite, header);

    if (mode == MODE_MENU) {
        draw_status_panel();

        MenuItemId items[10];
        const size_t n = menu_items(items);

        for (size_t i = 0; i < n; i++) {
            const int y = MENU_TOP + (int)i * MENU_ROW_H;

            if (i == menu_sel) {
                SDL_SetRenderDrawColor(g_ren, 45, 60, 85, 255);
                SDL_Rect hl = { 40, y - 5, SCREEN_W - 80, MENU_ROW_H - 6 };
                SDL_RenderFillRect(g_ren, &hl);
            }

            char scratch[96];
            text(g_font_small, 72, y, (i == menu_sel) ? kWhite : kDim,
                 menu_label(items[i], scratch, sizeof(scratch)));
        }

        const int hint_y = MENU_TOP + (int)n * MENU_ROW_H + 24;
        text(g_font_small, 72, hint_y, kFree, menu_hint(items[menu_sel]));

        static const Legend menu[] = {
            { GLYPH_A, "confirmar" },
            { GLYPH_B, "voltar"    },
        };
        draw_legend(48, SCREEN_H - 128, menu, 2);
        draw_footer_menu(sel);
    } else {
        if (R_FAILED(load_rc)) {
            text(g_font, 48, LIST_TOP, kError, "Nao foi possivel ler a lista de jogos.");
        } else if (g_count == 0) {
            text(g_font, 48, LIST_TOP, kDim, "Nenhum jogo instalado encontrado.");
        } else {
            for (size_t i = 0; i < ROWS_VISIBLE && first + i < g_count; i++) {
                draw_row(&g_games[first + i], LIST_TOP + (int)i * ROW_H, (first + i) == sel);
            }
        }

        draw_footer_list();
    }

    SDL_RenderPresent(g_ren);
}

// ---------------------------------------------------------------------- init

static bool init_video(void)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        return false;
    }
    if (TTF_Init() != 0) {
        return false;
    }
    if ((IMG_Init(IMG_INIT_JPG) & IMG_INIT_JPG) == 0) {
        return false;
    }

    g_win = SDL_CreateWindow("GameLock", 0, 0, SCREEN_W, SCREEN_H, 0);
    if (g_win == NULL) {
        return false;
    }

    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (g_ren == NULL) {
        return false;
    }

    g_font = open_system_font(28);
    g_font_small = open_system_font(20);
    g_font_icon = open_icon_font(22);
    return (g_font != NULL && g_font_small != NULL && g_font_icon != NULL);
}

static void shutdown_all(void)
{
    for (size_t i = 0; i < g_count; i++) {
        if (g_games[i].icon_tex != NULL) {
            SDL_DestroyTexture((SDL_Texture *)g_games[i].icon_tex);
        }
    }
    games_unload(g_games);
    config_free(&g_cfg);

    TTF_CloseFont(g_font_icon);
    TTF_CloseFont(g_font_small);
    TTF_CloseFont(g_font);
    SDL_DestroyRenderer(g_ren);
    SDL_DestroyWindow(g_win);
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();

    nsExit();
    plExit();
}

int main(int argc, char **argv)
{
    plInitialize(PlServiceType_User);
    nsInitialize();

    g_crypto_ok = pin_selftest();

    bool missing = false;
    bool invalid = false;
    config_ensure_dirs();
    config_load(&g_cfg, &missing, &invalid);

    // Antes da porta de PIN: o teclado numerico le os controles.
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&g_pad);

    if (!init_video()) {
        nsExit();
        plExit();
        return 1;
    }

    if (invalid) {
        diag_set(true, "config.json invalido: seguindo sem PIN e sem jogo protegido.");
    } else if (missing) {
        diag_set(false, "Nenhum config.json ainda: sera criado ao salvar.");
    }

    if (!g_crypto_ok) {
        msg_set(true, "Erro interno de seguranca: funcoes de PIN desativadas.");
    }

    // Porta de entrada: se ha PIN, ninguem mexe na lista sem digitar.
    if (g_cfg.has_pin && !require_pin("PIN do GameLock")) {
        shutdown_all();
        return 0;
    }

    const Result load_rc = games_load(&g_games, &g_count);
    if (R_FAILED(load_rc)) {
        diag_set(true, "games_load (ns): 0x%X", load_rc);
    }
    refresh_flags();

    // O cartao pode ter ficado dessincronizado (config editada fora, stub
    // sobrando de um jogo desinstalado). Reconcilia na abertura.
    if (!invalid && (g_cfg.locked.count > 0 || g_cfg.blocked.count > 0)) {
        apply_blocks("Estado aplicado:");
    }

    size_t sel = 0;
    size_t first = 0;
    size_t menu_sel = 0;
    Mode   mode = MODE_LIST;
    icons_sync(g_games, g_count, first);

    bool running = true;
    while (running && appletMainLoop()) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = false;
            }
        }

        padUpdate(&g_pad);
        const u64 down = padGetButtonsDown(&g_pad);

        if (mode == MODE_LIST) {
            if (down & HidNpadButton_Plus) {
                running = false;
            }
            if (down & HidNpadButton_X) {
                mode = MODE_MENU;
                menu_sel = 0;
            }

            if (g_count > 0) {
                const size_t last = g_count - 1;

                if ((down & HidNpadButton_Down) && sel < last) {
                    sel++;
                }
                if ((down & HidNpadButton_Up) && sel > 0) {
                    sel--;
                }
                if (down & HidNpadButton_ZR) {
                    sel = (sel + ROWS_VISIBLE > last) ? last : sel + ROWS_VISIBLE;
                }
                if (down & HidNpadButton_ZL) {
                    sel = (sel < ROWS_VISIBLE) ? 0 : sel - ROWS_VISIBLE;
                }
                if (down & HidNpadButton_A) {
                    action_toggle_lock(&g_games[sel]);
                }
                if (down & HidNpadButton_Y) {
                    action_toggle_authorization(&g_games[sel]);
                }

                if (sel < first) {
                    first = sel;
                }
                if (sel >= first + ROWS_VISIBLE) {
                    first = sel - ROWS_VISIBLE + 1;
                }

                icons_sync(g_games, g_count, first);
            }
        } else {
            MenuItemId items[10];
            const size_t n = menu_items(items);

            if (menu_sel >= n) {
                menu_sel = n - 1;
            }
            if ((down & HidNpadButton_Down) && menu_sel + 1 < n) {
                menu_sel++;
            }
            if ((down & HidNpadButton_Up) && menu_sel > 0) {
                menu_sel--;
            }
            if (down & HidNpadButton_B) {
                mode = MODE_LIST;
            }
            if (down & HidNpadButton_A) {
                switch (items[menu_sel]) {
                    case ITEM_PIN_CREATE: action_pin_create();        break;
                    case ITEM_PIN_CHANGE: action_pin_change();        break;
                    case ITEM_PIN_REMOVE: action_pin_remove();        break;
                    case ITEM_PROTECTION: action_toggle_protection(); break;
                    case ITEM_LOCK_ALL:   action_lock_all();          break;
                    case ITEM_APPLY:      apply_blocks("Reaplicado:"); break;
                    case ITEM_SAVE:
                        if (save_config()) {
                            msg_set(false, "Configuracao salva.");
                        }
                        break;
                    case ITEM_BACK:       mode = MODE_LIST;           break;
                }
                menu_sel = 0;
            }
        }

        g_frame++;
        draw(sel, first, load_rc, mode, menu_sel);
    }

    shutdown_all();
    return 0;
}
