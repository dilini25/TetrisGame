#include "raylib.h"
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/*
 * Raylib port of Tetris-2.c (Win32 GDI).
 * - Keeps the same piece/board/session logic (frames-based DAS/gravity/lock).
 * - Replaces Win32 rendering (HDC/BitBlt) with raylib drawing.
 * - Replaces Win32 input polling (GetAsyncKeyState) with raylib input.
 */

/* ── §1 CONSTANTS ────────────────────────────────────────────────────── */
#define CELL 30
#define COLS 10
#define ROWS 20
#define HIDDEN 2
#define TROWS (ROWS + HIDDEN)
#define SIDE_W 170
#define PLAY_W (COLS * CELL) /* 300 */
#define PLAY_H (ROWS * CELL) /* 600 */
#define WIN_W (PLAY_W + SIDE_W)
#define WIN_H PLAY_H

#define LOCK_DELAY 45
#define LOCK_MOVES 15
#define DAS_DELAY 14
#define DAS_RATE 2
#define CLEAR_ANIM 25
#define TIMER_MS 16

/* Gravity: frames per automatic 1-cell drop, indexed by level 1-20 */
static const int GRAV[21] = {
    0, 48, 43, 38, 33, 28, 23, 18, 13, 8, 6, 5, 5, 4, 4, 3, 3, 2, 2, 1, 1};

/* Scoring */
#define PTS_SOFT 1
#define PTS_HARD 2
static const int LINE_PTS[5] = {0, 100, 300, 500, 800};

/* Colors (raylib Color) */
static const Color C_BG = (Color){13, 13, 22, 255};
static const Color C_GRID = (Color){28, 28, 42, 255};
static const Color C_EDGE = (Color){55, 55, 80, 255};
static const Color C_PANEL = (Color){8, 8, 16, 255};
static const Color C_WHITE = (Color){240, 240, 240, 255};
static const Color C_GRAY = (Color){110, 110, 130, 255};
static const Color C_GOLD = (Color){255, 215, 0, 255};

/* Tetromino colours: index 0=empty, 1=I, 2=O, 3=T, 4=S, 5=Z, 6=J, 7=L */
static const Color COL[8] = {
    (Color){0, 0, 0, 255},
    (Color){0, 230, 230, 255},   /* I */
    (Color){230, 230, 0, 255},  /* O */
    (Color){150, 0, 230, 255},   /* T */
    (Color){0, 220, 0, 255},     /* S */
    (Color){220, 0, 0, 255},     /* Z */
    (Color){0, 0, 220, 255},     /* J */
    (Color){230, 140, 0, 255},   /* L */
};

static Color ColorLerp(Color a, Color b, float t)
{
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return (Color){
        (unsigned char)(a.r + (b.r - a.r) * t),
        (unsigned char)(a.g + (b.g - a.g) * t),
        (unsigned char)(a.b + (b.b - a.b) * t),
        255};
}

/* ── §2 PIECE DATA ───────────────────────────────────────────────────── */
typedef struct
{
    int id;
    unsigned rot[4];
} PieceDef;

static const PieceDef PDEFS[7] = {
    {1, {0x0F00, 0x2222, 0x00F0, 0x4444}}, /* I */
    {2, {0x0660, 0x0660, 0x0660, 0x0660}}, /* O */
    {3, {0x0E40, 0x4C40, 0x4E00, 0x4640}}, /* T */
    {4, {0x06C0, 0x8C40, 0x06C0, 0x8C40}}, /* S */
    {5, {0x0C60, 0x4C80, 0x0C60, 0x4C80}}, /* Z */
    {6, {0x08E0, 0x6440, 0x0E20, 0x44C0}}, /* J */
    {7, {0x02E0, 0x4460, 0x0E80, 0xC440}}, /* L */
};

static inline bool PCell(const PieceDef *p, int rot, int r, int c)
{
    return ((p->rot[rot] >> ((3 - r) * 4 + (3 - c))) & 1u) != 0;
}

/* ── §3 TYPES ───────────────────────────────────────────────────────── */
typedef enum
{
    S_MENU = 0,
    S_PLAY = 1,
    S_PAUSE = 2,
    S_OVER = 3,
} State;

typedef struct
{
    int x, y, type, rot;
} Piece;

