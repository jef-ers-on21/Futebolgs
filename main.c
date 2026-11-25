/* ============================================================
   FUTEBOL GS  -  Jogo de futebol para PSP / PPSSPP
   Modos: Amistoso, Campeonato Municipal, Liga Da Liberdade,
          Seja Treinador, Torne Uma Estrela, Modo Campanha, Editar
   Compilar com PSPSDK (pspdev). Ver build.yml.
   ============================================================ */
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <psppower.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <ctype.h>

PSP_MODULE_INFO("FUTEBOL GS", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);

#define SW 480
#define SH 272
#define STRIDE 512
#define MAXT 64
#define GSZ 16
#define NGRP 5
#define NTEAMS 80
#define NPL 512
#define NSEL 16
#define NP 5
#define MAGIC 0x46475335u
#define SAVEDIR "ms0:/PSP/SAVEDATA/FUTEBOLGS"

#define RGB(r,g,b) (0xFF000000u|((unsigned int)(b)<<16)|((unsigned int)(g)<<8)|(unsigned int)(r))
#define C_WHITE RGB(240,240,240)
#define C_GOLD  RGB(250,205,50)
#define C_DARK  RGB(10,10,10)

/* ---------------- Callbacks de saida ---------------- */
static int exit_cb(int a1, int a2, void *c) { sceKernelExitGame(); return 0; }
static int cb_thread(SceSize args, void *argp) {
    int id = sceKernelCreateCallback("exit_cb", exit_cb, NULL);
    sceKernelRegisterExitCallback(id);
    sceKernelSleepThreadCB();
    return 0;
}
static void setup_callbacks(void) {
    int t = sceKernelCreateThread("cb", cb_thread, 0x11, 0xFA0, 0, NULL);
    if (t >= 0) sceKernelStartThread(t, 0, NULL);
}

/* ---------------- Graficos basicos ---------------- */
static u32 *vram = (u32 *)0x44000000;
static SceCtrlData pad, oldpad;
#define PRESSED(b) (((pad.Buttons & (b)) != 0) && ((oldpad.Buttons & (b)) == 0))
#define HELD(b)    ((pad.Buttons & (b)) != 0)

static const u32 PAL[12] = {
    RGB(220,30,30), RGB(30,80,220), RGB(250,220,20), RGB(20,160,60),
    RGB(250,250,250), RGB(25,25,25), RGB(240,120,20), RGB(140,40,180),
    RGB(20,190,200), RGB(250,120,170), RGB(120,70,30), RGB(150,150,150)
};
static const char *PALN[12] = {"Vermelho","Azul","Amarelo","Verde","Branco","Preto",
                               "Laranja","Roxo","Ciano","Rosa","Marrom","Cinza"};

static void px(int x, int y, u32 c) {
    if (x >= 0 && x < SW && y >= 0 && y < SH) vram[y * STRIDE + x] = c;
}
static void rect(int x, int y, int w, int h, u32 c) {
    int i, j;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SW) w = SW - x;
    if (y + h > SH) h = SH - y;
    if (w <= 0 || h <= 0) return;
    for (j = 0; j < h; j++) {
        u32 *p = vram + (y + j) * STRIDE + x;
        for (i = 0; i < w; i++) p[i] = c;
    }
}
static void circle(int cx, int cy, int r, u32 c) {
    int a;
    for (a = 0; a < 256; a++) {
        float t = a * (6.2831853f / 256.0f);
        px(cx + (int)(cosf(t) * r), cy + (int)(sinf(t) * r), c);
    }
}
static void tx(int x, int y, u32 c, const char *fmt, ...) {
    char b[100]; char *p; va_list a;
    va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a);
    for (p = b; *p; p++, x += 8) pspDebugScreenPutChar(x, y, c, (unsigned char)*p);
}
static void txc(int y, u32 c, const char *fmt, ...) {
    char b[100]; va_list a;
    va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a);
    tx((SW - (int)strlen(b) * 8) / 2, y, c, "%s", b);
}
static void frame(void) {
    sceDisplayWaitVblankStart();
    oldpad = pad;
    sceCtrlPeekBufferPositive(&pad, 1);
}
static float rf(void) { return (rand() & 0x7FFF) / 32768.0f; }

static void bg(const char *title) {
    int i;
    rect(0, 0, SW, SH, RGB(10, 40, 20));
    for (i = 26; i < SH; i += 40) rect(0, i, SW, 20, RGB(13, 50, 25));
    rect(0, 0, SW, 24, RGB(0, 22, 8));
    rect(0, 24, SW, 2, C_GOLD);
    tx(8, 8, C_GOLD, "%s", title);
}

/* ---------------- Dados ---------------- */
typedef struct { char name[16]; int str, c1, c2; } Team;
typedef struct { char name[16]; unsigned char ovr, pos, nat, team; } Plyr;
typedef struct { unsigned char skin, hair, hcol, beard, build, boots, band, glove, socks; } Look;
typedef struct {
    int n, round, nr;
    int id[16];
    int pts[16], pj[16], w[16], d[16], l[16], gf[16], ga[16];
    int fa[30][8], fb[30][8];
} League;
typedef struct {
    unsigned int magic;
    Team t[NTEAMS];
    int camp_team, camp_stage, camp_tac;
    int star_started; char star_name[16];
    int star_pos, star_age, star_ovr, star_team, star_season, star_games;
    int star_goals, star_assists, star_rate, star_xp, star_trained;
    int star_tgoals, star_tassists;
    int diff, gspd;
    int star_money, star_moved, star_off[6];
    int coach_on, coach_team, coach_form, coach_tac, coach_tp, coach_bonus, coach_money;
    int coach_sq[8][2], coach_mk[6][2];
    League coach_lg;
    Plyr plr[NPL];
    int conv[NSEL][8];
    Look slook;
    int star_nat;
    int fame, moral, day, done, house[6], boots_lv, car, rel[5], pstate, aw[8];
    int fooded, totg, ticket, wc_pending, wc_boost, gym_n, posts, last_rate, home_reward;
} Save;
static Save S;
#define TM (S.t)

static const char *DEFN[MAXT] = {
    /* Liga Municipal */
    "Municipal FC", "Liberdade EC", "Atletico GS", "Uniao Esporte",
    "Estrela Norte", "Gremio Vila", "Real Bairro", "Sporting GS",
    "Ferroviario", "Aguia Dourada", "Tigres FC", "Leoes da Serra",
    "Vitoria SC", "Cruzeiro Sul", "Dragoes AC", "Raio FC",
    /* Liga Brasileira */
    "Sao Jorge FC", "Carioca AC", "Paulista EC", "Mineiro SC",
    "Gaucho FC", "Baiano EC", "Recife United", "Curitiba Verde",
    "Amazonas FC", "Pantanal EC", "Cerrado SC", "Litoral FC",
    "Sertao AC", "Serra Azul", "Pampa FC", "Manaus Real",
    /* Liga Europeia */
    "North Albion", "Rhein Athletic", "Costa Azzurra", "Iberia Real",
    "Nordic Stars", "Danube FC", "Alpine SC", "Baltic United",
    "Thames Rovers", "Seine FC", "Tiber City", "Gallia AC",
    "Hellas United", "Iberico FC", "Vistula SC", "Highland FC",
    /* Liga das Estrelas */
    "Galaxy Kings", "Titans FC", "Phoenix United", "Olympus SC",
    "Apollo FC", "Zeus AC", "Nova Stars", "Meteoro FC",
    "Cometa United", "Orion SC", "Atlas FC", "Solaris AC",
    "Vulcano FC", "Imperio Real", "Dynamo Prime", "Legends FC"
};
static const int DEFS[MAXT] = {
    78, 82, 74, 70, 66, 62, 76, 72, 58, 80, 68, 64, 71, 60, 85, 56,
    72, 80, 86, 78, 82, 70, 68, 74, 64, 66, 62, 69, 60, 75, 77, 65,
    88, 84, 83, 86, 76, 74, 73, 70, 85, 80, 79, 77, 69, 72, 67, 71,
    95, 92, 90, 93, 88, 91, 86, 87, 84, 89, 85, 83, 82, 94, 81, 96
};
static const char *GRPN[NGRP] = {"Liga Municipal", "Liga Brasileira", "Liga Europeia", "Liga das Estrelas", "Selecoes"};
static const char *DIFN[4] = {"Facil", "Normal", "Dificil", "Lendario"};
static const char *SPDN[3] = {"Lenta", "Normal", "Rapida"};
static const int DSTR[4] = {-14, 0, 8, 16};
static const float GSPD[3] = {0.8f, 1.0f, 1.25f};
static float dadj(void);

static const char *TACN[3] = {"Defensivo", "Equilibrado", "Ofensivo"};
static const int DUR[4] = {60, 120, 180, 300};

/* ---------------- Jogadores e selecoes ---------------- */
static const char *FNM[24] = {"Joao", "Pedro", "Lucas", "Davi", "Rafa", "Bruno", "Diego", "Caio", "Andre", "Igor", "Theo", "Hugo",
                              "Luan", "Yuri", "Enzo", "Nico", "Joel", "Tito", "Elias", "Ivan", "Otto", "Kaue", "Dudu", "Leo"};
static const char *LNM[24] = {"Silva", "Souza", "Costa", "Lima", "Rocha", "Alves", "Dias", "Moura", "Pinto", "Reis", "Gomes", "Nunes",
                              "Cruz", "Melo", "Lopes", "Vieira", "Ramos", "Pires", "Neves", "Brito", "Cunha", "Teles", "Maia", "Prado"};
static const char *SELN[NSEL] = {"Brasil", "Argentina", "Franca", "Alemanha", "Espanha", "Italia", "Inglaterra", "Portugal",
                                 "Holanda", "Uruguai", "Croacia", "Mexico", "Japao", "Coreia Sul", "Nigeria", "EUA"};
static const int SELC[NSEL][2] = {{2, 3}, {8, 4}, {1, 4}, {4, 5}, {0, 2}, {1, 4}, {4, 0}, {0, 3},
                                  {6, 1}, {8, 5}, {0, 4}, {3, 0}, {1, 0}, {0, 1}, {3, 4}, {4, 1}};
static const int NATB[NSEL] = {3, 2, 2, 3, 1, 1, 1, 0, 0, 0, -1, -1, -1, -2, -2, -1};
static const char *POSA[4] = {"ATA", "MEI", "ZAG", "GOL"};

static void pname(int idx, char *out) { snprintf(out, 24, "%s %s", FNM[idx % 24], LNM[(idx / 24) % 24]); }

