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

PSP_MODULE_INFO("FUTEBOL GS", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);

#define SW 480
#define SH 272
#define STRIDE 512
#define MAXT 16
#define NP 5
#define MAGIC 0x46475332u
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
typedef struct {
    unsigned int magic;
    Team t[MAXT];
    int camp_team, camp_stage, camp_tac;
    int star_started; char star_name[16];
    int star_pos, star_age, star_ovr, star_team, star_season, star_games;
    int star_goals, star_assists, star_rate, star_xp, star_trained;
    int star_tgoals, star_tassists;
} Save;
static Save S;
#define TM (S.t)

static const char *DEFN[MAXT] = {
    "Municipal FC", "Liberdade EC", "Atletico GS", "Uniao Esporte",
    "Estrela do Norte", "Gremio Vila", "Real Bairro", "Sporting GS",
    "Ferroviario", "Aguia Dourada", "Tigres FC", "Leoes da Serra",
    "Vitoria SC", "Cruzeiro Sul", "Dragoes AC", "Raio FC"
};
static const int DEFS[MAXT] = {78, 82, 74, 70, 66, 62, 76, 72, 58, 80, 68, 64, 71, 60, 85, 56};

static const char *TACN[3] = {"Defensivo", "Equilibrado", "Ofensivo"};
static const int DUR[4] = {60, 120, 180, 300};

static void reset_teams(void) {
    int i;
    for (i = 0; i < MAXT; i++) {
        memset(&TM[i], 0, sizeof(Team));
        strncpy(TM[i].name, DEFN[i], 15);
        TM[i].str = DEFS[i];
        TM[i].c1 = i % 12;
        TM[i].c2 = (i * 5 + 3) % 12;
        if (TM[i].c2 == TM[i].c1) TM[i].c2 = (TM[i].c2 + 1) % 12;
    }
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
    }
}

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