/* ── §4 GAME STATE ──────────────────────────────────────────────────── */
typedef struct
{
    int board[TROWS][COLS];
    Piece cur;
    Piece nxt[3];
    int held;
    bool holdUsed;
    State state;
    int score, hi, level, lines, combo;
    int gravTimer;
    int lockTimer;
    int lockMoves;
    bool onGround;
    int dasDir; /* -1 left | 0 none | 1 right */
    int dasTimer;
    int dasRepeat;
    float anim;
    int flashTimer;
    int flashRows[4];
    int flashCount;
    bool newHi;
    int bag[7];
    int bagIdx;
} GS;

/* ── §5 7-BAG RANDOMISER ─────────────────────────────────────────────── */
static void BagShuffle(GS *g)
{
    for (int i = 0; i < 7; i++)
        g->bag[i] = i;
    for (int i = 6; i > 0; i--)
    {
        int j = rand() % (i + 1);
        int tmp = g->bag[i];
        g->bag[i] = g->bag[j];
        g->bag[j] = tmp;
    }
    g->bagIdx = 0;
}

static int BagNext(GS *g)
{
    if (g->bagIdx >= 7)
        BagShuffle(g);
    return g->bag[g->bagIdx++];
}

/* ── §6 BOARD / COLLISION ───────────────────────────────────────────── */
static bool Valid(const GS *g, const Piece *p)
{
    const PieceDef *def = &PDEFS[p->type];
    for (int r = 0; r < 4; r++)
    {
        for (int c = 0; c < 4; c++)
        {
            if (!PCell(def, p->rot, r, c))
                continue;
            int br = p->y + r;
            int bc = p->x + c;
            if (bc < 0 || bc >= COLS || br >= TROWS)
                return false;
            if (br >= 0 && g->board[br][bc])
                return false;
        }
    }
    return true;
}

static void LockPiece(GS *g)
{
    const PieceDef *def = &PDEFS[g->cur.type];
    for (int r = 0; r < 4; r++)
    {
        for (int c = 0; c < 4; c++)
        {
            if (!PCell(def, g->cur.rot, r, c))
                continue;
            int br = g->cur.y + r;
            int bc = g->cur.x + c;
            if (br >= 0 && br < TROWS && bc >= 0 && bc < COLS)
                g->board[br][bc] = def->id;
        }
    }
}

static int ScanClear(GS *g)
{
    int count = 0;
    g->flashCount = 0;
    for (int r = 0; r < TROWS; r++)
    {
        bool full = true;
        for (int c = 0; c < COLS; c++)
        {
            if (!g->board[r][c])
            {
                full = false;
                break;
            }
        }
        if (full && g->flashCount < 4)
        {
            g->flashRows[g->flashCount++] = r;
            count++;
        }
    }
    if (count > 0)
        g->flashTimer = CLEAR_ANIM;
    return count;
}

static void RemoveClear(GS *g)
{
    for (int r = TROWS - 1; r >= 0;)
    {
        bool full = true;
        for (int c = 0; c < COLS; c++)
        {
            if (!g->board[r][c])
            {
                full = false;
                break;
            }
        }
        if (full)
        {
            /* collapse: shift [0..r-1] down by 1 row range */
            memmove(&g->board[1], &g->board[0], r * sizeof(g->board[0]));
            memset(&g->board[0], 0, sizeof(g->board[0]));
        }
        else
        {
            r--;
        }
    }
}

static int GhostY(const GS *g)
{
    Piece t = g->cur;
    while (true)
    {
        t.y++;
        if (!Valid(g, &t))
            return t.y - 1;
    }
}

/* ── §6 piece spawning & rotation ──────────────────────────────────── */
static Piece MakePiece(int type)
{
    Piece p;
    p.type = type;
    p.rot = 0;
    p.x = COLS / 2 - 2;
    p.y = HIDDEN - 2;
    return p;
}

static bool TryRotate(GS *g, int dir)
{
    /* 5-point SRS wall-kick offsets */
    static const int kx[5] = {0, -1, 1, -1, 1};
    static const int ky[5] = {0, 0, 0, -1, -1};
    Piece t = g->cur;
    t.rot = (t.rot + 4 + dir) % 4;
    for (int i = 0; i < 5; i++)
    {
        t.x = g->cur.x + kx[i];
        t.y = g->cur.y + ky[i];
        if (Valid(g, &t))
        {
            g->cur = t;
            return true;
        }
    }
    return false;
}