static void recalc_team(int t) {
    int i, n = 0, s = 0;
    for (i = 0; i < NPL; i++) if (S.plr[i].team == t) { s += S.plr[i].ovr; n++; }
    if (n > 0) TM[t].str = s / n;
}
static void recalc_sel(int k) {
    int i, s = 0;
    for (i = 0; i < 8; i++) s += S.plr[S.conv[k][i]].ovr;
    TM[MAXT + k].str = s / 8;
}
static int roster_of(int team, int *out) {
    int i, n = 0;
    if (team >= MAXT) { for (i = 0; i < 8; i++) out[n++] = S.conv[team - MAXT][i]; return n; }
    for (i = 0; i < NPL; i++) if (S.plr[i].team == team && n < 12) out[n++] = i;
    return n;
}
static void gen_players(void) {
    unsigned int rs = 20240611u;
    int t, k, i, j, n, uid, cand[64];
    for (t = 0; t < MAXT; t++) for (k = 0; k < 8; k++) {
        Plyr *p = &S.plr[t * 8 + k];
        char nm[24];
        int v;
        uid = t * 8 + k;
        rs = rs * 1103515245u + 12345u;
        p->team = (unsigned char)t;
        p->pos = (unsigned char)((k == 0) ? 3 : ((k < 3) ? 2 : ((k < 5) ? 1 : 0)));
        p->nat = (unsigned char)(((uid % 16) * 5 + uid / 16) % 16);
        v = TM[t].str + (int)((rs >> 16) % 17) - 8 + NATB[p->nat];
        if (p->pos == 3) v -= 2;
        if (v < 40) v = 40;
        if (v > 99) v = 99;
        p->ovr = (unsigned char)v;
        pname((uid * 37) % 576, nm);
        strncpy(p->name, nm, 15); p->name[15] = 0;
    }
    for (t = 0; t < MAXT; t++) recalc_team(t);
    for (k = 0; k < NSEL; k++) {
        n = 0;
        for (i = 0; i < NPL; i++) if (S.plr[i].nat == k && n < 64) cand[n++] = i;
        for (i = 0; i < 8; i++) {
            int b = i;
            for (j = i + 1; j < n; j++) if (S.plr[cand[j]].ovr > S.plr[cand[b]].ovr) b = j;
            uid = cand[b]; cand[b] = cand[i]; cand[i] = uid;
            S.conv[k][i] = cand[i];
        }
        recalc_sel(k);
    }
}
static void reset_teams(void) {
    int i;
    memset(S.t, 0, sizeof S.t);
    for (i = 0; i < MAXT; i++) {
        strncpy(TM[i].name, DEFN[i], 15);
        TM[i].str = DEFS[i];
        TM[i].c1 = i % 12;
        TM[i].c2 = (i * 5 + 3) % 12;
        if (TM[i].c2 == TM[i].c1) TM[i].c2 = (TM[i].c2 + 1) % 12;
    }
    for (i = 0; i < NSEL; i++) {
        strncpy(TM[MAXT + i].name, SELN[i], 15);
        TM[MAXT + i].c1 = SELC[i][0];
        TM[MAXT + i].c2 = SELC[i][1];
        TM[MAXT + i].str = 75;
    }
    gen_players();
}
static void save_game(void) {
    SceUID f;
    sceIoMkdir("ms0:/PSP/SAVEDATA", 0777);
    sceIoMkdir(SAVEDIR, 0777);
    S.magic = MAGIC;
    f = sceIoOpen(SAVEDIR "/DADOS.BIN", PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (f >= 0) { sceIoWrite(f, &S, sizeof S); sceIoClose(f); }
}
static void load_game(void) {
    int ok = 0;
    SceUID f;
    memset(&S, 0, sizeof S);
    f = sceIoOpen(SAVEDIR "/DADOS.BIN", PSP_O_RDONLY, 0777);
    if (f >= 0) {
        int r = sceIoRead(f, &S, sizeof S);
        sceIoClose(f);
        ok = (r == (int)sizeof S && S.magic == MAGIC);
    }
    if (!ok) {
        memset(&S, 0, sizeof S);
        reset_teams();
        S.magic = MAGIC;
        S.camp_team = -1;
        S.diff = 1; S.gspd = 1;
    }
    if (S.diff < 0 || S.diff > 3) S.diff = 1;
    if (S.gspd < 0 || S.gspd > 2) S.gspd = 1;
}
static float dadj(void) { return (float)DSTR[S.diff]; }

/* ---------------- Menus genericos ---------------- */
static void (*menu_extra)(void) = NULL;

static int menu(const char *title, const char **items, int n, int sel, int y0, const char *foot) {
    int vis = (SH - y0 - 24) / 16, k, top, i, y;
    if (vis < 1) vis = 1;
    for (;;) {
        frame();
        if (PRESSED(PSP_CTRL_UP)) sel = (sel + n - 1) % n;
        if (PRESSED(PSP_CTRL_DOWN)) sel = (sel + 1) % n;
        if (PRESSED(PSP_CTRL_CROSS)) return sel;
        if (PRESSED(PSP_CTRL_CIRCLE)) return -1;
        bg(title);
        if (menu_extra) menu_extra();
        top = 0;
        if (n > vis) { top = sel - vis / 2; if (top < 0) top = 0; if (top > n - vis) top = n - vis; }
        for (k = 0; k < vis && top + k < n; k++) {
            i = top + k; y = y0 + k * 16;
            if (i == sel) {
                rect(30, y - 2, SW - 60, 14, C_GOLD);
                tx(40, y + 1, C_DARK, "%s", items[i]);
            } else tx(40, y + 1, RGB(235, 235, 235), "%s", items[i]);
        }
        tx(8, SH - 12, RGB(170, 200, 170), "%s", foot ? foot : "X: escolher   O: voltar");
    }
}

static void end_screen(const char *title, const char *a, const char *b, const char *c) {
    for (;;) {
        frame();
        if (PRESSED(PSP_CTRL_CROSS) || PRESSED(PSP_CTRL_CIRCLE)) break;
        bg(title);
        txc(80, C_GOLD, "%s", a);
        txc(115, C_WHITE, "%s", b);
        txc(145, C_WHITE, "%s", c);
        tx(8, SH - 12, RGB(170, 200, 170), "X: continuar");
    }
}

static int pick_team_in(const char *title, int grp, int exclude) {
    int base = grp * GSZ, sel = (exclude == base) ? 1 : 0, vis = 11, top, k, i, y, id;
    for (;;) {
        frame();
        if (PRESSED(PSP_CTRL_UP)) sel = (sel + GSZ - 1) % GSZ;
        if (PRESSED(PSP_CTRL_DOWN)) sel = (sel + 1) % GSZ;
        if (PRESSED(PSP_CTRL_LEFT)) sel = (sel + GSZ - 5) % GSZ;
        if (PRESSED(PSP_CTRL_RIGHT)) sel = (sel + 5) % GSZ;
        if (PRESSED(PSP_CTRL_CROSS) && base + sel != exclude) return base + sel;
        if (PRESSED(PSP_CTRL_CIRCLE)) return -1;
        bg(title);
        tx(SW - 8 - (int)strlen(GRPN[grp]) * 8, 8, C_WHITE, "%s", GRPN[grp]);
        top = sel - vis / 2; if (top < 0) top = 0; if (top > GSZ - vis) top = GSZ - vis;
        for (k = 0; k < vis; k++) {
            i = top + k; id = base + i; y = 34 + k * 19;
            if (i == sel) rect(20, y - 3, SW - 40, 17, RGB(40, 100, 50));
            rect(28, y, 10, 10, PAL[TM[id].c1]);
            rect(40, y, 10, 10, PAL[TM[id].c2]);
            tx(58, y + 1, (id == exclude) ? RGB(120, 120, 120) : C_WHITE, "%s", TM[id].name);
            tx(244, y + 1, C_GOLD, "%2d", TM[id].str);
            rect(270, y + 2, TM[id].str * 18 / 10, 7, (i == sel) ? C_GOLD : RGB(90, 160, 100));
        }
        tx(8, SH - 12, RGB(170, 200, 170), "X: escolher  O: voltar  Cima/Baixo");
    }
}
static int pick_team(const char *title, int exclude) {
    int g = 0;
    for (;;) {
        int t;
        g = menu("ESCOLHA A LIGA DO TIME", GRPN, NGRP, g, 60, NULL);
        if (g < 0) return -1;
        t = pick_team_in(title, g, exclude);
        if (t >= 0) return t;
    }
}

static int pick_tactic(void) {
    return menu("TATICA", TACN, 3, 1, 60, NULL);
}
static int pick_duration(void) {
    static const char *it[4] = {"1 minuto por tempo", "2 minutos por tempo", "3 minutos por tempo", "5 minutos por tempo"};
    int c = menu("DURACAO DA PARTIDA", it, 4, 1, 60, NULL);
    return (c < 0) ? -1 : DUR[c];
}
static int confirm(const char *title) {
    static const char *it[2] = {"Sim", "Nao"};
    return menu(title, it, 2, 1, 60, NULL) == 0;
}

/* ---------------- Jogadores em 2D (sprites) ---------------- */
static const u32 SKIN[5] = {RGB(255, 224, 189), RGB(240, 194, 150), RGB(198, 134, 66), RGB(141, 85, 36), RGB(90, 56, 37)};
static const u32 HAIRC[6] = {RGB(20, 20, 20), RGB(70, 40, 20), RGB(120, 80, 40), RGB(230, 200, 90), RGB(180, 60, 30), RGB(190, 190, 190)};
static const char *SKINN[5] = {"Clara", "Parda clara", "Parda", "Escura", "Negra"};
static const char *HAIRN[6] = {"Careca", "Curto", "Medio", "Black Power", "Moicano", "Trancas"};
static const char *BEARDN[4] = {"Nenhuma", "Bigode", "Curta", "Cheia"};
static const char *BUILDN[3] = {"Magro", "Normal", "Forte"};
static const char *SOCKN[3] = {"Curto", "Medio", "Longo"};

static Look plk[2][NP];
static char pnm[2][NP][16];
static int anim = 0;

static void look_of(int uid, Look *L) {
    int nat = S.plr[uid].nat;
    L->skin = (unsigned char)((uid * 3 + nat) % 5);
    L->hair = (unsigned char)((uid * 7 + 1) % 6);
    L->hcol = (unsigned char)((uid * 11 + 2) % 6);
    L->beard = (unsigned char)((uid % 9 == 0) ? 3 : ((uid % 5 == 0) ? 1 : ((uid % 7 == 0) ? 2 : 0)));
    L->build = (unsigned char)(uid % 3);
    L->boots = (unsigned char)((uid * 5) % 12);
    L->band = (unsigned char)((uid % 13 == 0) ? 1 + uid % 12 : 0);
    L->glove = 0;
    L->socks = (unsigned char)(uid % 3);
}
static void sr(int cx, int fy, int sc, int dx, int dy, int w, int h, u32 c) {
    rect(cx + dx * sc, fy + dy * sc, w * sc, h * sc, c);
}
/* cx = centro, fy = linha dos pes, sc = escala, fd = direcao (-1,0,1), ph = fase da corrida */
static void draw_sprite(int cx, int fy, const Look *L, u32 c1, u32 c2, int gk, int fd, int ph, int sc) {
    static const int SL[3] = {2, 3, 5};
    u32 skin = SKIN[L->skin % 5], hc = HAIRC[L->hcol % 6], boots = PAL[L->boots % 12];
    u32 shirt = c1, shorts = c2, sock = c2;
    int hw = 3 + (L->build % 3), sl = SL[L->socks % 3], l1 = (ph == 1), l2 = (ph == 2), h1, h2;
    if (gk) { shorts = C_DARK; sock = C_DARK; }
    sr(cx, fy, sc, -hw, -1, 2 * hw + 1, 1, RGB(10, 60, 20));
    /* pernas, meioes e chuteiras */
    h1 = 5 - l1; h2 = 5 - l2;
    sr(cx, fy, sc, -3, -7, 2, h1, skin);
    sr(cx, fy, sc, 1, -7, 2, h2, skin);
    sr(cx, fy, sc, -3, -2 - l1 - ((sl < h1) ? sl : h1), 2, (sl < h1) ? sl : h1, sock);
    sr(cx, fy, sc, 1, -2 - l2 - ((sl < h2) ? sl : h2), 2, (sl < h2) ? sl : h2, sock);
    sr(cx, fy, sc, -4, -2 - l1, 3, 2, boots);
    sr(cx, fy, sc, 1, -2 - l2, 3, 2, boots);
    /* calcao e camisa */
    sr(cx, fy, sc, -hw, -10, 2 * hw + 1, 3, shorts);
    sr(cx, fy, sc, -hw, -15, 2 * hw + 1, 5, shirt);
    if (!gk) sr(cx, fy, sc, -hw, -13, 2 * hw + 1, 1, c2);
    /* bracos e luvas */
    sr(cx, fy, sc, -hw - 1, -15, 1, 5, skin);
    sr(cx, fy, sc, hw + 1, -15, 1, 5, skin);
    sr(cx, fy, sc, -hw - 1, -15, 1, 2, shirt);
    sr(cx, fy, sc, hw + 1, -15, 1, 2, shirt);
    if (L->glove > 0) {
        u32 gc = PAL[(L->glove - 1) % 12];
        sr(cx, fy, sc, -hw - 1, -12, 1, 2, gc);
        sr(cx, fy, sc, hw + 1, -12, 1, 2, gc);
    }
    /* cabeca */
    sr(cx, fy, sc, -2, -19, 5, 4, skin);
    sr(cx, fy, sc, -1 + fd, -18, 1, 1, C_DARK);
    sr(cx, fy, sc, 1 + fd, -18, 1, 1, C_DARK);
    switch (L->hair % 6) {
        case 1: sr(cx, fy, sc, -2, -20, 5, 2, hc); break;
        case 2: sr(cx, fy, sc, -2, -20, 5, 2, hc); sr(cx, fy, sc, -3, -19, 1, 3, hc); sr(cx, fy, sc, 3, -19, 1, 3, hc); break;
        case 3: sr(cx, fy, sc, -4, -23, 9, 4, hc); sr(cx, fy, sc, -3, -19, 1, 1, hc); sr(cx, fy, sc, 3, -19, 1, 1, hc); break;
        case 4: sr(cx, fy, sc, -1, -23, 3, 4, hc); break;
        case 5: sr(cx, fy, sc, -2, -20, 5, 2, hc); sr(cx, fy, sc, -3, -19, 1, 6, hc); sr(cx, fy, sc, 3, -19, 1, 6, hc); break;
        default: break;
    }
    switch (L->beard % 4) {
        case 1: sr(cx, fy, sc, -1, -17, 3, 1, hc); break;
        case 2: sr(cx, fy, sc, -2, -16, 5, 1, hc); break;
        case 3: sr(cx, fy, sc, -2, -17, 5, 2, hc); break;
        default: break;
    }
    if (L->band > 0) sr(cx, fy, sc, -2, -19, 5, 1, PAL[(L->band - 1) % 12]);
}
/* escolhe 5 jogadores do elenco para os 5 lugares do campo */
static void map_side(int t, int teamid, int starfix) {
    static const int want[NP] = {3, 2, 2, 1, 0};
    int ids[12], n = roster_of(teamid, ids), used[12], i, k, best, uid;
    memset(used, 0, sizeof used);
    for (i = 0; i < NP; i++) {
        best = -1;
        for (k = 0; k < n; k++) {
            if (used[k] || S.plr[ids[k]].pos != want[i]) continue;
            if (best < 0 || S.plr[ids[k]].ovr > S.plr[ids[best]].ovr) best = k;
        }
        if (best < 0) for (k = 0; k < n; k++) {
            if (used[k]) continue;
            if (best < 0 || S.plr[ids[k]].ovr > S.plr[ids[best]].ovr) best = k;
        }
        if (best >= 0) {
            used[best] = 1; uid = ids[best];
            look_of(uid, &plk[t][i]);
            strncpy(pnm[t][i], S.plr[uid].name, 15); pnm[t][i][15] = 0;
            if (i == 0) plk[t][i].glove = (unsigned char)(1 + uid % 12);
        } else {
            memset(&plk[t][i], 0, sizeof(Look));
            strcpy(pnm[t][i], "Jogador");
        }
    }
    if (t == 0 && starfix >= 0 && starfix < NP) {
        plk[0][starfix] = S.slook;
        strncpy(pnm[0][starfix], S.star_name, 15); pnm[0][starfix][15] = 0;
    }
}

/* ---------------- Motor da partida ---------------- */
#define FX0 10
#define FY0 28
#define FX1 470
#define FY1 266
#define GY0 117
#define GY1 177

typedef struct { float x, y, fx, fy, vx, vy, nocatch; } Pl;
typedef struct { int g0, g1, sg, sa, quit; } MRes;

static Pl pl[2][NP];
static float bx, by, bvx, bvy;
static int ot = -1, oi = -1, lastk = -1, prevk = -1;
static int ctrl = 3, ctrlfix = -1;
static int tactic0 = 1, tactic1 = 1;
static int tstr[2] = {70, 70};
static float gs = 1.0f;
static int gkhold[2];
static const float HX[NP] = {0.03f, 0.22f, 0.22f, 0.50f, 0.74f};
static const float HY[NP] = {0.50f, 0.28f, 0.72f, 0.50f, 0.50f};

static void home_pos(int t, int i, float *x, float *y) {
    float nx = HX[i], ny = HY[i];
    float ballnx = (t == 0) ? (bx - FX0) / (float)(FX1 - FX0) : (FX1 - bx) / (float)(FX1 - FX0);
    if (i > 0) {
        nx += (ballnx - 0.5f) * 0.35f;
        nx += (((t == 0) ? tactic0 : tactic1) - 1) * 0.07f;
        if (nx < 0.08f) nx = 0.08f;
        if (nx > 0.92f) nx = 0.92f;
        ny += ((by - FY0) / (float)(FY1 - FY0) - 0.5f) * 0.2f;
    }
    *x = (t == 0) ? FX0 + nx * (FX1 - FX0) : FX1 - nx * (FX1 - FX0);
    *y = FY0 + ny * (FY1 - FY0);
}
static void kickoff(int tk) {
    int t, i;
    bx = 240; by = 147; bvx = bvy = 0;
    for (t = 0; t < 2; t++) for (i = 0; i < NP; i++) {
        float x, y;
        home_pos(t, i, &x, &y);
        pl[t][i].x = x; pl[t][i].y = y;
        pl[t][i].vx = pl[t][i].vy = 0;
        pl[t][i].fx = (t == 0) ? 1.0f : -1.0f; pl[t][i].fy = 0;
        pl[t][i].nocatch = 0;
    }
    pl[tk][3].x = 240; pl[tk][3].y = 147;
    ot = tk; oi = 3; lastk = prevk = -1;
    gkhold[0] = gkhold[1] = 0;
}
static void kick(int t, int i, float dx, float dy, float sp) {
    float d = sqrtf(dx * dx + dy * dy);
    if (d < 0.001f) { dx = (t == 0) ? 1.0f : -1.0f; dy = 0; d = 1; }
    dx /= d; dy /= d;
    bvx = dx * sp; bvy = dy * sp;
    bx = pl[t][i].x + dx * 8; by = pl[t][i].y + dy * 8;
    ot = -1; oi = -1;
    pl[t][i].nocatch = 18;
    prevk = lastk; lastk = t * 10 + i;
}
static void jitter(float *dx, float *dy, int str) {
    float a = (((rand() % 2001) - 1000) / 1000.0f) * (0.03f + (100 - str) * 0.0013f);
    float c = cosf(a), s = sinf(a), nx = *dx * c - *dy * s, ny = *dx * s + *dy * c;
    *dx = nx; *dy = ny;
}
static int best_target(int t, int i, float dx, float dy) {
    int j, best = -1; float bs = -1e9f;
    for (j = 0; j < NP; j++) {
        float vx, vy, d, dot, s;
        if (j == i) continue;
        vx = pl[t][j].x - pl[t][i].x; vy = pl[t][j].y - pl[t][i].y;
        d = sqrtf(vx * vx + vy * vy);
        if (d < 20) continue;
        dot = (vx * dx + vy * dy) / d;
        if (dot < 0.45f) continue;
        s = dot * 100 - d * 0.15f;
        if (j == 0) s -= 60;
        if (s > bs) { bs = s; best = j; }
    }
    return best;
}
static void move_to(Pl *p, float tx_, float ty_, float spd) {
    float dx = tx_ - p->x, dy = ty_ - p->y, d = sqrtf(dx * dx + dy * dy);
    if (d > 1.0f) {
        float s = (d < spd) ? d : spd;
        p->vx = dx / d * s; p->vy = dy / d * s;
        p->fx = dx / d; p->fy = dy / d;
    } else { p->vx = p->vy = 0; }
}

static void gk_ai(int t) {
    Pl *p = &pl[t][0];
    float gx = (t == 0) ? FX0 + 10 : FX1 - 10, ty = by, spd = 1.2f + tstr[t] * 0.009f;
    if (ot == t && oi == 0) {
        ty = 147;
        if (++gkhold[t] > 50) {
            int j = 1 + rand() % 4;
            float dx = pl[t][j].x - p->x, dy = pl[t][j].y - p->y;
            jitter(&dx, &dy, 70);
            kick(t, 0, dx, dy, 5.6f);
        }
    } else if (ot == -1) {
        int toward = (t == 0) ? (bvx < -0.6f) : (bvx > 0.6f);
        if (toward) {
            float tt = (gx - bx) / bvx;
            if (tt > 0 && tt < 80) ty = by + bvy * tt;
        }
    } else if (ot == 1 - t) {
        ty = by * 0.5f + 147 * 0.5f;
    }
    if (ty < GY0 - 12) ty = GY0 - 12;
    if (ty > GY1 + 12) ty = GY1 + 12;
    move_to(p, gx, ty, spd);
}

static void ai_player(int t, int i, int chaser) {
    Pl *p = &pl[t][i];
    float spd = 1.1f + tstr[t] * 0.0065f;
    float gx = (t == 0) ? FX1 : FX0, gy = 147.0f, dir = (t == 0) ? 1.0f : -1.0f;
    if (i == 0) { gk_ai(t); return; }
    if (ot == t && oi == i) {
        float dx = gx - p->x, dy = gy - p->y, dg = sqrtf(dx * dx + dy * dy);
        int r = rand() % 100, j, near_ = 0;
        if (dg < 160 && (r < 3 || (dg < 70 && r < 12))) {
            float ty = gy + ((rand() % 47) - 23), sx = gx - p->x, sy = ty - p->y;
            jitter(&sx, &sy, tstr[t]);
            kick(t, i, sx, sy, 6.6f + (rand() % 10) * 0.1f);
            return;
        }
        if (t == 0 && ctrlfix >= 0 && i != ctrlfix && r < 3) {
            float sx = pl[0][ctrlfix].x - p->x, sy = pl[0][ctrlfix].y - p->y;
            float sd = sqrtf(sx * sx + sy * sy);
            if (sd > 25) { kick(t, i, sx, sy, 3.2f + sd * 0.028f > 7 ? 7 : 3.2f + sd * 0.028f); return; }
        }
        for (j = 1; j < NP; j++) {
            float ex = pl[1 - t][j].x - p->x, ey = pl[1 - t][j].y - p->y;
            if (ex * ex + ey * ey < 20 * 20 && ex * dir > 0) near_ = 1;
        }
        if (near_ && r < 8) {
            j = best_target(t, i, dir, 0);
            if (j >= 0) {
                float sx = pl[t][j].x - p->x, sy = pl[t][j].y - p->y;
                float sd = sqrtf(sx * sx + sy * sy), sp = 3.2f + sd * 0.028f;
                jitter(&sx, &sy, tstr[t] + 10);
                kick(t, i, sx, sy, sp > 7 ? 7 : sp);
                return;
            }
        }
        move_to(p, gx, gy, spd * 0.93f);
    } else if (chaser && ot != t) {
        move_to(p, bx + bvx * 4, by + bvy * 4, spd);
    } else {
        float hx, hy;
        home_pos(t, i, &hx, &hy);
        move_to(p, hx, hy, spd * 0.9f);
    }
}

static void human_control(int i) {
    Pl *p = &pl[0][i];
    float ax = (pad.Lx - 128) / 128.0f, ay = (pad.Ly - 128) / 128.0f, m, fx, fy, spd;
    if (fabsf(ax) < 0.28f) ax = 0;
    if (fabsf(ay) < 0.28f) ay = 0;
    if (HELD(PSP_CTRL_LEFT)) ax = -1;
    if (HELD(PSP_CTRL_RIGHT)) ax = 1;
    if (HELD(PSP_CTRL_UP)) ay = -1;
    if (HELD(PSP_CTRL_DOWN)) ay = 1;
    m = sqrtf(ax * ax + ay * ay);
    if (m > 0.1f) { p->fx = ax / m; p->fy = ay / m; }
    fx = p->fx; fy = p->fy;
    if (m > 1.0f) m = 1.0f;
    spd = 1.7f + tstr[0] * 0.004f;
    if (ctrlfix >= 0) spd *= (S.slook.build == 0) ? 1.05f : ((S.slook.build == 2) ? 0.97f : 1.0f);
    if (HELD(PSP_CTRL_RTRIGGER)) spd *= 1.28f;
    if (m > 0.1f) { p->vx = p->fx * spd * m; p->vy = p->fy * spd * m; }
    else { p->vx = p->vy = 0; }

    if (ot == 0 && oi == i) {
        if (PRESSED(PSP_CTRL_CROSS)) {
            int j = best_target(0, i, fx, fy);
            float dx, dy, sp;
            if (j >= 0) {
                float d;
                dx = pl[0][j].x - p->x; dy = pl[0][j].y - p->y;
                d = sqrtf(dx * dx + dy * dy);
                sp = 3.2f + d * 0.028f; if (sp > 7) sp = 7;
                jitter(&dx, &dy, tstr[0] + 15);
            } else { dx = fx; dy = fy; sp = 5; }
            kick(0, i, dx, dy, sp);
        } else if (PRESSED(PSP_CTRL_SQUARE)) {
            float ty = 147 + ay * 26, dx = FX1 - p->x, dy = ty - p->y;
            jitter(&dx, &dy, tstr[0]);
            kick(0, i, dx, dy, 7.4f + ((ctrlfix >= 0 && S.slook.build == 2) ? 0.5f : 0.0f));
        } else if (PRESSED(PSP_CTRL_TRIANGLE)) {
            kick(0, i, fx, fy, 8.6f);
        }
    } else {
        if (PRESSED(PSP_CTRL_CROSS) && ot == 1 && oi > 0) {
            Pl *o = &pl[1][oi];
            float dx = o->x - p->x, dy = o->y - p->y;
            if (dx * dx + dy * dy < 20.0f * 20.0f) {
                if (rand() % 100 < 65) { o->nocatch = 30; ot = 0; oi = i; }
                else p->nocatch = 25;
            }
        } else if (ot == -1 && PRESSED(PSP_CTRL_SQUARE)) {
            float dx = bx - p->x, dy = by - p->y;
            if (dx * dx + dy * dy < 14.0f * 14.0f) {
                dx = FX1 - bx; dy = 147 - by;
                jitter(&dx, &dy, tstr[0]);
                kick(0, i, dx, dy, 7.0f);
            }
        }
    }
}

/* retorna 0/1 = time que marcou, -1 = nada */
static int update_play(void) {
    int i, t, ch[2] = {-1, -1};
    float d, bd;
    for (t = 0; t < 2; t++) {
        bd = 1e9f;
        for (i = 1; i < NP; i++) {
            float dx, dy;
            if (t == 0 && i == ctrl) continue;
            dx = pl[t][i].x - bx; dy = pl[t][i].y - by;
            d = dx * dx + dy * dy;
            if (d < bd) { bd = d; ch[t] = i; }
        }
    }
    if (ctrlfix < 0) {
        if (ot == 0 && oi > 0) ctrl = oi;
        else if (ot != 0) {
            int bi = ctrl; float cd, dx, dy;
            bd = 1e9f;
            for (i = 1; i < NP; i++) {
                dx = pl[0][i].x - bx; dy = pl[0][i].y - by;
                d = dx * dx + dy * dy;
                if (d < bd) { bd = d; bi = i; }
            }
            dx = pl[0][ctrl].x - bx; dy = pl[0][ctrl].y - by;
            cd = dx * dx + dy * dy;
            if (bd < cd * 0.5f) ctrl = bi;
        }
    }
    for (t = 0; t < 2; t++) for (i = 0; i < NP; i++) {
        if (pl[t][i].nocatch > 0) pl[t][i].nocatch -= 1;
        if (t == 0 && i == ctrl) human_control(i);
        else ai_player(t, i, ch[t] == i);
    }
    for (t = 0; t < 2; t++) for (i = 0; i < NP; i++) {
        Pl *p = &pl[t][i];
        p->x += p->vx * gs; p->y += p->vy * gs;
        if (p->x < FX0 + 4) p->x = FX0 + 4;
        if (p->x > FX1 - 4) p->x = FX1 - 4;
        if (p->y < FY0 + 12) p->y = FY0 + 12;
        if (p->y > FY1 - 4) p->y = FY1 - 4;
    }
    /* desarmes automaticos da IA */
    if (ot >= 0 && oi > 0) {
        int q = 1 - ot;
        Pl *o = &pl[ot][oi];
        for (i = 1; i < NP; i++) {
            Pl *df = &pl[q][i];
            float dx = df->x - o->x, dy = df->y - o->y;
            if (q == 0 && i == ctrl) continue;
            if (dx * dx + dy * dy < 100.0f && df->nocatch <= 0) {
                if (rand() % 1000 < 6 + tstr[q] / 10) {
                    o->nocatch = 30; ot = q; oi = i;
                    break;
                }
            }
        }
    }
    if (ot >= 0) {
        Pl *o = &pl[ot][oi];
        bx = o->x + o->fx * 6; by = o->y + o->fy * 6;
        bvx = o->vx; bvy = o->vy;
        if (bx < FX0 + 3) bx = FX0 + 3;
        if (bx > FX1 - 3) bx = FX1 - 3;
        if (by < FY0 + 3) by = FY0 + 3;
        if (by > FY1 - 3) by = FY1 - 3;
    } else {
        float sp, bdist = 1e9f; int bt = -1, bi = -1;
        bx += bvx * gs; by += bvy * gs;
        bvx *= 0.986f; bvy *= 0.986f;
        if (fabsf(bvx) < 0.02f) bvx = 0;
        if (fabsf(bvy) < 0.02f) bvy = 0;
        if (by < FY0 + 3) { by = FY0 + 3; bvy = -bvy * 0.7f; }
        if (by > FY1 - 3) { by = FY1 - 3; bvy = -bvy * 0.7f; }
        if (bx < FX0 + 3) {
            if (by >= GY0 && by <= GY1) return 1;
            bx = FX0 + 3; bvx = -bvx * 0.7f;
        }
        if (bx > FX1 - 3) {
            if (by >= GY0 && by <= GY1) return 0;
            bx = FX1 - 3; bvx = -bvx * 0.7f;
        }
        sp = sqrtf(bvx * bvx + bvy * bvy);
        for (t = 0; t < 2; t++) for (i = 0; i < NP; i++) {
            Pl *p = &pl[t][i];
            float dx, dy, r;
            if (p->nocatch > 0) continue;
            dx = p->x - bx; dy = p->y - by;
            d = dx * dx + dy * dy;
            r = (i == 0) ? 10.0f : 7.0f;
            if (d < r * r && d < bdist) { bdist = d; bt = t; bi = i; }
        }
        if (bt >= 0) {
            if (bi == 0) {
                if (sp > 5.2f && (rand() % 100) >= (int)(25 + tstr[bt] * 0.4f)) {
                    bvx = -bvx * 0.4f; bvy += ((rand() % 7) - 3) * 0.8f;
                    pl[bt][0].nocatch = 10;
                } else { ot = bt; oi = 0; gkhold[bt] = 0; }
            } else if (sp < 6.0f) { ot = bt; oi = bi; }
        }
    }
    return -1;
}

/* ---------------- Desenho da partida ---------------- */
static void draw_player(int t, int i, int teamid, int iscontrolled) {
    Pl *p = &pl[t][i];
    int x = (int)p->x, y = (int)p->y + 4;
    u32 c1 = PAL[TM[teamid].c1], c2 = PAL[TM[teamid].c2];
    int fd = (p->fx > 0.3f) ? 1 : ((p->fx < -0.3f) ? -1 : 0);
    int mv = (fabsf(p->vx) + fabsf(p->vy) > 0.2f);
    int ph = mv ? 1 + ((anim / 6) % 2) : 0;
    if (i == 0) { c1 = RGB(250, 230, 40); c2 = C_DARK; }
    draw_sprite(x, y, &plk[t][i], c1, c2, i == 0, fd, ph, 1);
    if (iscontrolled) {
        rect(x - 3, y - 30, 7, 2, C_WHITE);
        rect(x - 2, y - 28, 5, 2, C_WHITE);
        rect(x - 1, y - 26, 3, 2, C_WHITE);
    }
}
static void draw_match(int ta, int tb, int g0, int g1, int fr, int total, int phase, int goalt) {
    int k, t, i, mn;
    u32 W = C_WHITE;
    rect(0, 24, SW, SH - 24, RGB(15, 80, 30));
    for (k = 0; k < 10; k++) rect(FX0 + k * 46, FY0, 46, FY1 - FY0, (k & 1) ? RGB(34, 139, 52) : RGB(44, 156, 62));
    rect(FX0, FY0, FX1 - FX0, 2, W); rect(FX0, FY1 - 2, FX1 - FX0, 2, W);
    rect(FX0, FY0, 2, FY1 - FY0, W); rect(FX1 - 2, FY0, 2, FY1 - FY0, W);
    rect(239, FY0, 2, FY1 - FY0, W);
    circle(240, 147, 30, W);
    rect(FX0, 92, 55, 2, W); rect(FX0, 202, 55, 2, W); rect(FX0 + 53, 92, 2, 112, W);
    rect(FX1 - 55, 92, 55, 2, W); rect(FX1 - 55, 202, 55, 2, W); rect(FX1 - 55, 92, 2, 112, W);
    rect(FX0 - 7, GY0, 7, 2, W); rect(FX0 - 7, GY1 - 2, 7, 2, W); rect(FX0 - 7, GY0, 2, GY1 - GY0, W);
    rect(FX1, GY0, 7, 2, W); rect(FX1, GY1 - 2, 7, 2, W); rect(FX1 + 5, GY0, 2, GY1 - GY0, W);
    anim++;
    {
        int ord[10], oc = 0, a2, b2;
        for (t = 0; t < 2; t++) for (i = 0; i < NP; i++) ord[oc++] = t * 10 + i;
        for (a2 = 1; a2 < 10; a2++) {
            int v = ord[a2];
            b2 = a2 - 1;
            while (b2 >= 0 && pl[ord[b2] / 10][ord[b2] % 10].y > pl[v / 10][v % 10].y) { ord[b2 + 1] = ord[b2]; b2--; }
            ord[b2 + 1] = v;
        }
        for (a2 = 0; a2 < 10; a2++) { t = ord[a2] / 10; i = ord[a2] % 10; draw_player(t, i, (t == 0) ? ta : tb, (t == 0 && i == ctrl)); }
    }
    tx(FX0 + 6, FY1 - 12, C_WHITE, "%s", pnm[0][ctrl]);
    rect((int)bx - 3, (int)by - 3, 7, 7, C_DARK);
    rect((int)bx - 2, (int)by - 2, 5, 5, W);
    /* HUD */
    rect(0, 0, SW, 24, RGB(0, 20, 8));
    rect(0, 24, SW, 2, C_GOLD);
    rect(6, 6, 10, 10, PAL[TM[ta].c1]);
    tx(20, 8, W, "%s", TM[ta].name);
    mn = fr * 90 / total; if (mn > 90) mn = 90;
    txc(8, C_GOLD, "%d x %d  %02d'", g0, g1, mn);
    rect(SW - 16, 6, 10, 10, PAL[TM[tb].c1]);
    tx(SW - 20 - (int)strlen(TM[tb].name) * 8, 8, W, "%s", TM[tb].name);
    if (phase == 0) {
        rect(60, 100, 360, 86, C_DARK);
        rect(62, 102, 356, 82, RGB(20, 60, 30));
        txc(108, C_GOLD, "PREPARAR!");
        txc(128, W, "X: passe/desarme   QUAD: chute");
        txc(142, W, "TRI: lancamento    R: correr");
        txc(156, W, "START: pausa");
        txc(170, C_GOLD, "Dif: %s   Vel: %s", DIFN[S.diff], SPDN[S.gspd]);
    } else if (phase == 2) {
        rect(100, 105, 280, 50, C_DARK);
        rect(102, 107, 276, 46, RGB(160, 20, 20));
        txc(112, C_GOLD, "GOOOOOL!");
        txc(132, W, "%s", TM[goalt == 0 ? ta : tb].name);
    } else if (phase == 3) {
        rect(100, 105, 280, 50, C_DARK);
        rect(102, 107, 276, 46, RGB(20, 60, 30));
        txc(112, C_GOLD, "FIM DO PRIMEIRO TEMPO");
        txc(132, W, "X: continuar");
    } else if (phase == 4) {
        rect(90, 100, 300, 60, C_DARK);
        rect(92, 102, 296, 56, RGB(20, 60, 30));
        txc(110, C_GOLD, "FIM DE JOGO");
        txc(130, W, "%s %d x %d %s", TM[ta].name, g0, g1, TM[tb].name);
        txc(146, W, "X: continuar");
    }
}
static int pause_menu(void) {
    rect(120, 90, 240, 80, C_DARK);
    rect(122, 92, 236, 76, RGB(20, 60, 30));
    txc(100, C_GOLD, "PAUSADO");
    txc(124, C_WHITE, "X / START: continuar");
    txc(140, C_WHITE, "O: abandonar partida");
    for (;;) {
        frame();
        if (PRESSED(PSP_CTRL_CROSS) || PRESSED(PSP_CTRL_START)) return 0;
        if (PRESSED(PSP_CTRL_CIRCLE)) return 1;
    }
}

/* starfix: -1 = controla o time; >=0 = controla so aquele jogador (modo estrela)
   s0 > 0 : forca do time do jogador (sobrepoe a do time) */
static MRes play_match(int ta, int tb, int half_secs, int tac, int starfix, int s0) {
    MRes R; int phase = 0, timer = 100, fr = 0, goalt = 0, sc;
    int half = half_secs * 60, total = half * 2;
    memset(&R, 0, sizeof R);
    tstr[0] = (s0 > 0) ? s0 : TM[ta].str; tstr[1] = TM[tb].str + DSTR[S.diff];
    if (tstr[1] < 20) tstr[1] = 20;
    if (tstr[1] > 110) tstr[1] = 110;
    gs = GSPD[S.gspd];
    tactic0 = tac; tactic1 = 1;
    if (tstr[1] > tstr[0] + 8) tactic1 = 2; else if (tstr[1] < tstr[0] - 12) tactic1 = 0;
    ctrlfix = starfix; ctrl = (starfix >= 0) ? starfix : 3;
    memset(pl, 0, sizeof pl);
    map_side(0, ta, starfix);
    map_side(1, tb, -1);
    kickoff(0);
    for (;;) {
        frame();
        if (phase == 1 && PRESSED(PSP_CTRL_START)) { if (pause_menu()) { R.quit = 1; break; } }
        if (phase == 0) {
            if (--timer <= 0) phase = 1;
        } else if (phase == 1) {
            sc = update_play(); fr++;
            if (sc >= 0) {
                if (sc == 0) {
                    R.g0++;
                    if (ctrlfix >= 0 && lastk / 10 == 0) {
                        if (lastk % 10 == ctrlfix) R.sg++;
                        else if (prevk >= 0 && prevk / 10 == 0 && prevk % 10 == ctrlfix) R.sa++;
                    }
                } else R.g1++;
                phase = 2; timer = 130; goalt = sc;
            } else if (fr >= total) phase = 4;
            else if (fr == half) phase = 3;
        } else if (phase == 2) {
            if (--timer <= 0) {
                if (fr >= total) phase = 4;
                else if (fr == half) phase = 3;
                else { kickoff(1 - goalt); phase = 0; timer = 45; }
            }
        } else if (phase == 3) {
            if (PRESSED(PSP_CTRL_CROSS)) { kickoff(1); phase = 0; timer = 60; }
        } else if (phase == 4) {
            if (PRESSED(PSP_CTRL_CROSS)) break;
        }
        draw_match(ta, tb, R.g0, R.g1, fr, total, phase, goalt);
    }
    return R;
}

/* ---------------- Simulacao ---------------- */
static int poisson(float lam) {
    float L = expf(-lam), p = 1.0f; int k = 0;
    do { k++; p *= rf(); } while (p > L && k < 12);
    return k - 1;
}
static void sim_match(float sa, float sb, float fa, float fb, int *ga, int *gb) {
    float la = 1.35f * powf(sa / sb, 1.6f) * fa, lb = 1.35f * powf(sb / sa, 1.6f) * fb;
    if (la > 4.5f) la = 4.5f;
    if (lb > 4.5f) lb = 4.5f;
    *ga = poisson(la); *gb = poisson(lb);
}
static int shootout(int ta, int tb, int *pa, int *pb) {
    float qa = 0.72f + (TM[ta].str - TM[tb].str) * 0.0015f, qb = 0.72f - (TM[ta].str - TM[tb].str) * 0.0015f;
    int a = 0, b = 0, i;
    for (i = 0; i < 5; i++) { if (rf() < qa) a++; if (rf() < qb) b++; }
    while (a == b) { if (rf() < qa) a++; if (rf() < qb) b++; }
    *pa = a; *pb = b;
    return a > b;
}
static void report_screen(int ta, int tb, int ga, int gb) {
    int mins[16], who[16], n = 0, i, j;
    for (i = 0; i < ga && n < 15; i++) { mins[n] = 1 + rand() % 90; who[n++] = 0; }
    for (i = 0; i < gb && n < 15; i++) { mins[n] = 1 + rand() % 90; who[n++] = 1; }
    for (i = 0; i < n; i++) for (j = i + 1; j < n; j++) if (mins[j] < mins[i]) {
        int tm = mins[i]; mins[i] = mins[j]; mins[j] = tm;
        tm = who[i]; who[i] = who[j]; who[j] = tm;
    }
    for (;;) {
        frame();
        if (PRESSED(PSP_CTRL_CROSS) || PRESSED(PSP_CTRL_CIRCLE)) break;
        bg("RELATORIO DA PARTIDA");
        txc(38, C_GOLD, "%s %d x %d %s", TM[ta].name, ga, gb, TM[tb].name);
        for (i = 0; i < n; i++)
            tx(150, 64 + i * 13, C_WHITE, "%2d' Gol do %s", mins[i], TM[who[i] == 0 ? ta : tb].name);
        if (n == 0) txc(100, C_WHITE, "Jogo sem gols");
        tx(8, SH - 12, RGB(170, 200, 170), "X: continuar");
    }
}

/* ---------------- Campeonatos (pontos corridos) ---------------- */
static League LG;
static League *gL;
static int gme;

static void league_init(League *L, int n, int me_id, int dbl) {
    int ids[GSZ], base = (me_id / GSZ) * GSZ, i, j, r, k, tmp;
    memset(L, 0, sizeof *L);
    L->n = n;
    for (i = 0; i < GSZ; i++) ids[i] = base + i;
    for (i = GSZ - 1; i > 0; i--) { j = rand() % (i + 1); tmp = ids[i]; ids[i] = ids[j]; ids[j] = tmp; }
    for (i = 0; i < GSZ; i++) if (ids[i] == me_id) { tmp = ids[0]; ids[0] = ids[i]; ids[i] = tmp; break; }
    for (i = 0; i < n; i++) L->id[i] = ids[i];
    k = rand() % n; tmp = L->id[0]; L->id[0] = L->id[k]; L->id[k] = tmp;
    L->nr = (n - 1) * (dbl ? 2 : 1);
    for (r = 0; r < n - 1; r++) {
        L->fa[r][0] = n - 1; L->fb[r][0] = r % (n - 1);
        for (k = 1; k < n / 2; k++) {
            L->fa[r][k] = (r + k) % (n - 1);
            L->fb[r][k] = (r + n - 1 - k) % (n - 1);
        }
    }
    if (dbl) for (r = 0; r < n - 1; r++) for (k = 0; k < n / 2; k++) {
        L->fa[r + n - 1][k] = L->fb[r][k];
        L->fb[r + n - 1][k] = L->fa[r][k];
    }
}
static int league_me(League *L, int me_id) {
    int i;
    for (i = 0; i < L->n; i++) if (L->id[i] == me_id) return i;
    return 0;
}
static void l_apply(League *L, int a, int b, int ga, int gb) {
    L->pj[a]++; L->pj[b]++;
    L->gf[a] += ga; L->ga[a] += gb; L->gf[b] += gb; L->ga[b] += ga;
    if (ga > gb) { L->w[a]++; L->l[b]++; L->pts[a] += 3; }
    else if (ga < gb) { L->w[b]++; L->l[a]++; L->pts[b] += 3; }
    else { L->d[a]++; L->d[b]++; L->pts[a]++; L->pts[b]++; }
}
static int l_better(League *L, int x, int y) {
    int gx, gy;
    if (L->pts[x] != L->pts[y]) return L->pts[x] > L->pts[y];
    gx = L->gf[x] - L->ga[x]; gy = L->gf[y] - L->ga[y];
    if (gx != gy) return gx > gy;
    return L->gf[x] > L->gf[y];
}
static void l_order(League *L, int *o) {
    int i, j, t;
    for (i = 0; i < L->n; i++) o[i] = i;
    for (i = 0; i < L->n; i++) for (j = i + 1; j < L->n; j++)
        if (l_better(L, o[j], o[i])) { t = o[i]; o[i] = o[j]; o[j] = t; }
}
static void table_screen(League *L, int me, const char *title) {
    int o[16], p;
    l_order(L, o);
    for (;;) {
        frame();
        if (PRESSED(PSP_CTRL_CROSS) || PRESSED(PSP_CTRL_CIRCLE)) break;
        bg(title);
        tx(20, 32, C_GOLD, "%2s %-14s %3s %2s %2s %2s %2s %3s", "#", "TIME", "PT", "J", "V", "E", "D", "SG");
        for (p = 0; p < L->n; p++) {
            int rh = (L->n > 12) ? 12 : 16, i = o[p], y = 46 + p * rh;
            if (i == me) rect(14, y - 2, 330, rh - 2, RGB(40, 100, 50));
            tx(20, y, (i == me) ? C_GOLD : C_WHITE, "%2d %-14s %3d %2d %2d %2d %2d %3d",
               p + 1, TM[L->id[i]].name, L->pts[i], L->pj[i], L->w[i], L->d[i], L->l[i], L->gf[i] - L->ga[i]);
        }
        tx(8, SH - 12, RGB(170, 200, 170), "X: voltar");
    }
}
static void draw_round(void) {
    int r = gL->round, k;
    if (r >= gL->nr) return;
    tx(20, 32, C_GOLD, "RODADA %d/%d", r + 1, gL->nr);
    for (k = 0; k < gL->n / 2; k++) {
        int a = gL->fa[r][k], b = gL->fb[r][k];
        tx(20, 46 + k * 14, (a == gme || b == gme) ? RGB(255, 255, 100) : RGB(200, 220, 200),
           "%-14s x %-14s", TM[gL->id[a]].name, TM[gL->id[b]].name);
    }
}
static void results_screen(League *L, int *ga, int *gb) {
    int k;
    for (;;) {
        frame();
        if (PRESSED(PSP_CTRL_CROSS) || PRESSED(PSP_CTRL_CIRCLE)) break;
        bg("RESULTADOS DA RODADA");
        for (k = 0; k < L->n / 2; k++) {
            int a = L->fa[L->round][k], b = L->fb[L->round][k];
            tx(30, 40 + k * 16, C_WHITE, "%-14s %d x %d %-14s", TM[L->id[a]].name, ga[k], gb[k], TM[L->id[b]].name);
        }
        tx(8, SH - 12, RGB(170, 200, 170), "X: continuar");
    }
}
static int find_my_fixture(League *L, int me) {
    int k, r = L->round;
    for (k = 0; k < L->n / 2; k++) if (L->fa[r][k] == me || L->fb[r][k] == me) return k;
    return 0;
}

/* mode 0 = Municipal (8 times), 1 = Liga da Liberdade (12 times + final) */
static void run_league(int n, int me_id, int mode, const char *title, int dbl) {
    int tac, half, me, o[16], k, c;
    League *L = &LG;
    static const char *it[4] = {"Jogar partida", "Simular partida", "Ver tabela", "Sair da competicao"};
    tac = pick_tactic(); if (tac < 0) return;
    half = pick_duration(); if (half < 0) return;
    league_init(L, n, me_id, dbl);
    me = league_me(L, me_id);
    gL = L; gme = me;
    while (L->round < L->nr) {
        int ga[8], gb[8], mk = find_my_fixture(L, me), a, b, opp, mg, og, y0;
        a = L->fa[L->round][mk]; b = L->fb[L->round][mk]; opp = (a == me) ? b : a;
        y0 = 46 + (n / 2) * 14 + 10;
        menu_extra = draw_round;
        c = menu(title, it, 4, 0, y0, NULL);
        menu_extra = NULL;
        if (c < 0 || c == 3) { if (confirm("SAIR DA COMPETICAO?")) return; else continue; }
        if (c == 2) { table_screen(L, me, title); continue; }
        if (c == 0) {
            MRes R = play_match(L->id[me], L->id[opp], half, tac, -1, 0);
            if (R.quit) continue;
            mg = R.g0; og = R.g1;
        } else {
            sim_match((float)TM[L->id[me]].str, (float)TM[L->id[opp]].str + dadj(), 1, 1, &mg, &og);
            report_screen(L->id[me], L->id[opp], mg, og);
        }
        for (k = 0; k < n / 2; k++) {
            if (k == mk) {
                if (a == me) { ga[k] = mg; gb[k] = og; } else { gb[k] = mg; ga[k] = og; }
            } else {
                sim_match((float)TM[L->id[L->fa[L->round][k]]].str, (float)TM[L->id[L->fb[L->round][k]]].str, 1, 1, &ga[k], &gb[k]);
            }
        }
        for (k = 0; k < n / 2; k++) l_apply(L, L->fa[L->round][k], L->fb[L->round][k], ga[k], gb[k]);
        results_screen(L, ga, gb);
        L->round++;
    }
    /* fim da fase de pontos */
    l_order(L, o);
    {
        int champ = o[0];
        char l1[64], l2[64], l3[64];
        l3[0] = 0;
        if (mode == 1) {
            int x = o[0], y = o[1], fg0, fg1;
            if (x == me || y == me) {
                int opp2 = (x == me) ? y : x;
                MRes R = play_match(L->id[me], L->id[opp2], half, tac, -1, 0);
                fg0 = R.g0; fg1 = R.g1;
                if (R.quit) { fg0 = 0; fg1 = 1; }
                if (fg0 == fg1) {
                    int pa, pb, w = shootout(L->id[me], L->id[opp2], &pa, &pb);
                    snprintf(l3, sizeof l3, "Penaltis %d x %d", pa, pb);
                    champ = w ? me : opp2;
                } else champ = (fg0 > fg1) ? me : opp2;
                snprintf(l1, sizeof l1, "GRANDE FINAL");
                snprintf(l2, sizeof l2, "%s %d x %d %s", TM[L->id[me]].name, fg0, fg1, TM[L->id[opp2]].name);
                end_screen(title, l1, l2, l3);
            } else {
                sim_match((float)TM[L->id[x]].str, (float)TM[L->id[y]].str, 1, 1, &fg0, &fg1);
                if (fg0 == fg1) fg0++;
                champ = (fg0 > fg1) ? x : y;
            }
        }
        snprintf(l1, sizeof l1, "CAMPEAO: %s", TM[L->id[champ]].name);
        snprintf(l2, sizeof l2, "%s", (champ == me) ? "PARABENS! VOCE E O CAMPEAO!" : "Seu time nao levou o titulo.");
        {
            int pp = 0;
            while (o[pp] != me) pp++;
            snprintf(l3, sizeof l3, "Sua posicao final: %d", pp + 1);
        }
        end_screen(title, l1, l2, l3);
        table_screen(L, me, title);
    }
}

/* ---------------- Seja Treinador ---------------- */
static int boost_of(int o) { return 1 + ((o > 60) ? (o - 60) / 6 : 0); }
static int price_of(int o) { return (o - 50) * (o - 50) / 20 + 2; }
static int coach_str(void) {
    int i, v = TM[S.coach_team].str + S.coach_bonus;
    for (i = 0; i < 8; i++) if (S.coach_sq[i][0] > 0) v += boost_of(S.coach_sq[i][0]);
    return v;
}
static void gen_market(void) {
    int i;
    for (i = 0; i < 6; i++) { S.coach_mk[i][0] = 58 + rand() % 37; S.coach_mk[i][1] = rand() % 576; }
}
static void coach_extra(void) {
    int n = 0, i;
    for (i = 0; i < 8; i++) if (S.coach_sq[i][0] > 0) n++;
    tx(20, 32, C_GOLD, "Caixa: %d mi   Reforcos: %d/8   Forca: %d", S.coach_money, n, coach_str());
}
static void coach_sell(void) {
    char b[9][64], nm[24];
    const char *it[9];
    int map[9], n, k, c;
    for (;;) {
        n = 0;
        for (k = 0; k < 8; k++) if (S.coach_sq[k][0] > 0) {
            pname(S.coach_sq[k][1], nm);
            snprintf(b[n], 64, "%-12s %2d  vender por %d mi", nm, S.coach_sq[k][0], price_of(S.coach_sq[k][0]) * 70 / 100);
            it[n] = b[n]; map[n] = k; n++;
        }
        if (n == 0) { end_screen("MERCADO", "Nenhum reforco no elenco", "", ""); return; }
        it[n] = "Voltar";
        menu_extra = coach_extra;
        c = menu("VENDER REFORCO", it, n + 1, 0, 48, NULL);
        menu_extra = NULL;
        if (c < 0 || c == n) return;
        k = map[c];
        S.coach_money += price_of(S.coach_sq[k][0]) * 70 / 100;
        S.coach_sq[k][0] = 0;
        save_game();
    }
}
static void coach_market(void) {
    char b[9][64], nm[24];
    const char *it[9];
    int sel = 0, c, k;
    for (k = 0; k < 9; k++) it[k] = b[k];
    for (;;) {
        for (k = 0; k < 6; k++) {
            int o = S.coach_mk[k][0];
            if (o > 0) {
                pname(S.coach_mk[k][1], nm);
                snprintf(b[k], 64, "%-12s %s %2d  +%d  %d mi", nm, POSA[S.coach_mk[k][1] % 3], o, boost_of(o), price_of(o));
            } else snprintf(b[k], 64, "-- contratado --");
        }
        snprintf(b[6], 64, "Vender um reforco");
        snprintf(b[7], 64, "Novas ofertas (custo 2 mi)");
        snprintf(b[8], 64, "Voltar");
        menu_extra = coach_extra;
        c = menu("MERCADO DE TRANSFERENCIAS", it, 9, sel, 48, "X: comprar/escolher   O: voltar");
        menu_extra = NULL;
        if (c < 0 || c == 8) return;
        sel = c;
        if (c < 6) {
            int o = S.coach_mk[c][0], slot = -1;
            if (o <= 0) continue;
            for (k = 0; k < 8; k++) if (S.coach_sq[k][0] == 0) { slot = k; break; }
            if (slot < 0) { end_screen("MERCADO", "Elenco cheio (8 reforcos)", "Venda alguem antes.", ""); continue; }
            if (S.coach_money < price_of(o)) { end_screen("MERCADO", "Caixa insuficiente", "", ""); continue; }
            S.coach_money -= price_of(o);
            S.coach_sq[slot][0] = o; S.coach_sq[slot][1] = S.coach_mk[c][1];
            S.coach_mk[c][0] = 0;
            save_game();
        } else if (c == 6) {
            coach_sell();
        } else if (c == 7) {
            if (S.coach_money < 2) end_screen("MERCADO", "Caixa insuficiente", "", "");
            else { S.coach_money -= 2; gen_market(); save_game(); }
        }
    }
}
static void coach_round_extra(void) {
    draw_round();
    tx(300, 32, C_GOLD, "Forca: %d", coach_str());
    tx(300, 46, C_WHITE, "Caixa: %d mi", S.coach_money);
}
static void coach_run(void) {
    static const char *FORM[4] = {"4-4-2", "4-3-3", "3-5-2", "5-4-1"};
    static const float FA[4] = {1.00f, 1.12f, 1.04f, 0.88f};
    static const float FD[4] = {1.00f, 1.10f, 1.05f, 0.85f};
    static const float TA_[3] = {0.85f, 1.00f, 1.15f};
    static const float TD_[3] = {0.85f, 1.00f, 1.12f};
    League *L = &S.coach_lg;
    int me = league_me(L, S.coach_team), k, c, o[16], sel = 4;
    char b[7][64];
    const char *it[7];
    gL = L; gme = me;
    for (k = 0; k < 7; k++) it[k] = b[k];
    while (L->round < L->nr) {
        int ga[8], gb[8], mk = find_my_fixture(L, me), a, b2, opp, mg = 0, og = 0;
        a = L->fa[L->round][mk]; b2 = L->fb[L->round][mk]; opp = (a == me) ? b2 : a;
        snprintf(b[0], 64, "Formacao: %s", FORM[S.coach_form]);
        snprintf(b[1], 64, "Tatica: %s", TACN[S.coach_tac]);
        snprintf(b[2], 64, "Treinar elenco (+1 forca) [%d pts]", S.coach_tp);
        snprintf(b[3], 64, "Mercado de transferencias");
        snprintf(b[4], 64, "Jogar rodada");
        snprintf(b[5], 64, "Ver tabela");
        snprintf(b[6], 64, "Salvar e sair");
        menu_extra = coach_round_extra;
        c = menu("SEJA TREINADOR", it, 7, sel, 46 + 4 * 14 + 10, "X: escolher/alterar   O: salvar e sair");
        menu_extra = NULL;
        if (c < 0) c = 6;
        sel = c;
        if (c == 0) { S.coach_form = (S.coach_form + 1) % 4; save_game(); continue; }
        if (c == 1) { S.coach_tac = (S.coach_tac + 1) % 3; save_game(); continue; }
        if (c == 2) { if (S.coach_tp > 0 && S.coach_bonus < 12) { S.coach_tp--; S.coach_bonus++; save_game(); } continue; }
        if (c == 3) { coach_market(); continue; }
        if (c == 5) { table_screen(L, me, "SEJA TREINADOR"); continue; }
        if (c == 6) { save_game(); return; }
        sim_match((float)coach_str(), (float)TM[L->id[opp]].str + dadj(),
                  FA[S.coach_form] * TA_[S.coach_tac], FD[S.coach_form] * TD_[S.coach_tac], &mg, &og);
        report_screen(S.coach_team, L->id[opp], mg, og);
        S.coach_money += 6 + ((mg > og) ? 4 : ((mg == og) ? 1 : 0));
        for (k = 0; k < 4; k++) {
            if (k == mk) {
                if (a == me) { ga[k] = mg; gb[k] = og; } else { gb[k] = mg; ga[k] = og; }
            } else {
                sim_match((float)TM[L->id[L->fa[L->round][k]]].str, (float)TM[L->id[L->fb[L->round][k]]].str, 1, 1, &ga[k], &gb[k]);
            }
        }
        for (k = 0; k < 4; k++) l_apply(L, L->fa[L->round][k], L->fb[L->round][k], ga[k], gb[k]);
        results_screen(L, ga, gb);
        L->round++;
        if (S.coach_tp < 5) S.coach_tp++;
        gen_market();
        save_game();
    }
    l_order(L, o);
    {
        char l1[64], l2[64];
        snprintf(l1, sizeof l1, "CAMPEAO: %s", TM[L->id[o[0]]].name);
        snprintf(l2, sizeof l2, "%s", (o[0] == me) ? "TREINADOR DO ANO! PARABENS!" : "Temporada encerrada.");
        end_screen("SEJA TREINADOR", l1, l2, "");
        table_screen(L, me, "SEJA TREINADOR");
    }
    S.coach_on = 0;
    save_game();
}
static void mode_coach(void) {
    static const char *it[3] = {"Continuar temporada", "Nova temporada", "Voltar"};
    for (;;) {
        int c = menu("SEJA TREINADOR", it, 3, 0, 60, NULL);
        if (c < 0 || c == 2) return;
        if (c == 0) {
            if (!S.coach_on) { end_screen("SEJA TREINADOR", "Nenhuma temporada salva", "Comece uma nova temporada", ""); continue; }
        } else {
            int t = pick_team("SEJA TREINADOR - SEU CLUBE", -1);
            if (t < 0) continue;
            if (S.coach_on && !confirm("SUBSTITUIR TEMPORADA SALVA?")) continue;
            S.coach_on = 1; S.coach_team = t; S.coach_form = 0; S.coach_tac = 1;
            S.coach_tp = 2; S.coach_bonus = 0; S.coach_money = 40;
            memset(S.coach_sq, 0, sizeof S.coach_sq);
            gen_market();
            league_init(&S.coach_lg, 8, t, 0);
            save_game();
        }
        coach_run();
    }
}

static void cup_results(const char *title, int *al, int cnt, int *ga, int *gb, int *pen);
/* ---------------- Torne Uma Estrela ---------------- */
static const char *POSN[3] = {"Atacante", "Meia", "Zagueiro"};
static const int POSI[3] = {4, 3, 1};

/* Teclado na tela: mais facil de digitar o nome */
static int edit_name(char *name, int maxlen, const char *title) {
    static const char *G[4] = {"ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ0123", "456789-.' "};
    static const char *BT[3] = {"APAGAR", "ESPACO", "OK"};
    char buf[20];
    int len, cx = 0, cy = 0, t = 0, r, c, i;
    if (maxlen > 15) maxlen = 15;
    strncpy(buf, name, maxlen); buf[maxlen] = 0;
    len = (int)strlen(buf);
    for (;;) {
        int cols;
        frame(); t++;
        if (PRESSED(PSP_CTRL_UP)) cy = (cy + 4) % 5;
        if (PRESSED(PSP_CTRL_DOWN)) cy = (cy + 1) % 5;
        cols = (cy == 4) ? 3 : 10;
        if (PRESSED(PSP_CTRL_LEFT)) cx = (cx + cols - 1) % cols;
        if (PRESSED(PSP_CTRL_RIGHT)) cx = (cx + 1) % cols;
        if (cx >= cols) cx = cols - 1;
        if (PRESSED(PSP_CTRL_SQUARE) && len > 0) buf[--len] = 0;
        if (PRESSED(PSP_CTRL_TRIANGLE) && len > 0 && len < maxlen) { buf[len++] = ' '; buf[len] = 0; }
        if (PRESSED(PSP_CTRL_CIRCLE)) return 0;
        if (PRESSED(PSP_CTRL_CROSS) || PRESSED(PSP_CTRL_START)) {
            int fin = PRESSED(PSP_CTRL_START) || (cy == 4 && cx == 2);
            if (!fin) {
                if (cy < 4) {
                    char ch = G[cy][cx];
                    if (len < maxlen) {
                        if (isalpha((unsigned char)ch))
                            ch = (len == 0 || buf[len - 1] == ' ' || buf[len - 1] == '.' || buf[len - 1] == '-')
                                 ? (char)toupper((unsigned char)ch) : (char)tolower((unsigned char)ch);
                        buf[len++] = ch; buf[len] = 0;
                    }
                } else if (cx == 0) {
                    if (len > 0) buf[--len] = 0;
                } else if (len > 0 && len < maxlen) { buf[len++] = ' '; buf[len] = 0; }
            } else {
                i = len - 1;
                while (i >= 0 && buf[i] == ' ') buf[i--] = 0;
                if (buf[0]) strcpy(name, buf); else strcpy(name, "Sem Nome");
                return 1;
            }
        }
        bg(title);
        rect(60, 36, 360, 26, C_DARK);
        rect(62, 38, 356, 22, RGB(20, 60, 30));
        tx(70, 45, C_WHITE, "%s", buf);
        if ((t / 20) % 2 == 0) tx(70 + len * 8, 45, C_GOLD, "_");
        for (r = 0; r < 4; r++) for (c = 0; c < 10; c++) {
            int x = 90 + c * 30, y = 72 + r * 26, sel = (r == cy && c == cx);
            rect(x, y, 26, 22, sel ? C_GOLD : RGB(30, 80, 45));
            tx(x + 9, y + 7, sel ? C_DARK : C_WHITE, "%c", G[r][c]);
        }
        for (c = 0; c < 3; c++) {
            int x = 90 + c * 100, sel = (cy == 4 && cx == c);
            rect(x, 176, 94, 22, sel ? C_GOLD : RGB(30, 80, 45));
            tx(x + (94 - (int)strlen(BT[c]) * 8) / 2, 183, sel ? C_DARK : C_WHITE, "%s", BT[c]);
        }
        txc(212, RGB(170, 200, 170), "X: digitar   QUAD: apagar   TRI: espaco");
        txc(226, RGB(170, 200, 170), "START: confirmar   O: cancelar");
    }
}

/* ---------- helpers do modo estrela ---------- */
enum { D_GYM = 1, D_TRAIN = 2, D_PELADA = 4, D_NEWS = 8, D_INT = 16, D_POST = 32, D_POST2 = 64, D_HATE = 128, D_FOOD = 256, D_CHAT = 512 };
static const char *CARN[4] = {"Nenhum", "Popular", "Esportivo", "Luxo"};
static const char *HNAME[6] = {"Cama", "Sofa", "TV", "Geladeira", "Mesa", "Estante trofeus"};
static const int HPRICE[6] = {15, 25, 40, 35, 20, 30};
static const char *HREQ[6] = {"sem requisito", "Fama 10+", "Jogar 3 partidas", "Pedir comida no app", "Relacao 30+ com alguem", "Ganhar 1 premio"};

static void addfame(int d) { S.fame += d; if (S.fame > 100) S.fame = 100; if (S.fame < 0) S.fame = 0; }
static void addmoral(int d) { S.moral += d; if (S.moral > 100) S.moral = 100; if (S.moral < 0) S.moral = 0; }
static void addrel(int i, int d) { S.rel[i] += d; if (S.rel[i] > 100) S.rel[i] = 100; if (S.rel[i] < 0) S.rel[i] = 0; }
static void msg2(const char *a, const char *b) { end_screen("TORNE UMA ESTRELA", a, b, ""); }
static int pay(int mil) {
    if (S.star_money < mil) { msg2("Dinheiro insuficiente", "Jogue partidas para ganhar salario."); return 0; }
    S.star_money -= mil;
    return 1;
}
static void xp_gain(int n) {
    S.star_xp += n;
    while (S.star_xp >= 3) {
        S.star_xp -= 3;
        if (S.star_ovr < 99) S.star_ovr++;
        msg2("Evolucao!", "Seu overall subiu +1");
    }
}
static void next_day(void) { S.day++; S.done = 0; }
static void list_screen(const char *title, const char **ln, int n) {
    int i;
    for (;;) {
        frame();
        if (PRESSED(PSP_CTRL_CROSS) || PRESSED(PSP_CTRL_CIRCLE)) break;
        bg(title);
        for (i = 0; i < n && i < 16; i++) tx(24, 36 + i * 14, C_WHITE, "%s", ln[i]);
        tx(8, SH - 12, RGB(170, 200, 170), "X: continuar");
    }
}

/* ---------- editor de visual (criador, barbearia, loja) ---------- */
static void look_val(int f, int v, char *o) {
    static const char *HCN[6] = {"Preto", "Castanho esc.", "Castanho", "Loiro", "Ruivo", "Grisalho"};
    switch (f) {
        case 0: snprintf(o, 24, "%s", SKINN[v % 5]); break;
        case 1: snprintf(o, 24, "%s", HAIRN[v % 6]); break;
        case 2: snprintf(o, 24, "%s", HCN[v % 6]); break;
        case 3: snprintf(o, 24, "%s", BEARDN[v % 4]); break;
        case 4: snprintf(o, 24, "%s", BUILDN[v % 3]); break;
        case 5: snprintf(o, 24, "%s", PALN[v % 12]); break;
        case 6: case 7: if (v == 0) snprintf(o, 24, "Nenhuma"); else snprintf(o, 24, "%s", PALN[(v - 1) % 12]); break;
        default: snprintf(o, 24, "%s", SOCKN[v % 3]); break;
    }
}
static int look_editor(const char *title, unsigned mask, Look *L, u32 c1, u32 c2) {
    static const char *LAB[9] = {"Pele", "Cabelo", "Cor cabelo", "Barba", "Fisico", "Chuteira", "Fita", "Luva", "Meiao"};
    static const int CNT[9] = {5, 6, 6, 4, 3, 12, 13, 13, 3};
    unsigned char *v[9];
    int sel = 0, i, t = 0;
    v[0] = &L->skin; v[1] = &L->hair; v[2] = &L->hcol; v[3] = &L->beard; v[4] = &L->build;
    v[5] = &L->boots; v[6] = &L->band; v[7] = &L->glove; v[8] = &L->socks;
    while (!(mask & (1u << sel))) sel++;
    for (;;) {
        char val[24];
        frame(); t++;
        if (PRESSED(PSP_CTRL_UP)) { do sel = (sel + 8) % 9; while (!(mask & (1u << sel))); }
        if (PRESSED(PSP_CTRL_DOWN)) { do sel = (sel + 1) % 9; while (!(mask & (1u << sel))); }
        if (PRESSED(PSP_CTRL_LEFT)) *v[sel] = (unsigned char)((*v[sel] + CNT[sel] - 1) % CNT[sel]);
        if (PRESSED(PSP_CTRL_RIGHT)) *v[sel] = (unsigned char)((*v[sel] + 1) % CNT[sel]);
        if (PRESSED(PSP_CTRL_START)) return 1;
        if (PRESSED(PSP_CTRL_CIRCLE)) return 0;
        bg(title);
        rect(16, 34, 176, 224, RGB(30, 110, 45));
        for (i = 0; i < 7; i++) rect(16, 34 + i * 32, 176, 16, RGB(38, 124, 52));
        rect(60, 226, 90, 6, RGB(15, 80, 30));
        draw_sprite(105, 232, L, c1, c2, 0, ((t / 40) % 3) - 1, 0, 4);
        for (i = 0; i < 9; i++) {
            int y = 40 + i * 22, en = (mask & (1u << i)) != 0;
            u32 col = !en ? RGB(100, 110, 100) : ((i == sel) ? C_DARK : C_WHITE);
            look_val(i, *v[i], val);
            if (i == sel) rect(200, y - 4, 270, 18, C_GOLD);
            tx(206, y, col, "%-11s< %s >", LAB[i], val);
        }
        tx(8, SH - 12, RGB(170, 200, 170), "Cima/Baixo: item  Esq/Dir: mudar  START: ok  O: sair");
    }
}
static int create_star(void) {
    char nm[16];
    int pos, nat;
    Look L;
    strcpy(nm, "Craque");
    if (!edit_name(nm, 12, "NOVA CARREIRA - NOME")) return 0;
    pos = menu("ESCOLHA A POSICAO", POSN, 3, 0, 60, NULL);
    if (pos < 0) return 0;
    nat = menu("NACIONALIDADE (SELECAO)", SELN, NSEL, 0, 36, NULL);
    if (nat < 0) return 0;
    memset(&L, 0, sizeof L);
    L.skin = 1; L.hair = 1; L.hcol = 1; L.build = 1; L.boots = 5; L.socks = 1;
    if (!look_editor("CRIE SEU JOGADOR", 0x1FF, &L, PAL[0], PAL[4])) return 0;
    strncpy(S.star_name, nm, 15); S.star_name[15] = 0;
    S.star_pos = pos; S.star_nat = nat; S.slook = L;
    return 1;
}

static int salary_of(int club) { return TM[club].str * TM[club].str / 200; }
static void gen_star_offers(void) {
    int cand[MAXT], n = 0, i, k;
    for (i = 0; i < MAXT; i++) {
        if (i == S.star_team) continue;
        if (TM[i].str <= S.star_ovr + 10 && TM[i].str >= S.star_ovr - 22) cand[n++] = i;
    }
    for (i = 0; i < 6; i++) S.star_off[i] = -1;
    for (i = 0; i < 6 && n > 0; i++) { k = rand() % n; S.star_off[i] = cand[k]; cand[k] = cand[--n]; }
}
static void star_market_extra(void) {
    tx(20, 32, C_GOLD, "Clube atual: %s (Sal %d mil)", TM[S.star_team].name, salary_of(S.star_team));
    tx(20, 44, C_WHITE, "Overall %d   Patrimonio %d mil   %s", S.star_ovr, S.star_money, S.star_moved ? "Janela fechada" : "Janela aberta");
}
static void star_market(void) {
    char b[7][64];
    const char *it[7];
    int sel = 0, c, k, id;
    for (k = 0; k < 7; k++) it[k] = b[k];
    for (;;) {
        for (k = 0; k < 6; k++) {
            id = S.star_off[k];
            if (id >= 0) snprintf(b[k], 64, "%-14s For %2d  Sal %d mil", TM[id].name, TM[id].str, salary_of(id));
            else snprintf(b[k], 64, "-- sem proposta --");
        }
        snprintf(b[6], 64, "Voltar");
        menu_extra = star_market_extra;
        c = menu("MERCADO DE TRANSFERENCIAS", it, 7, sel, 62, "X: aceitar proposta   O: voltar");
        menu_extra = NULL;
        if (c < 0 || c == 6) return;
        sel = c;
        id = S.star_off[c];
        if (id < 0) continue;
        if (S.star_moved) { end_screen("MERCADO", "Voce ja trocou de clube", "nesta temporada.", ""); continue; }
        {
            char q[64], l3[64];
            snprintf(q, sizeof q, "IR PARA O %s?", TM[id].name);
            if (!confirm(q)) continue;
            S.star_team = id; S.star_moved = 1; S.star_money += TM[id].str * 3;
            snprintf(l3, sizeof l3, "Luvas recebidas: %d mil", TM[id].str * 3);
            gen_star_offers();
            save_game();
            end_screen("TRANSFERENCIA", "Contrato assinado!", TM[id].name, l3);
        }
    }
}
static int star_s0(int club) {
    int v = (S.star_ovr + TM[club].str) / 2 + (S.moral - 50) / 8 + S.boots_lv;
    if (v < 30) v = 30;
    if (v > 105) v = 105;
    return v;
}
static int awards_total(void) { int i, n = 0; for (i = 0; i < 8; i++) n += S.aw[i]; return n; }
static int house_req(int i) {
    int k;
    switch (i) {
        case 0: return 1;
        case 1: return S.fame >= 10;
        case 2: return S.totg >= 3;
        case 3: return S.fooded;
        case 4: for (k = 0; k < 5; k++) if (S.rel[k] >= 30) return 1; return 0;
        default: return awards_total() >= 1;
    }
}
static int all_placed(void) { int i; for (i = 0; i < 6; i++) if (S.house[i] != 2) return 0; return 1; }

static void trophies_screen(void) {
    static char b[8][64];
    const char *ln[8];
    static const char *AN[8] = {"Melhor jogador da partida", "Artilheiro", "Lider de assistencias", "Melhor jogador da liga",
                                "Melhor jogador do ano", "Melhor jogador da America", "Bola de Ouro (so na Europa)", "Copas do Mundo"};
    int i;
    for (i = 0; i < 8; i++) { snprintf(b[i], 64, "%-28s x %d", AN[i], S.aw[i]); ln[i] = b[i]; }
    list_screen("SALA DE TROFEUS", ln, 8);
}
static void missions_screen(void) {
    static char b[7][72];
    const char *ln[7];
    int i;
    for (i = 0; i < 6; i++) {
        snprintf(b[i], 72, "[%s] %s: %s (%d mil)", (S.house[i] == 2) ? "OK" : ((S.house[i] == 1) ? "->" : "  "), HNAME[i], HREQ[i], HPRICE[i]);
        ln[i] = b[i];
    }
    snprintf(b[6], 72, "Compre na Loja de Moveis e monte em casa.");
    ln[6] = b[6];
    list_screen("MISSOES DA CASA", ln, 7);
}
static void home_view(void) {
    int i;
    for (;;) {
        frame();
        if (PRESSED(PSP_CTRL_CROSS) || PRESSED(PSP_CTRL_CIRCLE)) break;
        bg("MINHA CASA");
        rect(60, 36, 360, 134, RGB(220, 200, 160));
        rect(60, 170, 360, 80, RGB(140, 100, 60));
        for (i = 0; i < 6; i++) {
            int x, y, w, h;
            u32 col;
            switch (i) {
                case 0: x = 80; y = 120; w = 90; h = 50; col = RGB(70, 100, 200); break;
                case 1: x = 200; y = 130; w = 110; h = 40; col = RGB(160, 60, 60); break;
                case 2: x = 215; y = 62; w = 70; h = 44; col = RGB(20, 20, 30); break;
                case 3: x = 365; y = 60; w = 40; h = 100; col = RGB(235, 235, 240); break;
                case 4: x = 320; y = 200; w = 80; h = 36; col = RGB(120, 80, 40); break;
                default: x = 90; y = 56; w = 90; h = 30; col = C_GOLD; break;
            }
            if (S.house[i] == 2) {
                rect(x, y, w, h, col);
                if (i == 2) rect(x + 4, y + 4, w - 8, h - 8, RGB(60, 120, 200));
                if (i == 0) rect(x + 4, y + 4, 24, 14, RGB(240, 240, 240));
            } else {
                rect(x, y, w, 2, RGB(120, 120, 120)); rect(x, y + h - 2, w, 2, RGB(120, 120, 120));
                rect(x, y, 2, h, RGB(120, 120, 120)); rect(x + w - 2, y, 2, h, RGB(120, 120, 120));
            }
        }
        {
            int n = 0, k;
            for (k = 0; k < 6; k++) if (S.house[k] == 2) n++;
            tx(70, 232, C_WHITE, "Moveis montados: %d/6", n);
        }
        tx(8, SH - 12, RGB(170, 200, 170), "X: voltar");
    }
}
static void star_status(void) {
    for (;;) {
        frame();
        if (PRESSED(PSP_CTRL_CROSS) || PRESSED(PSP_CTRL_CIRCLE)) break;
        bg("STATUS DO JOGADOR");
        rect(330, 36, 140, 150, RGB(30, 110, 45));
        draw_sprite(400, 176, &S.slook, PAL[TM[S.star_team].c1], PAL[TM[S.star_team].c2], 0, 0, 0, 4);
        tx(20, 36, C_GOLD, "%s", S.star_name);
        tx(20, 52, C_WHITE, "%s - %s", POSN[S.star_pos], SELN[S.star_nat % 16]);
        tx(20, 66, C_WHITE, "Idade: %d   Overall: %d", S.star_age, S.star_ovr);
        tx(20, 80, C_WHITE, "Clube: %s", TM[S.star_team].name);
        tx(20, 98, C_WHITE, "Fama: %d   Moral: %d", S.fame, S.moral);
        tx(20, 112, C_WHITE, "Dinheiro: %d mil", S.star_money);
        tx(20, 126, C_WHITE, "Salario: %d mil/jogo", salary_of(S.star_team));
        tx(20, 144, C_WHITE, "Temp %d: %dj %dg %da", S.star_season, S.star_games, S.star_goals, S.star_assists);
        tx(20, 158, C_WHITE, "Carreira: %d gols, %d assist.", S.star_tgoals + S.star_goals, S.star_tassists + S.star_assists);
        tx(20, 172, C_WHITE, "Treino: %d/3   Dia %d", S.star_xp, S.day);
        tx(20, 186, C_WHITE, "Carro: %s   Chuteira nv %d", CARN[S.car], S.boots_lv);
        tx(20, 204, C_WHITE, "Premios: %d   Copas: %d", awards_total(), S.aw[7]);
        tx(8, SH - 12, RGB(170, 200, 170), "X: voltar");
    }
}

/* ---------- premios ---------- */
static void star_awards(int avg) {
    static char b[8][72];
    const char *ln[8];
    int n = 0, g = S.star_goals, a = S.star_assists, grp = S.star_team / GSZ;
    int base = avg + g * 2 + a + (S.star_ovr - 60) / 3;
    int score = avg + g * 2 + a + S.fame / 4 + S.star_ovr / 4 + S.wc_boost;
    if (g >= 5 + rand() % 5) { S.aw[1]++; addfame(5); snprintf(b[n], 72, "ARTILHEIRO da liga (%d gols)", g); ln[n] = b[n]; n++; }
    if (a >= 3 + rand() % 4) { S.aw[2]++; addfame(4); snprintf(b[n], 72, "LIDER DE ASSISTENCIAS (%d)", a); ln[n] = b[n]; n++; }
    if (base >= 85 + rand() % 20) { S.aw[3]++; addfame(6); snprintf(b[n], 72, "MELHOR JOGADOR DA LIGA"); ln[n] = b[n]; n++; }
    if (score >= 118 + rand() % 20) { S.aw[4]++; addfame(10); addmoral(8); snprintf(b[n], 72, "MELHOR JOGADOR DO ANO"); ln[n] = b[n]; n++; }
    if (grp == 1 && score >= 108 + rand() % 15) { S.aw[5]++; addfame(8); snprintf(b[n], 72, "MELHOR JOGADOR DA AMERICA"); ln[n] = b[n]; n++; }
    if (grp == 2 && score >= 135 + rand() % 15) { S.aw[6]++; addfame(25); addmoral(20); snprintf(b[n], 72, "*** BOLA DE OURO! ***"); ln[n] = b[n]; n++; }
    S.wc_boost = 0;
    if (n == 0) { ln[0] = "Nenhum premio desta vez. Continue evoluindo!"; n = 1; }
    list_screen("PREMIOS DA TEMPORADA", ln, n);
}

static void star_season_end(void) {
    int g = (S.star_games > 0) ? S.star_games : 1, avg = S.star_rate / g, inc, best = -1, i;
    char a[64], b[64], c[64];
    inc = (avg >= 75) ? 2 : ((avg >= 62) ? 1 : 0);
    if (S.star_age >= 31) inc = (inc > 0) ? inc - 1 : -1;
    S.star_ovr += inc;
    if (S.star_ovr > 99) S.star_ovr = 99;
    if (S.star_ovr < 40) S.star_ovr = 40;
    snprintf(a, sizeof a, "FIM DA TEMPORADA %d", S.star_season);
    snprintf(b, sizeof b, "Gols %d  Assist. %d  Nota media %d.%d", S.star_goals, S.star_assists, avg / 10, avg % 10);
    snprintf(c, sizeof c, "Overall %s%d  ->  %d", (inc >= 0) ? "+" : "", inc, S.star_ovr);
    end_screen("TORNE UMA ESTRELA", a, b, c);
    star_awards(avg);
    S.star_tgoals += S.star_goals; S.star_tassists += S.star_assists;
    if (S.star_season % 4 == 0) {
        int selk = MAXT + S.star_nat;
        if (S.star_ovr >= TM[selk].str - 6 || S.fame >= 60) {
            S.wc_pending = 1; S.ticket = 0;
            end_screen("COPA DO MUNDO", "CONVOCADO PARA A COPA!", "Compre a passagem na Agencia de Viagem", "e embarque no Aeroporto.");
        } else {
            addmoral(-6);
            end_screen("COPA DO MUNDO", "Voce nao foi convocado.", "Continue evoluindo!", "");
        }
    }
    for (i = 0; i < MAXT; i++) {
        if (i != S.star_team && TM[i].str > TM[S.star_team].str && TM[i].str <= S.star_ovr + 8)
            if (best < 0 || TM[i].str < TM[best].str) best = i;
    }
    if (best >= 0) {
        static const char *it[2] = {"Aceitar proposta", "Continuar no clube atual"};
        snprintf(a, sizeof a, "PROPOSTA: %s", TM[best].name);
        if (menu(a, it, 2, 0, 60, "X: escolher") == 0) { S.star_team = best; S.star_money += TM[best].str * 3; }
    }
    S.star_season++; S.star_age++;
    S.star_games = S.star_goals = S.star_assists = S.star_rate = 0;
    S.star_moved = 0;
    gen_star_offers();
    if (S.star_age >= 38) {
        snprintf(a, sizeof a, "%s se aposentou!", S.star_name);
        snprintf(b, sizeof b, "Gols na carreira: %d", S.star_tgoals);
        end_screen("FIM DE CARREIRA", a, b, "Obrigado por jogar!");
        S.star_started = 0;
    }
    save_game();
}

static void star_play(void) {
    int opp, s0, tries = 0;
    MRes R;
    do { opp = rand() % MAXT; tries++; }
    while ((opp == S.star_team || abs(TM[opp].str - TM[S.star_team].str) > 14) && tries < 60);
    if (opp == S.star_team) opp = (opp + 1) % MAXT;
    s0 = star_s0(S.star_team);
    R = play_match(S.star_team, opp, 90, 1, POSI[S.star_pos], s0);
    if (R.quit) return;
    {
        int win = R.g0 > R.g1, dr = (R.g0 == R.g1), rate, mvp, dm;
        char a[64], b[64], c[64];
        rate = 58 + R.sg * 11 + R.sa * 7 + (win ? 6 : (dr ? 2 : -2)) + (rand() % 11 - 5) + (S.star_ovr - 60) / 8 + (S.moral - 50) / 20;
        if (rate < 30) rate = 30;
        if (rate > 100) rate = 100;
        mvp = (rate >= 85);
        S.star_games++; S.star_goals += R.sg; S.star_assists += R.sa; S.star_rate += rate;
        S.star_money += salary_of(S.star_team);
        S.totg++; S.last_rate = rate;
        addfame(R.sg + R.sa + (win ? 1 : 0) + (mvp ? 2 : 0));
        dm = win ? 4 : (dr ? 0 : -4);
        if (rate < 55) dm -= 2;
        addmoral(dm);
        if (mvp) { S.aw[0]++; addmoral(3); }
        snprintf(a, sizeof a, "%s %d x %d %s", TM[S.star_team].name, R.g0, R.g1, TM[opp].name);
        snprintf(b, sizeof b, "Seus gols: %d   Assistencias: %d", R.sg, R.sa);
        snprintf(c, sizeof c, "Nota: %d.%d%s", rate / 10, rate % 10, mvp ? "  MELHOR EM CAMPO!" : "");
        end_screen("TORNE UMA ESTRELA", a, b, c);
    }
    next_day();
    gen_star_offers();
    if (S.star_games >= 10) star_season_end();
    save_game();
}

/* ---------- Copa do Mundo ---------- */
static void star_worldcup(void) {
    static const char *RN[4] = {"OITAVAS DE FINAL", "QUARTAS DE FINAL", "SEMIFINAL", "GRANDE FINAL"};
    int sel = MAXT + S.star_nat, al[16], n = 16, rnd = 0, i, j, t, gl = 0, as = 0, elim = 0;
    for (i = 0; i < 16; i++) al[i] = MAXT + i;
    for (i = 15; i > 0; i--) { j = rand() % (i + 1); t = al[i]; al[i] = al[j]; al[j] = t; }
    while (n > 1 && !elim) {
        int nx[8], ga[8], gb[8], pen[8], k, mk = -1;
        for (k = 0; k < n / 2; k++) if (al[2 * k] == sel || al[2 * k + 1] == sel) mk = k;
        for (k = 0; k < n / 2; k++) {
            int a = al[2 * k], b = al[2 * k + 1], x, y;
            pen[k] = 0;
            if (k == mk) {
                int opp = (a == sel) ? b : a;
                char l2[64];
                MRes R;
                snprintf(l2, sizeof l2, "%s vs %s", TM[sel].name, TM[opp].name);
                end_screen("COPA DO MUNDO", RN[rnd], l2, "X: jogar");
                R = play_match(sel, opp, 60, 1, POSI[S.star_pos], star_s0(sel));
                if (R.quit) { R.g0 = 0; R.g1 = 1; }
                gl += R.sg; as += R.sa;
                if (a == sel) { x = R.g0; y = R.g1; } else { x = R.g1; y = R.g0; }
            } else {
                sim_match((float)TM[a].str, (float)TM[b].str, 1, 1, &x, &y);
            }
            ga[k] = x; gb[k] = y;
            if (x == y) { int pa, pb, w = shootout(a, b, &pa, &pb); pen[k] = 1; nx[k] = w ? a : b; }
            else nx[k] = (x > y) ? a : b;
        }
        cup_results(RN[rnd], al, n / 2, ga, gb, pen);
        if (mk >= 0 && nx[mk] != sel) { elim = 1; break; }
        for (k = 0; k < n / 2; k++) al[k] = nx[k];
        n /= 2; rnd++;
    }
    {
        int champ = (!elim && n == 1 && al[0] == sel);
        char l1[64], l2[64];
        if (champ) {
            S.aw[7]++; S.wc_boost = 15; addfame(20); addmoral(20);
            snprintf(l1, sizeof l1, "CAMPEAO DO MUNDO!");
        } else {
            addfame(rnd * 3); addmoral(rnd >= 2 ? 5 : -5);
            if (rnd >= 3) S.wc_boost = 8;
            snprintf(l1, sizeof l1, "Fim da Copa: eliminado em %s", RN[rnd > 3 ? 3 : rnd]);
        }
        snprintf(l2, sizeof l2, "Seus gols: %d   Assistencias: %d", gl, as);
        end_screen("COPA DO MUNDO", l1, l2, "");
        if (gl >= 5) { S.aw[1]++; addfame(8); end_screen("COPA DO MUNDO", "CHUTEIRA DE OURO DA COPA!", "Voce foi o artilheiro do torneio.", ""); }
    }
    S.star_tgoals += gl; S.star_tassists += as;
    S.wc_pending = 0; S.ticket = 0;
    S.day += 20; S.done = 0;
}

/* ---------- celular ---------- */
static void phone_extra(void) {
    u32 k = RGB(30, 30, 40);
    rect(14, 28, 452, 6, k); rect(14, 258, 452, 6, k);
    rect(14, 28, 6, 236, k); rect(460, 28, 6, 236, k);
    tx(26, 38, RGB(160, 200, 255), "Sinal ||||   Dia %d   Bateria 87%%", S.day);
}
static int followers(void) { return 500 + S.fame * 1800 + (S.star_tgoals + S.star_goals) * 350; }
static void social_extra(void) {
    phone_extra();
    tx(26, 50, C_WHITE, "Seguidores: %d   Posts: %d", followers(), S.posts);
}
static void app_social(void) {
    static const char *it[5] = {"Postar foto", "Postar sobre o jogo", "Ver comentarios", "Responder haters", "Voltar"};
    static const char *CM[8] = {"Voce e o melhor!", "Mais um golaco, craque!", "Joga muito!", "Overrated...", "Vai pro banco!",
                                "Idolo da torcida!", "Quando vem pro meu time?", "Parabens pelo jogo!"};
    static char b[5][72];
    const char *ln[5];
    int c, sel = 0, i;
    for (;;) {
        menu_extra = social_extra;
        c = menu("REDE SOCIAL", it, 5, sel, 66, "X: escolher   O: voltar");
        menu_extra = NULL;
        if (c < 0 || c == 4) return;
        sel = c;
        if (c == 0) {
            if (S.done & D_POST) { msg2("Voce ja postou hoje.", ""); continue; }
            S.done |= D_POST; S.posts++; addfame(1 + rand() % 2); addmoral(rand() % 3);
            snprintf(b[0], 72, "Sua foto recebeu %d curtidas!", followers() / 10 + rand() % 100);
            msg2(b[0], "Fama aumentou.");
        } else if (c == 1) {
            if (S.last_rate <= 0) { msg2("Jogue uma partida primeiro.", ""); continue; }
            if (S.done & D_POST2) { msg2("Voce ja postou sobre o jogo hoje.", ""); continue; }
            S.done |= D_POST2; S.posts++;
            if (S.last_rate >= 70) { addfame(3); addmoral(2); msg2("Post sobre o jogo viralizou!", "Fama +3  Moral +2"); }
            else { addfame(1); addmoral(-1); msg2("Post morno... alguns criticaram.", "Fama +1  Moral -1"); }
        } else if (c == 2) {
            for (i = 0; i < 5; i++) {
                int k = (S.moral > 55 || S.last_rate >= 70) ? (rand() % 3 + (rand() % 2) * 5) : (rand() % 8);
                snprintf(b[i], 72, "@fa%d: %s", 100 + rand() % 900, CM[k % 8]);
                ln[i] = b[i];
            }
            list_screen("COMENTARIOS", ln, 5);
        } else {
            if (S.done & D_HATE) { msg2("Voce ja respondeu hoje.", ""); continue; }
            S.done |= D_HATE;
            if (rand() % 2) { addfame(2); addmoral(2); msg2("Resposta no estilo craque!", "Fama +2  Moral +2"); }
            else { addmoral(-3); msg2("A discussao deu ruim...", "Moral -3"); }
        }
    }
}

static char gmsg[100];
static void chat_extra(void) {
    phone_extra();
    rect(26, 52, 428, 16, RGB(50, 80, 120));
    tx(32, 56, C_WHITE, "%s", gmsg);
}
static const char *CNAME[5] = {"Mae", "Agente", "Tecnico", "Amigo", "Parceiro(a)"};
static void app_chat(int ct) {
    for (;;) {
        const char *it[5];
        int act[5], n = 0, c;
        if (ct == 0) snprintf(gmsg, sizeof gmsg, "%s", (S.moral < 40) ? "Mae: Voce esta bem? Descanse um pouco." : "Mae: Estou orgulhosa de voce! Beijos.");
        else if (ct == 1) snprintf(gmsg, sizeof gmsg, "Agente: Tenho propostas de clubes. Fama: %d.", S.fame);
        else if (ct == 2) snprintf(gmsg, sizeof gmsg, "%s", (S.last_rate >= 70) ? "Tecnico: Excelente jogo! Continue assim." : ((S.last_rate > 0) ? "Tecnico: Precisamos de mais de voce." : "Tecnico: Vamos comecar a temporada."));
        else if (ct == 3) snprintf(gmsg, sizeof gmsg, "Amigo: Bora uma pelada na quadra?");
        else if (S.pstate == 0) snprintf(gmsg, sizeof gmsg, "%s", (S.fame >= 10) ? "Alex: Oi! Sou seu fa. Podemos conversar?" : "Sem mensagens novas.");
        else if (S.pstate == 1) snprintf(gmsg, sizeof gmsg, "Alex: Oi! Saudades de voce.");
        else if (S.pstate == 2) snprintf(gmsg, sizeof gmsg, "Alex: Te amo! Vamos jantar?");
        else snprintf(gmsg, sizeof gmsg, "Alex: Nossa casa esta linda!");
        if (ct == 4 && S.pstate == 0) { if (S.fame >= 10) { it[n] = "Responder mensagem"; act[n++] = 4; } }
        else { it[n] = "Conversar"; act[n++] = 1; }
        if (ct == 1) { it[n] = "Ver propostas de clubes"; act[n++] = 2; }
        if (ct == 3) { it[n] = "Convidar para pelada"; act[n++] = 3; }
        if (ct == 4 && S.pstate >= 1) { it[n] = "Convidar para sair (3 mil)"; act[n++] = 5; }
        if (ct == 4 && S.pstate == 1) { it[n] = "Pedir em namoro"; act[n++] = 6; }
        if (ct == 4 && S.pstate == 2) { it[n] = "Pedir em casamento (30 mil)"; act[n++] = 7; }
        it[n] = "Voltar"; act[n++] = 8;
        menu_extra = chat_extra;
        c = menu(CNAME[ct], it, n, 0, 76, NULL);
        menu_extra = NULL;
        if (c < 0 || act[c] == 8) return;
        switch (act[c]) {
            case 1: case 3:
                if (S.done & (D_CHAT << ct)) { msg2("Voces ja conversaram hoje.", ""); break; }
                S.done |= (D_CHAT << ct);
                addrel(ct, act[c] == 3 ? 6 : 5);
                addmoral(ct == 0 ? 3 : 2);
                if (ct == 2 && S.last_rate >= 70) addmoral(3);
                msg2("Boa conversa!", "Relacao melhorou.");
                break;
            case 2: star_market(); break;
            case 4: S.pstate = 1; S.rel[4] = 15; msg2("Voces comecaram a conversar!", "Veja em Relacionamentos."); break;
            case 5: if (pay(3)) { addrel(4, 8); addmoral(4); msg2("Saida muito boa!", "Relacao +8  Moral +4"); } break;
            case 6:
                if (S.rel[4] >= 60) { S.pstate = 2; addmoral(8); addfame(1); msg2("Ela(e) disse SIM!", "Voces estao namorando."); }
                else msg2("Relacao ainda baixa.", "Precisa de 60+.");
                break;
            case 7:
                if (S.rel[4] < 90) msg2("Relacao ainda baixa.", "Precisa de 90+.");
                else if (!all_placed()) msg2("Mobilie a casa primeiro.", "Complete as missoes da casa.");
                else if (pay(30)) { S.pstate = 3; addfame(5); addmoral(15); msg2("CASAMENTO!", "Moral +15  Fama +5"); }
                break;
            default: break;
        }
        save_game();
    }
}
static void app_contacts(void) {
    static char b[6][40];
    const char *it[6];
    int c, k, sel = 0;
    for (;;) {
        for (k = 0; k < 5; k++) { snprintf(b[k], 40, "%s  (%d)", CNAME[k], S.rel[k]); it[k] = b[k]; }
        it[5] = "Voltar";
        menu_extra = phone_extra;
        c = menu("CONVERSAS", it, 6, sel, 66, NULL);
        menu_extra = NULL;
        if (c < 0 || c == 5) return;
        sel = c;
        app_chat(c);
    }
}
static void app_rel(void) {
    static const char *ST[4] = {"Solteiro(a)", "Conhecendo", "Namorando", "Casado(a)"};
    int i;
    for (;;) {
        frame();
        if (PRESSED(PSP_CTRL_CROSS) || PRESSED(PSP_CTRL_CIRCLE)) break;
        bg("RELACIONAMENTOS");
        for (i = 0; i < 5; i++) {
            tx(30, 44 + i * 28, C_WHITE, "%s", CNAME[i]);
            rect(170, 44 + i * 28, 204, 10, C_DARK);
            rect(172, 46 + i * 28, S.rel[i] * 2, 6, (S.rel[i] >= 60) ? RGB(230, 90, 120) : RGB(90, 170, 100));
            tx(384, 44 + i * 28, C_GOLD, "%d", S.rel[i]);
        }
        tx(30, 200, C_GOLD, "Status amoroso: %s", ST[S.pstate % 4]);
        tx(30, 216, C_WHITE, "Converse e convide para sair no celular.");
        tx(8, SH - 12, RGB(170, 200, 170), "X: voltar");
    }
}
static void app_food(void) {
    static const char *it[5] = {"Lanche (1 mil)", "Pizza (3 mil)", "Refeicao fitness (4 mil)", "Jantar romantico (10 mil)", "Voltar"};
    int c, sel = 0;
    for (;;) {
        menu_extra = phone_extra;
        c = menu("PEDIR COMIDA", it, 5, sel, 66, "X: pedir   O: voltar");
        menu_extra = NULL;
        if (c < 0 || c == 4) return;
        sel = c;
        if (c == 0 && pay(1)) { addmoral(1); S.fooded = 1; msg2("Lanche entregue!", "Moral +1"); }
        else if (c == 1 && pay(3)) { addmoral(3); S.fooded = 1; msg2("Pizza chegou!", "Moral +3"); }
        else if (c == 2) {
            if (S.done & D_FOOD) { msg2("Voce ja fez sua refeicao fitness hoje.", ""); continue; }
            if (pay(4)) { S.done |= D_FOOD; S.fooded = 1; addmoral(2); xp_gain(1); msg2("Refeicao fitness entregue!", "Moral +2  Treino +1"); }
        } else if (c == 3) {
            if (S.pstate < 1) { msg2("Voce precisa de um(a) parceiro(a).", "Converse no app de conversas."); continue; }
            if (pay(10)) { S.fooded = 1; addrel(4, 10); addmoral(5); msg2("Jantar a dois!", "Relacao +10  Moral +5"); }
        }
        save_game();
    }
}

/* ---------- mundo aberto ---------- */
#define WW 960
#define WH 576
#define NLOC 11
typedef struct { int x, y, w, h; const char *name; u32 col, roof; } Loc;
static const Loc LOC[NLOC] = {
    {40, 100, 100, 96, "CASA", RGB(200, 160, 110), RGB(170, 60, 50)},
    {180, 110, 100, 86, "ACADEMIA", RGB(90, 90, 160), RGB(50, 50, 110)},
    {320, 40, 200, 156, "ESTADIO", RGB(150, 150, 150), RGB(40, 120, 60)},
    {560, 110, 90, 86, "LOJA ESPORTES", RGB(220, 120, 40), RGB(150, 70, 20)},
    {680, 110, 90, 86, "LOJA MOVEIS", RGB(160, 110, 70), RGB(110, 70, 40)},
    {800, 110, 90, 86, "BARBEARIA", RGB(190, 90, 130), RGB(130, 50, 90)},
    {40, 300, 100, 90, "JORNAL", RGB(210, 210, 190), RGB(90, 90, 90)},
    {180, 300, 130, 90, "CONCESSIONARIA", RGB(70, 130, 190), RGB(40, 80, 130)},
    {350, 300, 100, 90, "AGENCIA VIAGEM", RGB(80, 170, 160), RGB(40, 110, 100)},
    {540, 290, 180, 140, "QUADRA LOCAL", RGB(200, 110, 60), RGB(150, 70, 30)},
    {760, 290, 170, 130, "AEROPORTO", RGB(190, 200, 215), RGB(100, 110, 130)}
};
static int wpx = 90, wpy = 214, wfd = 1, wph = 0, wt = 0;

static void app_car(void) {
    static char b[NLOC + 1][40];
    const char *it[NLOC + 1];
    int k, c;
    for (k = 0; k < NLOC; k++) { snprintf(b[k], 40, "%s", LOC[k].name); it[k] = b[k]; }
    it[NLOC] = "Cancelar";
    menu_extra = phone_extra;
    c = menu("CARRO POR APLICATIVO", it, NLOC + 1, 0, 66, "X: chamar (1 mil)   O: voltar");
    menu_extra = NULL;
    if (c < 0 || c == NLOC) return;
    if (!pay(1)) return;
    wpx = LOC[c].x + LOC[c].w / 2; wpy = LOC[c].y + LOC[c].h + 10;
    msg2("Corrida concluida!", LOC[c].name);
}
static void phone_menu(void) {
    static const char *it[7] = {"Rede Social", "Conversas", "Carro (app)", "Comida (app)", "Relacionamentos", "Missoes da casa", "Fechar"};
    int sel = 0;
    for (;;) {
        int c;
        menu_extra = phone_extra;
        c = menu("CELULAR", it, 7, sel, 66, "X: abrir app   O: fechar");
        menu_extra = NULL;
        if (c < 0 || c == 6) return;
        sel = c;
        switch (c) {
            case 0: app_social(); break;
            case 1: app_contacts(); break;
            case 2: app_car(); break;
            case 3: app_food(); break;
            case 4: app_rel(); break;
            default: missions_screen(); break;
        }
    }
}

/* ---------- locais ---------- */
static void loc_home(void) {
    static const char *it[6] = {"Dormir (proximo dia)", "Montar moveis comprados", "Ver a casa", "Missoes da casa", "Sala de trofeus", "Voltar"};
    for (;;) {
        int c = menu("CASA", it, 6, 0, 56, NULL);
        if (c < 0 || c == 5) return;
        if (c == 0) {
            next_day(); addmoral(4);
            if (rand() % 100 < 15) { addrel(0, 4); msg2("A familia ligou!", "Relacao com a familia melhorou."); }
            else msg2("Voce dormiu bem.", "Um novo dia comeca.");
            save_game();
        } else if (c == 1) {
            int i, n = 0;
            for (i = 0; i < 6; i++) if (S.house[i] == 1) { S.house[i] = 2; n++; addfame(1); addmoral(3); }
            if (!n) msg2("Nenhum movel para montar.", "Compre na Loja de Moveis.");
            else msg2("Moveis montados!", "Sua casa esta mais aconchegante.");
            if (all_placed() && !S.home_reward) { S.home_reward = 1; addmoral(10); addfame(5); msg2("CASA TOTALMENTE MOBILIADA!", "Moral e fama aumentaram."); }
            save_game();
        } else if (c == 2) home_view();
        else if (c == 3) missions_screen();
        else trophies_screen();
    }
}
static void loc_gym(void) {
    static const char *it[4] = {"Treino de forca (2 mil)", "Treino de velocidade (2 mil)", "Treino tecnico (2 mil)", "Voltar"};
    for (;;) {
        int c = menu("ACADEMIA", it, 4, 0, 56, "X: treinar   O: voltar");
        if (c < 0 || c == 3) return;
        if (S.done & D_GYM) { msg2("Voce ja treinou hoje.", "Volte amanha."); continue; }
        if (!pay(2)) continue;
        S.done |= D_GYM; addmoral(1);
        if (c == 0) {
            S.gym_n++;
            if (S.gym_n % 8 == 0 && S.slook.build < 2) { S.slook.build++; msg2("Seu fisico evoluiu!", "Voce ficou mais forte."); }
        }
        msg2("Treino concluido!", "Progresso +1");
        xp_gain(1);
        save_game();
    }
}
static void loc_stadium(void) {
    char b0[48];
    const char *it[3];
    for (;;) {
        int c;
        snprintf(b0, 48, "Jogar proxima partida (%d/10)", S.star_games + 1);
        it[0] = b0; it[1] = "Treino coletivo (gratis)"; it[2] = "Voltar";
        c = menu("ESTADIO", it, 3, 0, 56, NULL);
        if (c < 0 || c == 2) return;
        if (c == 0) { star_play(); if (!S.star_started) return; }
        else if (S.done & D_TRAIN) msg2("Voce ja treinou com o time hoje.", "");
        else { S.done |= D_TRAIN; addmoral(1); msg2("Treino coletivo concluido!", "Progresso +1"); xp_gain(1); save_game(); }
    }
}
static void loc_sports(void) {
    static const int BP[3] = {10, 30, 70};
    char b0[64];
    const char *it[3];
    for (;;) {
        int c;
        if (S.boots_lv < 3) snprintf(b0, 64, "Chuteira nivel %d (+%d chute) - %d mil", S.boots_lv + 1, S.boots_lv + 1, BP[S.boots_lv]);
        else snprintf(b0, 64, "Chuteira no nivel maximo");
        it[0] = b0; it[1] = "Personalizar chuteira/especiais (2 mil)"; it[2] = "Voltar";
        c = menu("LOJA DE ESPORTES", it, 3, 0, 56, NULL);
        if (c < 0 || c == 2) return;
        if (c == 0) {
            if (S.boots_lv >= 3) continue;
            if (pay(BP[S.boots_lv])) { S.boots_lv++; msg2("Chuteira nova!", "Seu desempenho melhorou."); save_game(); }
        } else {
            Look tmp = S.slook;
            if (S.star_money < 2) { msg2("Dinheiro insuficiente", ""); continue; }
            if (look_editor("LOJA - ESPECIAIS", 0x1E0, &tmp, PAL[TM[S.star_team].c1], PAL[TM[S.star_team].c2]) && pay(2)) { S.slook = tmp; save_game(); }
        }
    }
}
static void loc_furn(void) {
    static char b[7][72];
    const char *it[7];
    int k, c;
    for (k = 0; k < 7; k++) it[k] = b[k];
    for (;;) {
        for (k = 0; k < 6; k++) {
            if (S.house[k] == 2) snprintf(b[k], 72, "%-16s [montado]", HNAME[k]);
            else if (S.house[k] == 1) snprintf(b[k], 72, "%-16s [comprado]", HNAME[k]);
            else if (house_req(k)) snprintf(b[k], 72, "%-16s %d mil", HNAME[k], HPRICE[k]);
            else snprintf(b[k], 72, "%-16s BLOQ: %s", HNAME[k], HREQ[k]);
        }
        snprintf(b[6], 72, "Voltar");
        c = menu("LOJA DE MOVEIS", it, 7, 0, 56, "X: comprar   O: voltar");
        if (c < 0 || c == 6) return;
        if (S.house[c]) { msg2("Voce ja comprou este item.", ""); continue; }
        if (!house_req(c)) { msg2("Missao bloqueada:", HREQ[c]); continue; }
        if (!pay(HPRICE[c])) continue;
        S.house[c] = 1;
        msg2("Item comprado!", "Va para casa e monte o movel.");
        save_game();
    }
}
static void loc_barber(void) {
    static const char *it[2] = {"Mudar visual: cabelo e barba (3 mil)", "Voltar"};
    for (;;) {
        int c = menu("BARBEARIA", it, 2, 0, 56, NULL);
        if (c != 0) return;
        if (S.star_money < 3) { msg2("Dinheiro insuficiente", ""); continue; }
        {
            Look tmp = S.slook;
            if (look_editor("BARBEARIA", 0xE, &tmp, PAL[TM[S.star_team].c1], PAL[TM[S.star_team].c2]) && pay(3)) {
                S.slook = tmp; addmoral(2); save_game();
            }
        }
    }
}
static void loc_news(void) {
    static const char *it[3] = {"Ler manchetes", "Dar entrevista", "Voltar"};
    static char b[5][72];
    for (;;) {
        int c = menu("JORNAL", it, 3, 0, 56, NULL);
        if (c < 0 || c == 2) return;
        if (c == 0) {
            const char *ln[5];
            int n = 0, x, y, ga, gb;
            x = rand() % MAXT;
            do { y = rand() % MAXT; } while (y == x);
            sim_match((float)TM[x].str, (float)TM[y].str, 1, 1, &ga, &gb);
            snprintf(b[n], 72, "%s tem %d gols e %d assist. na temporada", S.star_name, S.star_goals, S.star_assists); ln[n] = b[n]; n++;
            snprintf(b[n], 72, "Fama de %s: %d pontos. Moral: %d", S.star_name, S.fame, S.moral); ln[n] = b[n]; n++;
            snprintf(b[n], 72, "RODADA: %s %d x %d %s", TM[x].name, ga, gb, TM[y].name); ln[n] = b[n]; n++;
            if (awards_total() > 0) snprintf(b[n], 72, "%s ja soma %d premios na carreira", S.star_name, awards_total());
            else snprintf(b[n], 72, "Mercado: %s procura reforcos", TM[rand() % MAXT].name);
            ln[n] = b[n]; n++;
            if (S.wc_pending) { snprintf(b[n], 72, "Selecao: %s convocado para a Copa!", S.star_name); ln[n] = b[n]; n++; }
            list_screen("JORNAL DO DIA", ln, n);
        } else {
            static const char *iv[4] = {"Humilde: elogiar o time", "Confiante: falar de titulos", "Polemico: criticar rivais", "Voltar"};
            int k;
            if (S.done & D_INT) { msg2("Voce ja deu entrevista hoje.", ""); continue; }
            k = menu("ENTREVISTA", iv, 4, 0, 56, NULL);
            if (k < 0 || k == 3) continue;
            S.done |= D_INT;
            if (k == 0) { addmoral(3); addfame(1); addrel(2, 4); msg2("O publico gostou da humildade!", "Moral +3  Fama +1"); }
            else if (k == 1) { addfame(2); addmoral(1); msg2("Voce animou a torcida!", "Fama +2"); }
            else if (rand() % 100 < 35) { addfame(-3); addmoral(-4); msg2("A fala virou polemica!", "Fama -3  Moral -4"); }
            else { addfame(4); addmoral(-1); msg2("Falou o que pensa e viralizou!", "Fama +4"); }
        }
    }
}
static void loc_dealer(void) {
    static const int CP[3] = {40, 150, 400};
    static char b[4][64];
    const char *it[4];
    int k, c;
    for (k = 0; k < 4; k++) it[k] = b[k];
    for (;;) {
        for (k = 0; k < 3; k++) {
            if (S.car >= k + 1) snprintf(b[k], 64, "%-10s [seu carro / melhor]", CARN[k + 1]);
            else snprintf(b[k], 64, "%-10s %d mil (mais rapido, fama+)", CARN[k + 1], CP[k]);
        }
        snprintf(b[3], 64, "Voltar");
        c = menu("CONCESSIONARIA", it, 4, 0, 56, "X: comprar   O: voltar");
        if (c < 0 || c == 3) return;
        if (S.car >= c + 1) { msg2("Voce ja tem um carro igual ou melhor.", ""); continue; }
        if (!pay(CP[c])) continue;
        S.car = c + 1; addfame(2 * (c + 1)); addmoral(5);
        msg2("Carro novo na garagem!", "Agora voce anda mais rapido.");
        save_game();
    }
}
static void loc_agency(void) {
    static char b[4][64];
    const char *it[4];
    int k;
    for (k = 0; k < 4; k++) it[k] = b[k];
    for (;;) {
        int c;
        snprintf(b[0], 64, "%s", S.wc_pending ? (S.ticket ? "Passagem da Copa: comprada" : "Comprar passagem da Copa (5 mil)") : "Passagem da Copa (sem convocacao)");
        snprintf(b[1], 64, "Ferias (20 mil): moral +15, 3 dias");
        snprintf(b[2], 64, "Viagem a dois (30 mil): relacao +15");
        snprintf(b[3], 64, "Voltar");
        c = menu("AGENCIA DE VIAGEM", it, 4, 0, 56, NULL);
        if (c < 0 || c == 3) return;
        if (c == 0) {
            if (!S.wc_pending) msg2("Voce nao foi convocado.", "");
            else if (S.ticket) msg2("Voce ja tem a passagem.", "Va ao Aeroporto.");
            else if (pay(5)) { S.ticket = 1; msg2("Passagem comprada!", "Embarque no Aeroporto."); }
        } else if (c == 1) {
            if (pay(20)) { addmoral(15); S.day += 3; S.done = 0; msg2("Ferias incriveis!", "Moral +15"); }
        } else {
            if (S.pstate < 1) msg2("Voce precisa de um(a) parceiro(a).", "Converse no celular.");
            else if (pay(30)) { addrel(4, 15); addmoral(10); S.day += 2; S.done = 0; msg2("Viagem romantica!", "Relacao +15  Moral +10"); }
        }
        save_game();
    }
}
static void loc_court(void) {
    static const char *it[2] = {"Pelada com a galera (partida rapida)", "Voltar"};
    for (;;) {
        int c = menu("QUADRA LOCAL", it, 2, 0, 56, NULL);
        if (c != 0) return;
        if (S.done & D_PELADA) { msg2("Voce ja jogou pelada hoje.", ""); continue; }
        S.done |= D_PELADA;
        {
            int opp = rand() % MAXT;
            MRes R = play_match(S.star_team, opp, 25, 1, POSI[S.star_pos], star_s0(S.star_team) - 3);
            if (R.quit) { S.done &= ~D_PELADA; continue; }
            if (R.g0 > R.g1) { addfame(1); addmoral(3); xp_gain(1); msg2("Vitoria na pelada!", "Fama +1  Moral +3"); }
            else if (R.g0 == R.g1) { addmoral(1); msg2("Empate na pelada.", "Moral +1"); }
            else { addmoral(-1); msg2("Perdeu a pelada...", "Moral -1"); }
        }
        save_game();
    }
}
static void loc_airport(void) {
    static const char *it[2] = {"Embarcar para a Copa do Mundo", "Voltar"};
    int c = menu("AEROPORTO", it, 2, 0, 56, NULL);
    if (c != 0) return;
    if (!S.wc_pending) { msg2("Nenhum voo disponivel.", "Voce nao foi convocado."); return; }
    if (!S.ticket) { msg2("Voce precisa da passagem.", "Compre na Agencia de Viagem."); return; }
    star_worldcup();
    save_game();
}
static void interact(int i) {
    switch (i) {
        case 0: loc_home(); break;
        case 1: loc_gym(); break;
        case 2: loc_stadium(); break;
        case 3: loc_sports(); break;
        case 4: loc_furn(); break;
        case 5: loc_barber(); break;
        case 6: loc_news(); break;
        case 7: loc_dealer(); break;
        case 8: loc_agency(); break;
        case 9: loc_court(); break;
        default: loc_airport(); break;
    }
}

/* ---------- desenho do mundo ---------- */
static void txs(int x, int y, u32 c, const char *s) {
    int n = (int)strlen(s);
    if (x < 0 || y < 0 || x + n * 8 > SW || y + 8 > SH) return;
    tx(x, y, c, "%s", s);
}
static int blocked(int x, int y) {
    int i;
    if (x < 10 || x > WW - 10 || y < 20 || y > WH - 8) return 1;
    for (i = 0; i < NLOC; i++) {
        const Loc *l = &LOC[i];
        if (x + 4 > l->x && x - 4 < l->x + l->w && y + 1 > l->y && y - 3 < l->y + l->h) return 1;
    }
    return 0;
}
static int door_near(int x, int y) {
    int i;
    for (i = 0; i < NLOC; i++) {
        int dx = LOC[i].x + LOC[i].w / 2, dy = LOC[i].y + LOC[i].h;
        if (abs(x - dx) <= 18 && y >= dy && y <= dy + 26) return i;
    }
    return -1;
}
static void draw_building(int i, int cx, int cy) {
    const Loc *l = &LOC[i];
    int x = l->x - cx, y = l->y - cy, k, j;
    rect(x, y, l->w, l->h, l->col);
    rect(x, y, l->w, 12, l->roof);
    rect(x, y + l->h - 3, l->w, 3, RGB(60, 60, 60));
    if (i == 2) {
        rect(x + 20, y + 30, l->w - 40, l->h - 60, RGB(40, 150, 60));
        rect(x + 20, y + 30, l->w - 40, 2, C_WHITE); rect(x + 20, y + l->h - 32, l->w - 40, 2, C_WHITE);
        rect(x + l->w / 2, y + 30, 2, l->h - 60, C_WHITE);
    } else if (i == 9) {
        rect(x + 8, y + 20, l->w - 16, l->h - 40, RGB(220, 140, 70));
        rect(x + 8, y + 20, l->w - 16, 2, C_WHITE); rect(x + 8, y + l->h - 22, l->w - 16, 2, C_WHITE);
        rect(x + l->w / 2, y + 20, 2, l->h - 40, C_WHITE);
        circle(x + l->w / 2, y + l->h / 2, 18, C_WHITE);
    } else if (i == 10) {
        rect(x + 10, y + 24, l->w - 20, 14, RGB(70, 70, 80));
        rect(x + 40, y + 60, 90, 8, C_WHITE); rect(x + 70, y + 52, 8, 24, C_WHITE);
        rect(x + 120, y + 56, 6, 16, C_WHITE);
    } else {
        for (j = 0; j < 2; j++) for (k = 0; k < (l->w - 20) / 24; k++)
            rect(x + 10 + k * 24, y + 22 + j * 22, 14, 12, RGB(170, 215, 240));
    }
    rect(x + l->w / 2 - 8, y + l->h - 18, 16, 15, RGB(90, 55, 25));
    rect(x + l->w / 2 - 6, y + l->h - 16, 12, 13, RGB(130, 85, 45));
    txs(x + (l->w - (int)strlen(l->name) * 8) / 2, y - 11, C_WHITE, l->name);
}
static void world_draw(void) {
    int camx = wpx - SW / 2, camy = wpy - SH / 2, i, k, tx_, ty_;
    if (camx < 0) camx = 0;
    if (camx > WW - SW) camx = WW - SW;
    if (camy < 0) camy = 0;
    if (camy > WH - SH) camy = WH - SH;
    rect(0, 0, SW, SH, RGB(52, 142, 60));
    for (ty_ = camy / 16; ty_ <= (camy + SH) / 16; ty_++)
        for (tx_ = camx / 16; tx_ <= (camx + SW) / 16; tx_++)
            if (((tx_ * 7 + ty_ * 13) % 5) == 0) rect(tx_ * 16 - camx + 4, ty_ * 16 - camy + 5, 4, 2, RGB(44, 128, 52));
    rect(-camx, 224 - camy, WW, 48, RGB(170, 170, 160));
    rect(-camx, 232 - camy, WW, 32, RGB(70, 70, 75));
    for (k = 0; k < WW; k += 40) rect(k - camx, 247 - camy, 20, 2, RGB(230, 230, 120));
    rect(472 - camx, 264 - camy, 48, WH - 264, RGB(170, 170, 160));
    rect(480 - camx, 264 - camy, 32, WH - 264, RGB(70, 70, 75));
    for (k = 272; k < WH; k += 40) rect(495 - camx, k - camy, 2, 20, RGB(230, 230, 120));
    for (i = 0; i < NLOC; i++) if (LOC[i].y + LOC[i].h <= wpy) draw_building(i, camx, camy);
    {
        int sx = wpx - camx, sy = wpy - camy;
        if (S.car > 0) {
            u32 cc = (S.car == 1) ? RGB(200, 60, 60) : ((S.car == 2) ? RGB(240, 200, 30) : RGB(30, 30, 40));
            rect(sx - 13, sy - 7, 26, 10, cc); rect(sx - 8, sy - 11, 16, 5, RGB(160, 210, 240));
            rect(sx - 11, sy + 1, 6, 4, C_DARK); rect(sx + 5, sy + 1, 6, 4, C_DARK);
        }
        draw_sprite(sx, sy, &S.slook, PAL[TM[S.star_team].c1], PAL[TM[S.star_team].c2], 0, wfd, wph, 1);
    }
    for (i = 0; i < NLOC; i++) if (LOC[i].y + LOC[i].h > wpy) draw_building(i, camx, camy);
    /* HUD */
    rect(0, 0, SW, 20, RGB(0, 20, 8));
    rect(0, 20, SW, 2, C_GOLD);
    tx(6, 6, C_WHITE, "Dia %d  $%d mil  Fama %d  Moral %d  Ovr %d", S.day, S.star_money, S.fame, S.moral, S.star_ovr);
    i = door_near(wpx, wpy);
    rect(0, SH - 18, SW, 18, RGB(0, 20, 8));
    if (i >= 0) tx(8, SH - 13, C_GOLD, "X: entrar - %s", LOC[i].name);
    else tx(8, SH - 13, RGB(170, 200, 170), "TRI: celular   START: menu   R: correr");
}
static void star_world(void) {
    while (S.star_started) {
        float ax, ay, spd;
        int sx, sy, di;
        frame(); wt++;
        ax = (pad.Lx - 128) / 128.0f; ay = (pad.Ly - 128) / 128.0f;
        if (fabsf(ax) < 0.25f) ax = 0;
        if (fabsf(ay) < 0.25f) ay = 0;
        if (HELD(PSP_CTRL_LEFT)) ax = -1;
        if (HELD(PSP_CTRL_RIGHT)) ax = 1;
        if (HELD(PSP_CTRL_UP)) ay = -1;
        if (HELD(PSP_CTRL_DOWN)) ay = 1;
        spd = 1.8f + ((S.car == 1) ? 1.0f : ((S.car == 2) ? 1.8f : ((S.car == 3) ? 2.2f : 0.0f)));
        if (HELD(PSP_CTRL_RTRIGGER)) spd *= 1.4f;
        sx = (int)floorf(ax * spd + 0.5f); sy = (int)floorf(ay * spd + 0.5f);
        if (sx != 0 && !blocked(wpx + sx, wpy)) wpx += sx;
        if (sy != 0 && !blocked(wpx, wpy + sy)) wpy += sy;
        if (ax > 0.3f) wfd = 1; else if (ax < -0.3f) wfd = -1;
        wph = (sx != 0 || sy != 0) ? 1 + ((wt / 6) % 2) : 0;
        di = door_near(wpx, wpy);
        if (di >= 0 && PRESSED(PSP_CTRL_CROSS)) { interact(di); continue; }
        if (PRESSED(PSP_CTRL_TRIANGLE)) { phone_menu(); continue; }
        if (PRESSED(PSP_CTRL_START)) {
            static const char *it[6] = {"Status do jogador", "Trofeus", "Missoes da casa", "Celular", "Salvar e sair", "Voltar"};
            int c = menu("MENU", it, 6, 0, 50, NULL);
            if (c == 0) star_status();
            else if (c == 1) trophies_screen();
            else if (c == 2) missions_screen();
            else if (c == 3) phone_menu();
            else if (c == 4) { save_game(); return; }
            continue;
        }
        world_draw();
    }
}

static void mode_star(void) {
    static const char *it[3] = {"Continuar carreira", "Nova carreira", "Voltar"};
    for (;;) {
        int c = menu("TORNE UMA ESTRELA", it, 3, 0, 60, NULL);
        if (c < 0 || c == 2) return;
        if (c == 0) {
            if (!S.star_started) { end_screen("TORNE UMA ESTRELA", "Nenhuma carreira salva", "Comece uma nova carreira", ""); continue; }
            star_world();
        } else {
            int ids[MAXT], i, j, t;
            if (S.star_started && !confirm("SUBSTITUIR CARREIRA SALVA?")) continue;
            if (!create_star()) continue;
            for (i = 0; i < MAXT; i++) ids[i] = i;
            for (i = 0; i < MAXT; i++) for (j = i + 1; j < MAXT; j++)
                if (TM[ids[j]].str < TM[ids[i]].str) { t = ids[i]; ids[i] = ids[j]; ids[j] = t; }
            S.star_started = 1;
            S.star_age = 17; S.star_ovr = 58 + rand() % 8;
            S.star_team = ids[rand() % 5];
            S.star_season = 1; S.star_games = S.star_goals = S.star_assists = S.star_rate = 0;
            S.star_xp = S.star_trained = S.star_tgoals = S.star_tassists = 0;
            S.star_money = 20; S.star_moved = 0;
            S.fame = 5; S.moral = 60; S.day = 1; S.done = 0;
            memset(S.house, 0, sizeof S.house);
            S.boots_lv = 0; S.car = 0;
            S.rel[0] = 50; S.rel[1] = 30; S.rel[2] = 30; S.rel[3] = 30; S.rel[4] = 0;
            S.pstate = 0;
            memset(S.aw, 0, sizeof S.aw);
            S.fooded = 0; S.totg = 0; S.ticket = 0; S.wc_pending = 0; S.wc_boost = 0;
            S.gym_n = 0; S.posts = 0; S.last_rate = 0; S.home_reward = 0;
            wpx = 90; wpy = 214;
            gen_star_offers();
            save_game();
            star_world();
        }
    }
}

/* ---------------- Modo Campanha ---------------- */
static int camp_opp(int stage) {
    int ids[MAXT], n = 0, i, j, t;
    for (i = 0; i < MAXT; i++) if (i != S.camp_team) ids[n++] = i;
    for (i = 0; i < n; i++) for (j = i + 1; j < n; j++)
        if (TM[ids[j]].str < TM[ids[i]].str) { t = ids[i]; ids[i] = ids[j]; ids[j] = t; }
    return ids[stage * (n - 1) / 9];
}
static void camp_loop(void) {
    char b[2][64];
    const char *it[3];
    int sel = 0;
    it[0] = b[0]; it[1] = b[1]; it[2] = "Voltar";
    for (;;) {
        int opp, c;
        if (S.camp_stage >= 10) {
            end_screen("MODO CAMPANHA", "CAMPANHA CONCLUIDA!", "Voce venceu as 10 fases!", "Inicie uma nova campanha.");
            return;
        }
        opp = camp_opp(S.camp_stage);
        snprintf(b[0], 64, "Jogar Fase %d/10: vs %s (%d)", S.camp_stage + 1, TM[opp].name, TM[opp].str);
        snprintf(b[1], 64, "Tatica: %s", TACN[S.camp_tac]);
        c = menu("MODO CAMPANHA", it, 3, sel, 60, NULL);
        if (c < 0 || c == 2) return;
        sel = c;
        if (c == 1) { S.camp_tac = (S.camp_tac + 1) % 3; save_game(); continue; }
        {
            MRes R = play_match(S.camp_team, opp, 90, S.camp_tac, -1, 0);
            int won; char a[64], l2[64], l3[64];
            if (R.quit) continue;
            won = R.g0 > R.g1;
            snprintf(a, sizeof a, "%s %d x %d %s", TM[S.camp_team].name, R.g0, R.g1, TM[opp].name);
            l3[0] = 0;
            if (R.g0 == R.g1) {
                int pa, pb;
                won = shootout(S.camp_team, opp, &pa, &pb);
                snprintf(l3, sizeof l3, "Penaltis: %d x %d", pa, pb);
            }
            if (won) { snprintf(l2, sizeof l2, "VITORIA! Fase %d concluida", S.camp_stage + 1); S.camp_stage++; }
            else snprintf(l2, sizeof l2, "Derrota. Tente novamente!");
            save_game();
            end_screen("MODO CAMPANHA", a, l2, l3);
        }
    }
}
static void mode_campanha(void) {
    static const char *it[3] = {"Continuar campanha", "Nova campanha", "Voltar"};
    for (;;) {
        int c = menu("MODO CAMPANHA", it, 3, 0, 60, NULL);
        if (c < 0 || c == 2) return;
        if (c == 1) {
            int t = pick_team("CAMPANHA - ESCOLHA SEU TIME", -1);
            if (t < 0) continue;
            S.camp_team = t; S.camp_stage = 0; S.camp_tac = 1;
            save_game();
        } else if (S.camp_team < 0) {
            end_screen("MODO CAMPANHA", "Nenhuma campanha iniciada", "Escolha: Nova campanha", "");
            continue;
        }
        camp_loop();
    }
}

/* ---------------- Editar ---------------- */
static void edit_team(int id) {
    int sel = 0, i;
    for (;;) {
        frame();
        if (PRESSED(PSP_CTRL_UP)) sel = (sel + 3) % 4;
        if (PRESSED(PSP_CTRL_DOWN)) sel = (sel + 1) % 4;
        if (sel == 1) {
            if (PRESSED(PSP_CTRL_LEFT) && TM[id].str > 30) TM[id].str--;
            if (PRESSED(PSP_CTRL_RIGHT) && TM[id].str < 99) TM[id].str++;
            if (HELD(PSP_CTRL_LTRIGGER) && TM[id].str > 30) TM[id].str--;
            if (HELD(PSP_CTRL_RTRIGGER) && TM[id].str < 99) TM[id].str++;
        }
        if (sel == 2) {
            if (PRESSED(PSP_CTRL_LEFT)) TM[id].c1 = (TM[id].c1 + 11) % 12;
            if (PRESSED(PSP_CTRL_RIGHT)) TM[id].c1 = (TM[id].c1 + 1) % 12;
        }
        if (sel == 3) {
            if (PRESSED(PSP_CTRL_LEFT)) TM[id].c2 = (TM[id].c2 + 11) % 12;
            if (PRESSED(PSP_CTRL_RIGHT)) TM[id].c2 = (TM[id].c2 + 1) % 12;
        }
        if (PRESSED(PSP_CTRL_CROSS) && sel == 0) edit_name(TM[id].name, 14, "EDITAR NOME DO TIME");
        if (PRESSED(PSP_CTRL_CIRCLE)) { save_game(); return; }
        bg("EDITAR TIME");
        for (i = 0; i < 4; i++) {
            int y = 50 + i * 24;
            u32 col = (i == sel) ? C_GOLD : C_WHITE;
            if (i == sel) rect(30, y - 4, 300, 18, RGB(40, 100, 50));
            if (i == 0) tx(40, y, col, "Nome: %s", TM[id].name);
            if (i == 1) tx(40, y, col, "Forca: < %d >", TM[id].str);
            if (i == 2) tx(40, y, col, "Cor 1: < %s >", PALN[TM[id].c1]);
            if (i == 3) tx(40, y, col, "Cor 2: < %s >", PALN[TM[id].c2]);
        }
        /* camisa */
        rect(370, 60, 70, 70, C_DARK);
        rect(374, 64, 62, 62, PAL[TM[id].c1]);
        rect(390, 80, 30, 30, PAL[TM[id].c2]);
        rect(350, 66, 22, 26, PAL[TM[id].c1]); rect(438, 66, 22, 26, PAL[TM[id].c1]);
        tx(8, SH - 24, RGB(170, 200, 170), "Cima/Baixo: item   Esq/Dir ou L/R: alterar");
        tx(8, SH - 12, RGB(170, 200, 170), "X: editar nome   O: salvar e voltar");
    }
}
static void plist_line(int uid, char *out, int n) {
    snprintf(out, n, "%-13s %s %2d %-10s", S.plr[uid].name, POSA[S.plr[uid].pos % 4], S.plr[uid].ovr, SELN[S.plr[uid].nat % 16]);
}
static int pick_player(const char *title, int team) {
    static char b[12][56];
    const char *it[12];
    int ids[12], n, k, c;
    n = roster_of(team, ids);
    if (n <= 0) return -1;
    for (k = 0; k < n; k++) { plist_line(ids[k], b[k], 56); it[k] = b[k]; }
    c = menu(title, it, n, 0, 40, "X: escolher   O: voltar");
    return (c < 0) ? -1 : ids[c];
}
static void recalc_all(void) {
    int i;
    for (i = 0; i < MAXT; i++) recalc_team(i);
    for (i = 0; i < NSEL; i++) recalc_sel(i);
}
static void edit_player(int uid) {
    Plyr *p = &S.plr[uid];
    int sel = 0, i;
    for (;;) {
        frame();
        if (PRESSED(PSP_CTRL_UP)) sel = (sel + 3) % 4;
        if (PRESSED(PSP_CTRL_DOWN)) sel = (sel + 1) % 4;
        if (sel == 1) {
            if (PRESSED(PSP_CTRL_LEFT) && p->ovr > 30) p->ovr--;
            if (PRESSED(PSP_CTRL_RIGHT) && p->ovr < 99) p->ovr++;
            if (PRESSED(PSP_CTRL_LTRIGGER) && p->ovr > 35) p->ovr -= 5;
            if (PRESSED(PSP_CTRL_RTRIGGER) && p->ovr < 94) p->ovr += 5;
        }
        if (sel == 2) {
            if (PRESSED(PSP_CTRL_LEFT)) p->pos = (unsigned char)((p->pos + 3) % 4);
            if (PRESSED(PSP_CTRL_RIGHT)) p->pos = (unsigned char)((p->pos + 1) % 4);
        }
        if (sel == 3) {
            if (PRESSED(PSP_CTRL_LEFT)) p->nat = (unsigned char)((p->nat + 15) % 16);
            if (PRESSED(PSP_CTRL_RIGHT)) p->nat = (unsigned char)((p->nat + 1) % 16);
        }
        if (PRESSED(PSP_CTRL_CROSS) && sel == 0) edit_name(p->name, 14, "NOME DO JOGADOR");
        if (PRESSED(PSP_CTRL_CIRCLE)) break;
        bg("EDITAR JOGADOR");
        for (i = 0; i < 4; i++) {
            int y = 50 + i * 24;
            u32 col = (i == sel) ? C_GOLD : C_WHITE;
            if (i == sel) rect(30, y - 4, 320, 18, RGB(40, 100, 50));
            if (i == 0) tx(40, y, col, "Nome: %s", p->name);
            if (i == 1) tx(40, y, col, "Overall: < %d >", p->ovr);
            if (i == 2) tx(40, y, col, "Posicao: < %s >", POSA[p->pos % 4]);
            if (i == 3) tx(40, y, col, "Pais: < %s >", SELN[p->nat % 16]);
        }
        {
            Look L;
            look_of(uid, &L);
            rect(370, 40, 90, 110, RGB(30, 110, 45));
            draw_sprite(415, 140, &L, PAL[TM[p->team < MAXT ? p->team : 0].c1], PAL[TM[p->team < MAXT ? p->team : 0].c2], p->pos == 3, 0, 0, 3);
        }
        tx(8, SH - 24, RGB(170, 200, 170), "Esq/Dir: mudar (L/R: +-5)   X: editar nome");
        tx(8, SH - 12, RGB(170, 200, 170), "O: salvar e voltar");
    }
    if (p->team < MAXT) recalc_team(p->team);
    recalc_all();
    save_game();
}
static void transfer_players(void) {
    int t1, t2, p1, p2;
    char a[64], b[64];
    t1 = pick_team("TRANSFERIR - TIME DE ORIGEM", -1);
    if (t1 < 0) return;
    if (t1 >= MAXT) { end_screen("TRANSFERIR", "Selecoes usam a lista de convocados.", "", ""); return; }
    p1 = pick_player("JOGADOR QUE SAI (X)", t1);
    if (p1 < 0) return;
    t2 = pick_team("TRANSFERIR - TIME DE DESTINO", t1);
    if (t2 < 0) return;
    if (t2 >= MAXT) { end_screen("TRANSFERIR", "Selecoes usam a lista de convocados.", "", ""); return; }
    p2 = pick_player("JOGADOR EM TROCA (X)", t2);
    if (p2 < 0) return;
    S.plr[p1].team = (unsigned char)t2;
    S.plr[p2].team = (unsigned char)t1;
    recalc_team(t1); recalc_team(t2); recalc_all();
    snprintf(a, sizeof a, "%s -> %s", S.plr[p1].name, TM[t2].name);
    snprintf(b, sizeof b, "%s -> %s", S.plr[p2].name, TM[t1].name);
    save_game();
    end_screen("TRANSFERENCIA REGISTRADA", a, b, "");
}
static void edit_conv(void) {
    static char b[34][56];
    const char *it[34];
    int k, slot, cand[64], n, i, j, c, in;
    k = menu("CONVOCADOS - SELECAO", SELN, NSEL, 0, 36, NULL);
    if (k < 0) return;
    for (;;) {
        int uid = pick_player("CONVOCADOS (X: trocar)", MAXT + k);
        if (uid < 0) return;
        for (slot = 0; slot < 8; slot++) if (S.conv[k][slot] == uid) break;
        if (slot >= 8) continue;
        n = 0;
        for (i = 0; i < NPL && n < 64; i++) if (S.plr[i].nat == k) {
            in = 0;
            for (j = 0; j < 8; j++) if (S.conv[k][j] == i) in = 1;
            if (!in) cand[n++] = i;
        }
        for (i = 0; i < n; i++) for (j = i + 1; j < n; j++)
            if (S.plr[cand[j]].ovr > S.plr[cand[i]].ovr) { c = cand[i]; cand[i] = cand[j]; cand[j] = c; }
        if (n == 0) { end_screen("CONVOCADOS", "Nenhum jogador disponivel", "desta nacionalidade.", ""); continue; }
        if (n > 33) n = 33;
        for (i = 0; i < n; i++) {
            snprintf(b[i], 56, "%-13s %s %2d %s", S.plr[cand[i]].name, POSA[S.plr[cand[i]].pos % 4], S.plr[cand[i]].ovr, TM[S.plr[cand[i]].team].name);
            it[i] = b[i];
        }
        c = menu("SUBSTITUTO (X: escolher)", it, n, 0, 36, NULL);
        if (c >= 0) { S.conv[k][slot] = cand[c]; recalc_sel(k); save_game(); }
    }
}
static void mode_edit(void) {
    static const char *it[7] = {"Editar time", "Editar jogadores", "Registrar/transferir jogadores", "Convocados (selecoes)",
                                "Restaurar times padrao", "Salvar alteracoes", "Voltar"};
    for (;;) {
        int c = menu("EDITAR", it, 7, 0, 50, NULL);
        if (c < 0 || c == 6) { save_game(); return; }
        if (c == 0) {
            int t = pick_team("EDITAR - ESCOLHA O TIME", -1);
            if (t >= 0) edit_team(t);
        } else if (c == 1) {
            int t = pick_team("EDITAR JOGADORES - TIME", -1);
            while (t >= 0) {
                int u = pick_player("ESCOLHA O JOGADOR (X)", t);
                if (u < 0) break;
                edit_player(u);
            }
        } else if (c == 2) transfer_players();
        else if (c == 3) edit_conv();
        else if (c == 4) {
            if (confirm("RESTAURAR TIMES PADRAO?")) { reset_teams(); save_game(); }
        } else {
            save_game();
            end_screen("EDITAR", "Alteracoes salvas!", "", "");
        }
    }
}

/* ---------------- Amistoso ---------------- */
static void mode_amistoso(void) {
    int a, b, tac, half;
    a = pick_team("AMISTOSO - SEU TIME", -1); if (a < 0) return;
    b = pick_team("AMISTOSO - ADVERSARIO", a); if (b < 0) return;
    half = pick_duration(); if (half < 0) return;
    tac = pick_tactic(); if (tac < 0) return;
    for (;;) {
        static const char *it[2] = {"Jogar novamente", "Sair"};
        MRes R = play_match(a, b, half, tac, -1, 0);
        (void)R;
        if (menu("AMISTOSO", it, 2, 0, 60, NULL) != 0) return;
    }
}

/* ---------------- Opcoes ---------------- */
static void mode_options(void) {
    char b[2][48];
    const char *it[3];
    int sel = 0, c;
    it[0] = b[0]; it[1] = b[1]; it[2] = "Voltar";
    for (;;) {
        snprintf(b[0], 48, "Dificuldade: %s", DIFN[S.diff]);
        snprintf(b[1], 48, "Velocidade do jogo: %s", SPDN[S.gspd]);
        c = menu("OPCOES", it, 3, sel, 60, "X: alterar   O: voltar");
        if (c < 0 || c == 2) { save_game(); return; }
        sel = c;
        if (c == 0) S.diff = (S.diff + 1) % 4;
        else S.gspd = (S.gspd + 1) % 3;
    }
}

/* ---------------- Copa Mundial (mata-mata) ---------------- */
static void cup_results(const char *title, int *al, int cnt, int *ga, int *gb, int *pen) {
    int k;
    for (;;) {
        frame();
        if (PRESSED(PSP_CTRL_CROSS) || PRESSED(PSP_CTRL_CIRCLE)) break;
        bg(title);
        for (k = 0; k < cnt; k++) {
            int a = al[2 * k], b = al[2 * k + 1];
            tx(20, 40 + k * 16, C_WHITE, "%-14s %d x %d %-14s%s", TM[a].name, ga[k], gb[k], TM[b].name, pen[k] ? " (pen)" : "");
        }
        tx(8, SH - 12, RGB(170, 200, 170), "X: continuar");
    }
}
static void run_cup(void) {
    static const char *RN[4] = {"OITAVAS DE FINAL", "QUARTAS DE FINAL", "SEMIFINAL", "GRANDE FINAL"};
    int me_id, tac, half, al[16], ids[MAXT], n = 16, rnd = 0, i, j, t;
    me_id = pick_team("COPA MUNDIAL - SEU TIME", -1); if (me_id < 0) return;
    tac = pick_tactic(); if (tac < 0) return;
    half = pick_duration(); if (half < 0) return;
    for (i = 0; i < MAXT; i++) ids[i] = i;
    for (i = MAXT - 1; i > 0; i--) { j = rand() % (i + 1); t = ids[i]; ids[i] = ids[j]; ids[j] = t; }
    for (i = 0; i < MAXT; i++) if (ids[i] == me_id) { t = ids[0]; ids[0] = ids[i]; ids[i] = t; break; }
    for (i = 0; i < 16; i++) al[i] = ids[i];
    for (i = 15; i > 0; i--) { j = rand() % (i + 1); t = al[i]; al[i] = al[j]; al[j] = t; }
    while (n > 1) {
        int nx[8], ga[8], gb[8], pen[8], k, mk = -1;
        for (k = 0; k < n / 2; k++) if (al[2 * k] == me_id || al[2 * k + 1] == me_id) mk = k;
        for (k = 0; k < n / 2; k++) {
            int a = al[2 * k], b = al[2 * k + 1], x, y;
            pen[k] = 0;
            if (k == mk) {
                int opp = (a == me_id) ? b : a, mg = 0, og = 0;
                for (;;) {
                    char b0[64]; const char *itl[2]; int c;
                    snprintf(b0, sizeof b0, "Jogar contra %s (%d)", TM[opp].name, TM[opp].str);
                    itl[0] = b0; itl[1] = "Simular partida";
                    c = menu(RN[rnd], itl, 2, 0, 60, NULL);
                    if (c < 0) { if (confirm("SAIR DA COPA?")) return; else continue; }
                    if (c == 0) {
                        MRes R = play_match(me_id, opp, half, tac, -1, 0);
                        if (R.quit) continue;
                        mg = R.g0; og = R.g1;
                    } else {
                        sim_match((float)TM[me_id].str, (float)TM[opp].str + dadj(), 1, 1, &mg, &og);
                        report_screen(me_id, opp, mg, og);
                    }
                    break;
                }
                if (a == me_id) { x = mg; y = og; } else { x = og; y = mg; }
            } else {
                sim_match((float)TM[a].str, (float)TM[b].str, 1, 1, &x, &y);
            }
            ga[k] = x; gb[k] = y;
            if (x == y) { int pa, pb, w = shootout(a, b, &pa, &pb); pen[k] = 1; nx[k] = w ? a : b; }
            else nx[k] = (x > y) ? a : b;
        }
        cup_results(RN[rnd], al, n / 2, ga, gb, pen);
        if (mk >= 0 && nx[mk] != me_id) {
            end_screen("COPA MUNDIAL", "ELIMINADO!", RN[rnd], "Seu time caiu na copa.");
            return;
        }
        for (k = 0; k < n / 2; k++) al[k] = nx[k];
        n /= 2; rnd++;
    }
    {
        char l1[64];
        snprintf(l1, sizeof l1, "CAMPEAO: %s", TM[al[0]].name);
        end_screen("COPA MUNDIAL", l1, "PARABENS! VOCE LEVOU A COPA!", "");
    }
}

/* ---------------- Mais ligas ---------------- */
static void mode_more_leagues(void) {
    static const char *it[5] = {
        "Liga Brasileira (16 times)", "Liga Europeia (12, ida e volta)",
        "Liga das Estrelas (8, ida/volta + final)", "Copa Mundial (mata-mata)", "Voltar"
    };
    for (;;) {
        int c = menu("MAIS LIGAS", it, 5, 0, 60, NULL), t;
        if (c < 0 || c == 4) return;
        if (c == 3) { run_cup(); continue; }
        t = pick_team_in("ESCOLHA SEU TIME", c + 1, -1);
        if (t < 0) continue;
        if (c == 0) run_league(16, t, 0, "LIGA BRASILEIRA", 0);
        else if (c == 1) run_league(12, t, 0, "LIGA EUROPEIA", 1);
        else run_league(8, t, 1, "LIGA DAS ESTRELAS", 1);
    }
}

/* ---------------- Titulo e menu principal ---------------- */
static void title_screen(void) {
    int t = 0, k;
    for (;;) {
        frame();
        if (PRESSED(PSP_CTRL_START) || PRESSED(PSP_CTRL_CROSS)) return;
        t++;
        rect(0, 0, SW, SH, RGB(15, 80, 30));
        for (k = 0; k < 12; k++) rect(k * 40, 0, 40, SH, (k & 1) ? RGB(34, 139, 52) : RGB(44, 156, 62));
        rect(20, 20, SW - 40, 2, C_WHITE); rect(20, SH - 22, SW - 40, 2, C_WHITE);
        rect(20, 20, 2, SH - 40, C_WHITE); rect(SW - 22, 20, 2, SH - 40, C_WHITE);
        rect(239, 20, 2, SH - 40, C_WHITE);
        circle(240, 136, 50, C_WHITE);
        rect(90, 80, 300, 60, C_DARK);
        rect(94, 84, 292, 52, RGB(20, 60, 30));
        tx(138 + 2, 96 + 2, C_DARK, "F U T E B O L   G S");
        tx(138, 96, C_GOLD, "F U T E B O L   G S");
        txc(116, C_WHITE, "Futebol para PSP");
        { int by_ = 190 + (int)(sinf(t * 0.1f) * 8);
          rect(228, by_ - 2, 24, 24, C_DARK); rect(230, by_, 20, 20, C_WHITE); rect(236, by_ + 6, 8, 8, C_DARK); }
        if ((t / 30) % 2 == 0) txc(240, C_WHITE, "PRESSIONE START");
    }
}

int main(void) {
    static const char *items[10] = {
        "Amistoso", "Campeonato Municipal", "Liga Da Liberdade", "Seja Treinador",
        "Torne Uma Estrela", "Modo Campanha", "Editar", "Mais Ligas", "Opcoes", "Sair"
    };
    int sel = 0;
    setup_callbacks();
    scePowerSetClockFrequency(333, 333, 166);
    pspDebugScreenInit();
    pspDebugScreenEnableBackColor(0);
    sceDisplaySetMode(0, SW, SH);
    sceDisplaySetFrameBuf((void *)0x04000000, STRIDE, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_NEXTFRAME);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    srand(sceKernelGetSystemTimeLow());
    load_game();
    title_screen();
    for (;;) {
        int c = menu("MENU PRINCIPAL", items, 10, sel, 40, "Cima/Baixo + X");
        if (c < 0) continue;
        sel = c;
        switch (c) {
            case 0: mode_amistoso(); break;
            case 1: { int t = pick_team("CAMPEONATO MUNICIPAL - SEU TIME", -1);
                      if (t >= 0) run_league(8, t, 0, "CAMPEONATO MUNICIPAL", 0); } break;
            case 2: { int t = pick_team("LIGA DA LIBERDADE - SEU TIME", -1);
                      if (t >= 0) run_league(12, t, 1, "LIGA DA LIBERDADE", 0); } break;
            case 3: mode_coach(); break;
            case 4: mode_star(); break;
            case 5: mode_campanha(); break;
            case 6: mode_edit(); break;
            case 7: mode_more_leagues(); break;
            case 8: mode_options(); break;
            case 9: sceKernelExitGame(); break;
        }
    }
    return 0;
}
