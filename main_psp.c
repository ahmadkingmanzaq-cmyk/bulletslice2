/* PSP backend for BULLET SLICE: sceGu coloured sprites + sceCtrl input.
 * Build with the PSPDEV toolchain (see README.md). */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspctrl.h>
#include <math.h>
#include <stddef.h>
#include "game.h"

PSP_MODULE_INFO("BULLETSLICE", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define BUF_W 512
static unsigned int __attribute__((aligned(16))) list[262144];

typedef struct { unsigned int color; short x, y, z; } Vtx;

static void rect(float x, float y, float w, float h, uint32_t col) {
    if (w <= 0 || h <= 0) return;
    if (x > SCR_W || y > SCR_H || x + w < 0 || y + h < 0) return;
    Vtx *v = (Vtx *)sceGuGetMemory(2 * sizeof(Vtx));
    v[0].color = col; v[0].x = (short)floorf(x + 0.5f);     v[0].y = (short)floorf(y + 0.5f);     v[0].z = 0;
    v[1].color = col; v[1].x = (short)floorf(x + w + 0.5f); v[1].y = (short)floorf(y + h + 0.5f); v[1].z = 0;
    if (v[1].x == v[0].x) v[1].x++;
    if (v[1].y == v[0].y) v[1].y++;
    sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, v);
}

static int exit_cb(int a, int b, void *c) { (void)a; (void)b; (void)c; sceKernelExitGame(); return 0; }
static int cb_thread(SceSize args, void *argp) {
    (void)args; (void)argp;
    int id = sceKernelCreateCallback("Exit Callback", exit_cb, NULL);
    sceKernelRegisterExitCallback(id);
    sceKernelSleepThreadCB();
    return 0;
}

static void gu_setup(void) {
    sceGuInit();
    sceGuStart(GU_DIRECT, list);
    sceGuDrawBuffer(GU_PSM_8888, (void *)0, BUF_W);
    sceGuDispBuffer(SCR_W, SCR_H, (void *)0x88000, BUF_W);
    sceGuDepthBuffer((void *)0x110000, BUF_W);
    sceGuOffset(2048 - (SCR_W / 2), 2048 - (SCR_H / 2));
    sceGuViewport(2048, 2048, SCR_W, SCR_H);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuFrontFace(GU_CW);
    sceGuShadeModel(GU_SMOOTH);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

static unsigned read_input(void) {
    SceCtrlData pad;
    sceCtrlPeekBufferPositive(&pad, 1);
    unsigned in = 0;
    int ax = (int)pad.Lx - 128, ay = (int)pad.Ly - 128;
    if (pad.Buttons & PSP_CTRL_LEFT  || ax < -48) in |= IN_LEFT;
    if (pad.Buttons & PSP_CTRL_RIGHT || ax >  48) in |= IN_RIGHT;
    if (pad.Buttons & PSP_CTRL_UP    || ay < -48) in |= IN_UP;
    if (pad.Buttons & PSP_CTRL_DOWN  || ay >  48) in |= IN_DOWN;
    if (pad.Buttons & PSP_CTRL_CROSS)    in |= IN_JUMP;
    if (pad.Buttons & PSP_CTRL_RTRIGGER) in |= IN_FIRE;
    if (pad.Buttons & PSP_CTRL_SQUARE)   in |= IN_RELOAD;
    if (pad.Buttons & PSP_CTRL_CIRCLE)   in |= IN_DODGE;
    if (pad.Buttons & PSP_CTRL_TRIANGLE) in |= IN_PAIN;
    if (pad.Buttons & PSP_CTRL_LTRIGGER) in |= IN_SPRINT;
    if (pad.Buttons & PSP_CTRL_SELECT)   in |= IN_SWAP;
    if (pad.Buttons & PSP_CTRL_START)    in |= IN_START;
    return in;
}

int main(void) {
    int thid = sceKernelCreateThread("update_thread", cb_thread, 0x11, 0xFA0, 0, 0);
    if (thid >= 0) sceKernelStartThread(thid, 0, 0);

    gu_setup();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    static Game game;
    game_init(&game, (unsigned)sceKernelGetSystemTimeLow());
    SceInt64 last = sceKernelGetSystemTimeWide();

    for (;;) {
        SceInt64 now = sceKernelGetSystemTimeWide();
        float dt = (float)(now - last) / 1000000.0f;
        last = now;

        game_update(&game, read_input(), dt);

        sceGuStart(GU_DIRECT, list);
        sceGuClearColor(0xFF000000);
        sceGuClear(GU_COLOR_BUFFER_BIT);
        game_draw(&game, rect);
        sceGuFinish();
        sceGuSync(0, 0);
        sceDisplayWaitVblankStart();
        sceGuSwapBuffers();
    }
    return 0;
}