static void Spawn(GS *g)
{
    g->cur = g->nxt[0];
    g->nxt[0] = g->nxt[1];
    g->nxt[1] = g->nxt[2];
    g->nxt[2] = MakePiece(BagNext(g));
    g->holdUsed = false;
    g->onGround = false;
    g->lockTimer = 0;
    g->lockMoves = 0;
    g->gravTimer = GRAV[g->level];
}

/* ── §7 GAME SESSION ──────────────────────────────────────────────── */
static void SessionInit(GS *g)
{
    memset(g, 0, sizeof(*g));
    g->state = S_MENU;
    g->held = -1;
}

static void SessionStart(GS *g)
{
    int hi = g->hi;
    memset(g, 0, sizeof(*g));
    g->state = S_PLAY;
    g->hi = hi;
    g->level = 1;
    g->held = -1;
    g->combo = -1;
    BagShuffle(g);
    g->nxt[0] = MakePiece(BagNext(g));
    g->nxt[1] = MakePiece(BagNext(g));
    g->nxt[2] = MakePiece(BagNext(g));
    Spawn(g);
}

static void DoLock(GS *g)
{
    int cleared;
    LockPiece(g);
    cleared = ScanClear(g);
    if (cleared > 0)
    {
        g->combo++;
        g->score += LINE_PTS[cleared] * g->level +
                     (g->combo > 0 ? 50 * g->combo * g->level : 0);
        g->lines += cleared;
        g->level = g->lines / 10 + 1;
        if (g->level > 20)
            g->level = 20;
    }
    else
    {
        g->combo = -1;
    }

    if (g->score > g->hi)
    {
        g->hi = g->score;
        g->newHi = true;
    }

    if (g->flashTimer <= 0)
        RemoveClear(g);

    Spawn(g);
    if (!Valid(g, &g->cur))
        g->state = S_OVER;
}

static void HardDrop(GS *g)
{
    int cells = 0;
    while (true)
    {
        Piece t = g->cur;
        t.y++;
        if (!Valid(g, &t))
            break;
        g->cur.y++;
        cells++;
    }
    g->score += cells * PTS_HARD;
    DoLock(g);
}

static void HoldPiece(GS *g)
{
    if (g->holdUsed)
        return;

    g->holdUsed = true;
    if (g->held < 0)
    {
        g->held = g->cur.type;
        Spawn(g);
    }
    else
    {
        int tmp = g->held;
        g->held = g->cur.type;
        g->cur = MakePiece(tmp);
        g->onGround = false;
        g->lockTimer = 0;
    }
}

static void SessionUpdate(GS *g)
{
    /* frames-based logic: mimic TIMER_MS cadence */
    float dt = TIMER_MS / 1000.0f;
    Piece t = {0};
    g->anim += dt;

    switch (g->state)
    {
    case S_MENU:
        if (IsKeyPressed(KEY_ENTER))
            SessionStart(g);
        break;

    case S_PLAY:
        if (g->flashTimer > 0)
        {
            g->flashTimer--;
            if (g->flashTimer == 0)
                RemoveClear(g);
            break;
        }

        /* DAS — Delayed Auto Shift for held left/right */
        if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A))
        {
            if (g->dasDir != -1)
            {
                g->dasDir = -1;
                g->dasTimer = 0;
                g->dasRepeat = 0;
            }
        }
        else if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D))
        {
            if (g->dasDir != 1)
            {
                g->dasDir = 1;
                g->dasTimer = 0;
                g->dasRepeat = 0;
            }
        }
        else
        {
            g->dasDir = 0;
        }

        if (g->dasDir != 0)
        {
            bool move = false;
            if (g->dasTimer == 0)
            {
                move = true;
                g->dasTimer = 1;
            }
            else if (g->dasTimer >= DAS_DELAY)
            {
                if (++g->dasRepeat >= DAS_RATE)
                {
                    move = true;
                    g->dasRepeat = 0;
                }
                g->dasTimer++;
            }
            else
            {
                g->dasTimer++;
            }

            if (move)
            {
                t = g->cur;
                t.x += g->dasDir;
                if (Valid(g, &t))
                {
                    g->cur = t;
                    if (g->onGround && g->lockMoves < LOCK_MOVES)
                    {
                        g->lockTimer = 0;
                        g->lockMoves++;
                    }
                }
            }
        }

        /* Soft drop */
        if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S))
        {
            t = g->cur;
            t.y++;
            if (Valid(g, &t))
            {
                g->cur = t;
                g->score += PTS_SOFT;
                g->gravTimer = GRAV[g->level];
            }
        }

        /* Gravity */
        if (--g->gravTimer <= 0)
        {
            g->gravTimer = GRAV[g->level];
            t = g->cur;
            t.y++;
            if (Valid(g, &t))
            {
                g->cur = t;
                g->onGround = false;
            }
            else
            {
                g->onGround = true;
            }
        }

        /* Lock delay */
        t = g->cur;
        t.y++;
        g->onGround = !Valid(g, &t);
        if (g->onGround)
        {
            if (++g->lockTimer >= LOCK_DELAY || g->lockMoves >= LOCK_MOVES)
                DoLock(g);
        }
        else
        {
            g->lockTimer = 0;
        }
        break;

    case S_PAUSE:
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_P) || IsKeyPressed(KEY_ESCAPE))
            g->state = S_PLAY;
        break;

    case S_OVER:
        if (IsKeyPressed(KEY_ENTER))
            SessionStart(g);
        if (IsKeyPressed(KEY_ESCAPE))
            g->state = S_MENU;
        break;
    }
}