static int pick_team(const char *title, int exclude) {
    int sel = (exclude == 0) ? 1 : 0, vis = 11, top, k, i, y;
    for (;;) {
        frame();
        if (PRESSED(PSP_CTRL_UP)) sel = (sel + MAXT - 1) % MAXT;
        if (PRESSED(PSP_CTRL_DOWN)) sel = (sel + 1) % MAXT;
        if (PRESSED(PSP_CTRL_LEFT)) sel = (sel + MAXT - 5) % MAXT;
        if (PRESSED(PSP_CTRL_RIGHT)) sel = (sel + 5) % MAXT;
        if (PRESSED(PSP_CTRL_CROSS) && sel != exclude) return sel;
        if (PRESSED(PSP_CTRL_CIRCLE)) return -1;
        bg(title);
        top = sel - vis / 2; if (top < 0) top = 0; if (top > MAXT - vis) top = MAXT - vis;
        for (k = 0; k < vis; k++) {
            i = top + k; y = 34 + k * 19;
            if (i == sel) rect(20, y - 3, SW - 40, 17, RGB(40, 100, 50));
            rect(28, y, 10, 10, PAL[TM[i].c1]);
            rect(40, y, 10, 10, PAL[TM[i].c2]);
            tx(58, y + 1, (i == exclude) ? RGB(120, 120, 120) : C_WHITE, "%s", TM[i].name);
            tx(244, y + 1, C_GOLD, "%2d", TM[i].str);
            rect(270, y + 2, TM[i].str * 18 / 10, 7, (i == sel) ? C_GOLD : RGB(90, 160, 100));
        }
        tx(8, SH - 12, RGB(170, 200, 170), "X: escolher  O: voltar  Cima/Baixo");
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
            kick(0, i, dx, dy, 7.4f);
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
        p->x += p->vx; p->y += p->vy;
        if (p->x < FX0 + 4) p->x = FX0 + 4;
        if (p->x > FX1 - 4) p->x = FX1 - 4;
        if (p->y < FY0 + 4) p->y = FY0 + 4;
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
        bx += bvx; by += bvy;
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
    int x = (int)p->x, y = (int)p->y;
    u32 c1 = PAL[TM[teamid].c1], c2 = PAL[TM[teamid].c2];
    if (i == 0) { c1 = RGB(250, 230, 40); c2 = PAL[TM[teamid].c1]; }
    rect(x - 5, y - 5, 11, 11, C_DARK);
    rect(x - 4, y - 4, 9, 9, c1);
    rect(x - 1, y - 1, 3, 3, c2);
    if (iscontrolled) {
        rect(x - 3, y - 12, 7, 2, C_WHITE);
        rect(x - 2, y - 10, 5, 2, C_WHITE);
        rect(x - 1, y - 8, 3, 2, C_WHITE);
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
    for (t = 0; t < 2; t++) for (i = 0; i < NP; i++)
        draw_player(t, i, (t == 0) ? ta : tb, (t == 0 && i == ctrl));
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
        rect(60, 100, 360, 70, C_DARK);
        rect(62, 102, 356, 66, RGB(20, 60, 30));
        txc(108, C_GOLD, "PREPARAR!");
        txc(128, W, "X: passe/desarme   QUAD: chute");
        txc(142, W, "TRI: lancamento    R: correr");
        txc(156, W, "START: pausa");
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
    tstr[0] = (s0 > 0) ? s0 : TM[ta].str; tstr[1] = TM[tb].str;
    tactic0 = tac; tactic1 = 1;
    if (tstr[1] > tstr[0] + 8) tactic1 = 2; else if (tstr[1] < tstr[0] - 12) tactic1 = 0;
    ctrlfix = starfix; ctrl = (starfix >= 0) ? starfix : 3;
    memset(pl, 0, sizeof pl);
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
typedef struct {
    int n, round, nr;
    int id[12];
    int pts[12], pj[12], w[12], d[12], l[12], gf[12], ga[12];
    int fa[11][6], fb[11][6];
} League;
static League LG;
static League *gL;
static int gme;

static void league_init(League *L, int n, int me_id) {
    int ids[MAXT], i, j, r, k, tmp;
    memset(L, 0, sizeof *L);
    L->n = n;
    for (i = 0; i < MAXT; i++) ids[i] = i;
    for (i = MAXT - 1; i > 0; i--) { j = rand() % (i + 1); tmp = ids[i]; ids[i] = ids[j]; ids[j] = tmp; }
    for (i = 0; i < MAXT; i++) if (ids[i] == me_id) { tmp = ids[0]; ids[0] = ids[i]; ids[i] = tmp; break; }
    for (i = 0; i < n; i++) L->id[i] = ids[i];
    k = rand() % n; tmp = L->id[0]; L->id[0] = L->id[k]; L->id[k] = tmp;
    L->nr = n - 1;
    for (r = 0; r < L->nr; r++) {
        L->fa[r][0] = n - 1; L->fb[r][0] = r % (n - 1);
        for (k = 1; k < n / 2; k++) {
            L->fa[r][k] = (r + k) % (n - 1);
            L->fb[r][k] = (r + n - 1 - k) % (n - 1);
        }
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
    int o[12], p;
    l_order(L, o);
    for (;;) {
        frame();
        if (PRESSED(PSP_CTRL_CROSS) || PRESSED(PSP_CTRL_CIRCLE)) break;
        bg(title);
        tx(20, 32, C_GOLD, "%2s %-14s %3s %2s %2s %2s %2s %3s", "#", "TIME", "PT", "J", "V", "E", "D", "SG");
        for (p = 0; p < L->n; p++) {
            int i = o[p], y = 46 + p * 16;
            if (i == me) rect(14, y - 2, 330, 14, RGB(40, 100, 50));
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
static void run_league(int n, int me_id, int mode, const char *title) {
    int tac, half, me, o[12], k, c;
    League *L = &LG;
    static const char *it[4] = {"Jogar partida", "Simular partida", "Ver tabela", "Sair da competicao"};
    tac = pick_tactic(); if (tac < 0) return;
    half = pick_duration(); if (half < 0) return;
    league_init(L, n, me_id);
    me = league_me(L, me_id);
    gL = L; gme = me;
    while (L->round < L->nr) {
        int ga[6], gb[6], mk = find_my_fixture(L, me), a, b, opp, mg, og, y0;
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
            sim_match((float)TM[L->id[me]].str, (float)TM[L->id[opp]].str, 1, 1, &mg, &og);
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
static void mode_coach(void) {
    static const char *FORM[4] = {"4-4-2", "4-3-3", "3-5-2", "5-4-1"};
    static const float FA[4] = {1.00f, 1.12f, 1.04f, 0.88f};
    static const float FD[4] = {1.00f, 1.10f, 1.05f, 0.85f};
    static const float TA_[3] = {0.85f, 1.00f, 1.15f};
    static const float TD_[3] = {0.85f, 1.00f, 1.12f};
    int me_id, me, form = 0, tac = 1, tp = 2, bonus = 0, sel = 3, o[12], k, c, y0;
    char b[6][56];
    const char *it[6];
    League *L = &LG;
    me_id = pick_team("SEJA TREINADOR - SEU CLUBE", -1);
    if (me_id < 0) return;
    league_init(L, 8, me_id);
    me = league_me(L, me_id);
    gL = L; gme = me;
    for (k = 0; k < 6; k++) it[k] = b[k];
    while (L->round < L->nr) {
        int ga[6], gb[6], mk = find_my_fixture(L, me), a, b2, opp, mg = 0, og = 0;
        a = L->fa[L->round][mk]; b2 = L->fb[L->round][mk]; opp = (a == me) ? b2 : a;
        snprintf(b[0], 56, "Formacao: %s", FORM[form]);
        snprintf(b[1], 56, "Tatica: %s", TACN[tac]);
        snprintf(b[2], 56, "Treinar elenco (+1 forca) [%d pts]  Forca: %d", tp, TM[me_id].str + bonus);
        snprintf(b[3], 56, "Jogar rodada");
        snprintf(b[4], 56, "Ver tabela");
        snprintf(b[5], 56, "Sair do modo treinador");
        y0 = 46 + 4 * 14 + 10;
        menu_extra = draw_round;
        c = menu("SEJA TREINADOR", it, 6, sel, y0, "X: escolher/alterar   O: sair");
        menu_extra = NULL;
        if (c < 0) c = 5;
        sel = c;
        if (c == 0) { form = (form + 1) % 4; continue; }
        if (c == 1) { tac = (tac + 1) % 3; continue; }
        if (c == 2) { if (tp > 0 && bonus < 12) { tp--; bonus++; } continue; }
        if (c == 4) { table_screen(L, me, "SEJA TREINADOR"); continue; }
        if (c == 5) { if (confirm("SAIR DO MODO?")) return; else continue; }
        {
            float sm = (float)(TM[me_id].str + bonus), so = (float)TM[L->id[opp]].str;
            sim_match(sm, so, FA[form] * TA_[tac], FD[form] * TD_[tac], &mg, &og);
            report_screen(me_id, L->id[opp], mg, og);
        }
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
        if (tp < 5) tp++;
    }
    l_order(L, o);
    {
        char l1[64], l2[64];
        snprintf(l1, sizeof l1, "CAMPEAO: %s", TM[L->id[o[0]]].name);
        snprintf(l2, sizeof l2, "%s", (o[0] == me) ? "TREINADOR DO ANO! PARABENS!" : "Temporada encerrada.");
        end_screen("SEJA TREINADOR", l1, l2, "");
        table_screen(L, me, "SEJA TREINADOR");
    }
}

/* ---------------- Torne Uma Estrela ---------------- */
static const char *POSN[3] = {"Atacante", "Meia", "Zagueiro"};
static const int POSI[3] = {4, 3, 1};

static int edit_name(char *name, int maxlen, const char *title) {
    static const char *cs = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    int len = (int)strlen(cs), pos = 0, i;
    char buf[17];
    memset(buf, ' ', 16); buf[16] = 0;
    for (i = 0; i < maxlen && name[i]; i++) buf[i] = name[i];
    buf[maxlen] = 0;
    for (;;) {
        char *f;
        frame();
        f = strchr(cs, buf[pos]); if (!f) f = (char *)cs;
        i = (int)(f - cs);
        if (PRESSED(PSP_CTRL_UP)) buf[pos] = cs[(i + 1) % len];
        if (PRESSED(PSP_CTRL_DOWN)) buf[pos] = cs[(i + len - 1) % len];
        if (PRESSED(PSP_CTRL_LEFT) && pos > 0) pos--;
        if (PRESSED(PSP_CTRL_RIGHT) && pos < maxlen - 1) pos++;
        if (PRESSED(PSP_CTRL_SQUARE)) buf[pos] = ' ';
        if (PRESSED(PSP_CTRL_CROSS)) {
            int e = maxlen - 1;
            while (e >= 0 && buf[e] == ' ') e--;
            if (e < 0) { strcpy(name, "Sem Nome"); }
            else { memcpy(name, buf, e + 1); name[e + 1] = 0; }
            return 1;
        }
        if (PRESSED(PSP_CTRL_CIRCLE)) return 0;
        bg(title);
        txc(70, C_WHITE, "Digite o nome");
        tx((SW - maxlen * 8) / 2, 110, C_WHITE, "%s", buf);
        rect((SW - maxlen * 8) / 2 + pos * 8, 120, 8, 2, C_GOLD);
        txc(150, RGB(170, 200, 170), "Cima/Baixo: letra   Esq/Dir: posicao");
        txc(164, RGB(170, 200, 170), "QUAD: apagar   X: confirmar   O: cancelar");
    }
}
static void star_info(void) {
    tx(20, 34, C_GOLD, "%s - %s, %d anos", S.star_name, POSN[S.star_pos], S.star_age);
    tx(20, 48, C_WHITE, "Clube: %s   Overall: %d", TM[S.star_team].name, S.star_ovr);
    tx(20, 62, C_WHITE, "Temporada %d  Jogos %d/10  Gols %d  Assist %d", S.star_season, S.star_games, S.star_goals, S.star_assists);
}
static void star_status(void) {
    for (;;) {
        frame();
        if (PRESSED(PSP_CTRL_CROSS) || PRESSED(PSP_CTRL_CIRCLE)) break;
        bg("STATUS DO JOGADOR");
        tx(30, 44, C_GOLD, "%s", S.star_name);
        tx(30, 62, C_WHITE, "Posicao: %s", POSN[S.star_pos]);
        tx(30, 76, C_WHITE, "Idade: %d anos", S.star_age);
        tx(30, 90, C_WHITE, "Overall: %d", S.star_ovr);
        tx(30, 104, C_WHITE, "Clube: %s", TM[S.star_team].name);
        tx(30, 124, C_WHITE, "Temporada %d: %d jogos, %d gols, %d assist.", S.star_season, S.star_games, S.star_goals, S.star_assists);
        tx(30, 138, C_WHITE, "Carreira: %d gols, %d assistencias", S.star_tgoals + S.star_goals, S.star_tassists + S.star_assists);
        tx(30, 152, C_WHITE, "Progresso de treino: %d/3", S.star_xp);
        tx(8, SH - 12, RGB(170, 200, 170), "X: voltar");
    }
}
static void star_play(void) {
    int opp, s0;
    MRes R;
    do { opp = rand() % MAXT; } while (opp == S.star_team);
    s0 = (S.star_ovr + TM[S.star_team].str) / 2;
    R = play_match(S.star_team, opp, 90, 1, POSI[S.star_pos], s0);
    if (R.quit) return;
    {
        int win = R.g0 > R.g1, dr = (R.g0 == R.g1), rate;
        char a[64], b[64], c[64];
        rate = 58 + R.sg * 11 + R.sa * 7 + (win ? 6 : (dr ? 2 : -2)) + (rand() % 11 - 5) + (S.star_ovr - 60) / 8;
        if (rate < 30) rate = 30;
        if (rate > 100) rate = 100;
        S.star_games++; S.star_goals += R.sg; S.star_assists += R.sa; S.star_rate += rate; S.star_trained = 0;
        snprintf(a, sizeof a, "%s %d x %d %s", TM[S.star_team].name, R.g0, R.g1, TM[opp].name);
        snprintf(b, sizeof b, "Seus gols: %d   Assistencias: %d", R.sg, R.sa);
        snprintf(c, sizeof c, "Nota da partida: %d.%d", rate / 10, rate % 10);
        end_screen("TORNE UMA ESTRELA", a, b, c);
    }
    save_game();
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
    S.star_tgoals += S.star_goals; S.star_tassists += S.star_assists;
    for (i = 0; i < MAXT; i++) {
        if (i != S.star_team && TM[i].str > TM[S.star_team].str && TM[i].str <= S.star_ovr + 8)
            if (best < 0 || TM[i].str < TM[best].str) best = i;
    }
    if (best >= 0) {
        static const char *it[2] = {"Aceitar proposta", "Continuar no clube atual"};
        snprintf(a, sizeof a, "PROPOSTA: %s", TM[best].name);
        if (menu(a, it, 2, 0, 60, "X: escolher") == 0) S.star_team = best;
    }
    S.star_season++; S.star_age++;
    S.star_games = S.star_goals = S.star_assists = S.star_rate = 0;
    if (S.star_age >= 38) {
        snprintf(a, sizeof a, "%s se aposentou!", S.star_name);
        snprintf(b, sizeof b, "Gols na carreira: %d", S.star_tgoals);
        end_screen("FIM DE CARREIRA", a, b, "Obrigado por jogar!");
        S.star_started = 0;
    }
    save_game();
}
static void star_hub(void) {
    char b[3][48];
    const char *it[5];
    int sel = 0, c, k;
    for (k = 0; k < 3; k++) it[k] = b[k];
    it[3] = "Status do jogador"; it[4] = "Salvar e sair";
    while (S.star_started) {
        snprintf(b[0], 48, "Jogar partida %d/10", S.star_games + 1);
        snprintf(b[1], 48, "%s", S.star_trained ? "Treino ja feito nesta rodada" : "Treinar (progresso do overall)");
        snprintf(b[2], 48, "Treino: %d/3 para +1 overall", S.star_xp);
        menu_extra = star_info;
        c = menu("TORNE UMA ESTRELA", it, 5, sel, 90, NULL);
        menu_extra = NULL;
        if (c < 0) c = 4;
        sel = c;
        if (c == 0) {
            star_play();
            if (S.star_started && S.star_games >= 10) star_season_end();
        } else if (c == 1) {
            if (!S.star_trained) {
                S.star_trained = 1; S.star_xp++;
                if (S.star_xp >= 3) { S.star_xp = 0; if (S.star_ovr < 99) S.star_ovr++; end_screen("TREINO", "Treino concluido!", "Seu overall subiu +1", ""); }
                save_game();
            }
        } else if (c == 3) star_status();
        else if (c == 4) { save_game(); return; }
    }
}
static void mode_star(void) {
    static const char *it[3] = {"Continuar carreira", "Nova carreira", "Voltar"};
    for (;;) {
        int c = menu("TORNE UMA ESTRELA", it, 3, 0, 60, NULL);
        if (c < 0 || c == 2) return;
        if (c == 0) {
            if (!S.star_started) { end_screen("TORNE UMA ESTRELA", "Nenhuma carreira salva", "Comece uma nova carreira", ""); continue; }
            star_hub();
        } else {
            char nm[16]; int pos, ids[MAXT], i, j, t;
            strcpy(nm, "Craque");
            if (!edit_name(nm, 12, "NOVA CARREIRA - NOME")) continue;
            pos = menu("ESCOLHA A POSICAO", POSN, 3, 0, 60, NULL);
            if (pos < 0) continue;
            for (i = 0; i < MAXT; i++) ids[i] = i;
            for (i = 0; i < MAXT; i++) for (j = i + 1; j < MAXT; j++)
                if (TM[ids[j]].str < TM[ids[i]].str) { t = ids[i]; ids[i] = ids[j]; ids[j] = t; }
            S.star_started = 1;
            strncpy(S.star_name, nm, 15); S.star_name[15] = 0;
            S.star_pos = pos; S.star_age = 17; S.star_ovr = 58 + rand() % 8;
            S.star_team = ids[rand() % 5];
            S.star_season = 1; S.star_games = S.star_goals = S.star_assists = S.star_rate = 0;
            S.star_xp = S.star_trained = S.star_tgoals = S.star_tassists = 0;
            save_game();
            star_hub();
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
static void mode_edit(void) {
    static const char *it[4] = {"Editar time", "Restaurar times padrao", "Salvar alteracoes", "Voltar"};
    for (;;) {
        int c = menu("EDITAR", it, 4, 0, 60, NULL);
        if (c < 0 || c == 3) { save_game(); return; }
        if (c == 0) {
            int t = pick_team("EDITAR - ESCOLHA O TIME", -1);
            if (t >= 0) edit_team(t);
        } else if (c == 1) {
            if (confirm("RESTAURAR TIMES PADRAO?")) { reset_teams(); save_game(); }
        } else if (c == 2) {
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
    static const char *items[8] = {
        "Amistoso", "Campeonato Municipal", "Liga Da Liberdade", "Seja Treinador",
        "Torne Uma Estrela", "Modo Campanha", "Editar", "Sair"
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
        int c = menu("MENU PRINCIPAL", items, 8, sel, 40, "Cima/Baixo + X");
        if (c < 0) continue;
        sel = c;
        switch (c) {
            case 0: mode_amistoso(); break;
            case 1: { int t = pick_team("CAMPEONATO MUNICIPAL - SEU TIME", -1);
                      if (t >= 0) run_league(8, t, 0, "CAMPEONATO MUNICIPAL"); } break;
            case 2: { int t = pick_team("LIGA DA LIBERDADE - SEU TIME", -1);
                      if (t >= 0) run_league(12, t, 1, "LIGA DA LIBERDADE"); } break;
            case 3: mode_coach(); break;
            case 4: mode_star(); break;
            case 5: mode_campanha(); break;
            case 6: mode_edit(); break;
            case 7: sceKernelExitGame(); break;
        }
    }
    return 0;
}