/* ── §8 RAYLIB DRAW HELPERS ──────────────────────────────────────────── */
static void DrawBevelCell(int px, int py, int sz, Color col)
{
    /* Fill base */
    DrawRectangle(px, py, sz, sz, col);

    /* Highlight (+60) */
    int hr = (int)col.r + 60; if (hr > 255) hr = 255;
    int hg = (int)col.g + 60; if (hg > 255) hg = 255;
    int hb = (int)col.b + 60; if (hb > 255) hb = 255;
    Color hi = (Color){(unsigned char)hr, (unsigned char)hg, (unsigned char)hb, 255};

    /* Shadow (-50) */
    int sr = (int)col.r - 50; if (sr < 0) sr = 0;
    int sg = (int)col.g - 50; if (sg < 0) sg = 0;
    int sb = (int)col.b - 50; if (sb < 0) sb = 0;
    Color sh = (Color){(unsigned char)sr, (unsigned char)sg, (unsigned char)sb, 255};

    /* Approximate the original 3D edge lines */
    DrawLine(px + 1, py + sz - 2, px + 1, py + 1, hi);
    DrawLine(px + 1, py + 1, px + sz - 2, py + 1, hi);

    DrawLine(px + sz - 2, py + 2, px + sz - 2, py + sz - 2, sh);
    DrawLine(px + 2, py + sz - 2, px + sz - 2, py + sz - 2, sh);
}

static void TxtCtr(int y, int xOff, int wide, const char *s, Color col, int fs)
{
    int w = MeasureText(s, fs);
    DrawText(s, xOff + (wide - w) / 2, y, fs, col);
}

/* ── §9 DRAW GAME ───────────────────────────────────────────────────── */
static void DrawCell(int br, int bc, Color col, bool ghost)
{
    int px = bc * CELL;
    int py = (br - HIDDEN) * CELL;
    if (py < 0 || py >= PLAY_H)
        return;

    if (ghost)
    {
        Rectangle rec = {(float)(px + 1), (float)(py + 1), (float)(CELL - 2), (float)(CELL - 2)};
        Color gc = ColorLerp(C_BG, col, 0.20f);
        Color oc = ColorLerp(C_BG, col, 0.45f);
        DrawRectangleRec(rec, gc);
        DrawRectangleLinesEx(rec, 1, oc);
    }
    else
    {
        DrawBevelCell(px, py, CELL, col);
    }
}

static void DrawBoard(const GS *g)
{
    /* Board background */
    DrawRectangle(0, 0, PLAY_W, PLAY_H, C_BG);

    /* Grid lines */
    for (int c = 1; c < COLS; c++)
        DrawLine(c * CELL, 0, c * CELL, PLAY_H, C_GRID);
    for (int r = 1; r < ROWS; r++)
        DrawLine(0, r * CELL, PLAY_W, r * CELL, C_GRID);

    /* Placed cells */
    for (int r = HIDDEN; r < TROWS; r++)
    {
        for (int c = 0; c < COLS; c++)
        {
            if (g->board[r][c])
                DrawCell(r, c, COL[g->board[r][c]], false);
        }
    }

    /* Ghost + active piece */
    if (g->state == S_PLAY || g->state == S_PAUSE)
    {
        int gy = GhostY(g);
        const PieceDef *def = &PDEFS[g->cur.type];
        for (int r = 0; r < 4; r++)
        {
            for (int c = 0; c < 4; c++)
            {
                if (!PCell(def, g->cur.rot, r, c))
                    continue;
                if (gy + r != g->cur.y + r)
                    DrawCell(gy + r, g->cur.x + c, COL[def->id], true);
            }
        }
        for (int r = 0; r < 4; r++)
        {
            for (int c = 0; c < 4; c++)
            {
                if (PCell(def, g->cur.rot, r, c))
                    DrawCell(g->cur.y + r, g->cur.x + c, COL[def->id], false);
            }
        }
    }

    /* Line-clear flash */
    if (g->flashTimer > 0)
    {
        float t = (float)g->flashTimer / (float)CLEAR_ANIM;
        Color fc = ColorLerp(C_BG, (Color){255, 255, 255, 255}, t * 0.75f);
        for (int i = 0; i < g->flashCount; i++)
        {
            int vr = g->flashRows[i] - HIDDEN;
            if (vr < 0 || vr >= ROWS)
                continue;
            DrawRectangle(0, vr * CELL, PLAY_W, CELL, fc);
        }
    }

    /* Border */
    DrawRectangleLinesEx((Rectangle){0, 0, PLAY_W, PLAY_H}, 2, C_EDGE);
}

static void DrawMini(const PieceDef *def, int type, int ox, int oy, int bsz, float dim)
{
    int csz = bsz / 5;
    int sx = ox + (bsz - 4 * csz) / 2;
    int sy = oy + (bsz - 4 * csz) / 2;

    Color col = dim < 1.0f ? ColorLerp(COL[def->id], (Color){40, 40, 55, 255}, 1.0f - dim) : COL[def->id];
    (void)type;

    for (int r = 0; r < 4; r++)
    {
        for (int c = 0; c < 4; c++)
        {
            if (PCell(def, 0, r, c))
                DrawBevelCell(sx + c * csz, sy + r * csz, csz, col);
        }
    }
}

static void DrawPanel(const GS *g)
{
    int px = PLAY_W;
    int tx = PLAY_W + 10;
    int bw = SIDE_W - 20;
    int sy = 0;

    /* Panel background + border */
    DrawRectangle(px, 0, SIDE_W, WIN_H, C_PANEL);
    DrawRectangleLinesEx((Rectangle){px, 0, SIDE_W, WIN_H}, 2, C_EDGE);

    /* HOLD */
    DrawText("HOLD", tx, 8, 12, C_GRAY);
    {
        int hy = 26;
        int hbw = bw;
        DrawRectangle(tx, hy, hbw, hbw, (Color){18, 18, 30, 255});
        DrawRectangleLinesEx((Rectangle){tx, hy, hbw, hbw}, 1, C_EDGE);
        if (g->held >= 0)
        {
            float dim = g->holdUsed ? 0.35f : 1.0f;
            DrawMini(&PDEFS[g->held], g->held, tx, hy, hbw, dim);
        }
        sy = hy + hbw + 8;
    }

    /* NEXT */
    DrawText("NEXT", tx, sy, 12, C_GRAY);
    sy += 18;
    for (int i = 0; i < 3; i++)
    {
        int nbw = (i == 0) ? bw : (int)(bw * 0.72f);
        int nx = tx + (bw - nbw) / 2;
        DrawRectangle(nx, sy, nbw, nbw, (Color){18, 18, 30, 255});
        DrawRectangleLinesEx((Rectangle){nx, sy, nbw, nbw}, 1, C_EDGE);
        DrawMini(&PDEFS[g->nxt[i].type], g->nxt[i].type, nx, sy, nbw, 1.0f);
        sy += nbw + 5;
    }

    sy += 8;

    /* STATS */
    DrawText("SCORE", tx, sy, 11, C_GRAY);
    sy += 15;
    char buf[64];
    snprintf(buf, sizeof(buf), "%d", g->score);
    TxtCtr(sy, px, SIDE_W, buf, C_WHITE, 17);
    sy += 20;

    DrawText("BEST", tx, sy, 11, C_GRAY);
    sy += 15;
    snprintf(buf, sizeof(buf), "%d", g->hi);
    TxtCtr(sy, px, SIDE_W, buf, C_GOLD, 17);
    sy += 20;

    DrawText("LEVEL", tx, sy, 11, C_GRAY);
    sy += 15;
    snprintf(buf, sizeof(buf), "%d", g->level);
    TxtCtr(sy, px, SIDE_W, buf, C_WHITE, 17);
    sy += 20;

    DrawText("LINES", tx, sy, 11, C_GRAY);
    sy += 15;
    snprintf(buf, sizeof(buf), "%d", g->lines);
    TxtCtr(sy, px, SIDE_W, buf, C_WHITE, 17);
    sy += 20;

    if (g->combo > 0)
    {
        snprintf(buf, sizeof(buf), "COMBO x%d", g->combo + 1);
        TxtCtr(sy, px, SIDE_W, buf, (Color){255, 160, 0, 255}, 12);
        sy += 18;
    }

    /* Controls hint */
    int hintY = WIN_H - 88;
    DrawText("[Z]     Rotate CCW", tx, hintY, 10, C_GRAY);
    hintY = WIN_H - 74;
    DrawText("[X/Up]  Rotate CW", tx, hintY, 10, C_GRAY);
    hintY = WIN_H - 60;
    DrawText("[C]     Hold", tx, hintY, 10, C_GRAY);
    hintY = WIN_H - 46;
    DrawText("[Space] Hard drop", tx, hintY, 10, C_GRAY);
    hintY = WIN_H - 32;
    DrawText("[P]     Pause", tx, hintY, 10, C_GRAY);
}

static void DrawMenu(const GS *g)
{
    DrawRectangle(0, 0, WIN_W, WIN_H, (Color){8, 8, 18, 255});

    /* animated vertical glow lines */
    for (int i = 0; i < 30; i++)
    {
        int bright = (int)(35.0f + 30.0f * sinf(g->anim * 1.8f + (float)i * 0.35f));
        float yf = fmodf((float)(i * 24) + g->anim * 50.f, (float)WIN_H);
        int y = (int)yf;
        Color ab = (Color){(unsigned char)(bright / 3), 0, (unsigned char)bright, 255};
        DrawRectangle(0, y, 6, 14, ab);
        DrawRectangle(WIN_W - 6, y, 6, 14, ab);
    }

    /* Title */
    int yTitle1 = 62;
    int yTitle2 = 65;
    int w = MeasureText("TETRIS", 72);
    int tx = (WIN_W - w) / 2;
    DrawText("TETRIS", tx + 4, yTitle2, 72, (Color){35, 0, 70, 255});
    DrawText("TETRIS", tx, yTitle1, 72, (Color){160, 0, 255, 255});

    TxtCtr(148, 0, WIN_W, "Win32 / C   POP Semester 2", (Color){60, 0, 90, 255}, 14);
    TxtCtr(183, 0, WIN_W, "Arrows / WASD  - Move & Soft Drop", (Color){130, 130, 160, 255}, 13);
    TxtCtr(200, 0, WIN_W, "Z              - Rotate CCW", (Color){130, 130, 160, 255}, 13);
    TxtCtr(217, 0, WIN_W, "X / Up         - Rotate CW", (Color){130, 130, 160, 255}, 13);
    TxtCtr(234, 0, WIN_W, "C              - Hold piece", (Color){130, 130, 160, 255}, 13);
    TxtCtr(251, 0, WIN_W, "Space          - Hard drop", (Color){130, 130, 160, 255}, 13);
    TxtCtr(268, 0, WIN_W, "P              - Pause", (Color){130, 130, 160, 255}, 13);

    if (((int)(g->anim * 2.0f)) % 2 == 0)
        TxtCtr(310, 0, WIN_W, "Press  ENTER  to Start", C_WHITE, 19);

    if (g->hi > 0)
    {
        char hib[64];
        snprintf(hib, sizeof(hib), "Session Best: %d", g->hi);
        TxtCtr(WIN_H - 44, 0, WIN_W, hib, C_GOLD, 14);
    }
}

static void DrawPause(void)
{
    DrawRectangle(0, 0, WIN_W, WIN_H, BLACK);
    /* small hack to mimic the hatch: draw some diagonal lines */
    for (int x = -WIN_H; x < WIN_W; x += 12)
        DrawLine(x, 0, x + WIN_H, WIN_H, (Color){20, 0, 40, 70});

    int w = MeasureText("PAUSED", 56);
    int px = (WIN_W - w) / 2;
    int py = WIN_H / 2 - 28;
    DrawText("PAUSED", px + 3, py + 3, 56, (Color){30, 0, 55, 255});
    DrawText("PAUSED", px, py, 56, (Color){160, 0, 255, 255});
    TxtCtr(py + 64, 0, WIN_W, "P / Enter to resume", (Color){140, 140, 160, 255}, 15);
}

static void DrawOver(const GS *g)
{
    DrawRectangle(0, 0, WIN_W, WIN_H, BLACK);
    /* rounded panel */
    Rectangle rr = {35, 108, WIN_W - 70, WIN_H - 70};
    DrawRectangleRounded(rr, 18, 18, (Color){10, 6, 22, 255});
    DrawRectangleRoundedLines(rr, 18, 18, 2, (Color){110, 0, 150, 255});

    int w = MeasureText("GAME OVER", 48);
    int gx = (WIN_W - w) / 2;
    DrawText("GAME OVER", gx + 3, 136, 48, (Color){40, 0, 65, 255});
    DrawText("GAME OVER", gx, 133, 48, (Color){170, 0, 240, 255});

    char rows[3][64];
    snprintf(rows[0], sizeof(rows[0]), "Score:  %d", g->score);
    snprintf(rows[1], sizeof(rows[1]), "Lines:  %d", g->lines);
    snprintf(rows[2], sizeof(rows[2]), "Level:  %d", g->level);

    for (int i = 0; i < 3; i++)
        TxtCtr(196 + i * 28, 0, WIN_W, rows[i], (i == 0) ? C_WHITE : (Color){200, 200, 220, 255}, 17);

    if (g->newHi)
        TxtCtr(286, 0, WIN_W, "** New High Score! **", C_GOLD, 16);

    if (((int)(g->anim * 2.0f)) % 2 == 0)
        TxtCtr(322, 0, WIN_W, "ENTER  -  Play Again", C_WHITE, 16);
    TxtCtr(346, 0, WIN_W, "ESC    -  Main Menu", (Color){120, 120, 145, 255}, 15);
}

int main(void)
{
    SetTraceLogLevel(LOG_NONE);
    srand((unsigned)time(NULL));

    GS g;
    SessionInit(&g);

    InitWindow(WIN_W, WIN_H, "Tetris (raylib)");
    SetTargetFPS(60);

    while (!WindowShouldClose())
    {
        /* One-shot keys (handled here like WndProc WM_KEYDOWN) */
        if (g.state == S_PLAY && g.flashTimer <= 0)
        {
            if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_X))
            {
                if (TryRotate(&g, 1))
                {
                    Piece t = g.cur;
                    t.y++;
                    if (!Valid(&g, &t))
                    {
                        g.lockTimer = 0;
                        if (g.lockMoves < LOCK_MOVES)
                            g.lockMoves++;
                    }
                }
            }
            else if (IsKeyPressed(KEY_Z))
            {
                if (TryRotate(&g, -1))
                {
                    Piece t = g.cur;
                    t.y++;
                    if (!Valid(&g, &t))
                    {
                        g.lockTimer = 0;
                        if (g.lockMoves < LOCK_MOVES)
                            g.lockMoves++;
                    }
                }
            }
            else if (IsKeyPressed(KEY_SPACE))
            {
                HardDrop(&g);
            }
            else if (IsKeyPressed(KEY_C))
            {
                HoldPiece(&g);
            }
            else if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_ESCAPE))
            {
                g.state = S_PAUSE;
            }
        }

        SessionUpdate(&g);

        BeginDrawing();
        ClearBackground(C_BG);
        switch (g.state)
        {
        case S_MENU:
            DrawMenu(&g);
            break;
        case S_PLAY:
            DrawBoard(&g);
            DrawPanel(&g);
            break;
        case S_PAUSE:
            DrawBoard(&g);
            DrawPanel(&g);
            DrawPause();
            break;
        case S_OVER:
            DrawBoard(&g);
            DrawPanel(&g);
            DrawOver(&g);
            break;
        }
        EndDrawing();
    }

    CloseWindow();
    return 0;
}

