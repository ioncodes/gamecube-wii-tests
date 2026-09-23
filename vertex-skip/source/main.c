#include <gccore.h>
#include <string.h>

typedef struct {
    f32 x, y, z;
} Position;

static u8 fifo[GX_FIFO_MINSIZE] ATTRIBUTE_ALIGN(32);
static Position positions[65536] ATTRIBUTE_ALIGN(32) = {
    {0, 0, -0.5f},
    {0, 180, -0.5f},
    {140, 0, -0.5f},
    {140, 180, -0.5f},
};

static void panel(f32 x, u8 mode) {
    Mtx model;
    guMtxIdentity(model);
    model[0][3] = x;
    model[1][3] = 150;
    GX_LoadPosMtxImm(model, GX_PNMTX0);
    GX_SetVtxDesc(GX_VA_POS, mode);

    const u16 indices[] = {0, 1, 0xffff, 2, 3};
    GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT0, mode == GX_DIRECT ? 4 : 5);
    for (unsigned i = 0; i < 5; ++i) {
        const u16 index = indices[i];
        const int skip = index == 0xffff;

        if (mode == GX_DIRECT) {
            if (skip) continue;

            const Position* p = &positions[index];
            GX_Position3f32(p->x, p->y, p->z);
        } else if (mode == GX_INDEX8) {
            GX_Position1x8((u8)index);  // 0xff disables this vertex.
        } else {
            GX_Position1x16(index);  // 0xffff disables this vertex.
        }

        if (skip)
            GX_Color4u8(230, 60, 50, 255);
        else
            GX_Color4u8(40, 100, 220, 255);
    }
    GX_End();
}

int main(void) {
    VIDEO_Init();
    PAD_Init();
    GXRModeObj* mode = VIDEO_GetPreferredMode(NULL);
    void* xfb[2];
    for (unsigned i = 0; i < 2; ++i) {
        void* buffer = SYS_AllocateFramebuffer(mode);
        if (!buffer) return 1;
        xfb[i] = MEM_K0_TO_K1(buffer);
        VIDEO_ClearFrameBuffer(mode, xfb[i], COLOR_BLACK);
    }
    VIDEO_Configure(mode);
    VIDEO_SetNextFramebuffer(xfb[0]);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (mode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();

    positions[0xff] = positions[0xffff] = (Position){170, -60, -0.5f};
    DCFlushRange(positions, sizeof(positions));

    memset(fifo, 0, sizeof(fifo));
    GX_Init(fifo, sizeof(fifo));
    GX_InvVtxCache();
    GX_SetCopyClear((GXColor){16, 16, 16, 255}, 0x00ffffff);
    GX_SetViewport(0, 0, mode->fbWidth, mode->efbHeight, 0, 1);
    GX_SetScissor(0, 0, mode->fbWidth, mode->efbHeight);
    const u32 height = GX_SetDispCopyYScale(
        GX_GetYScaleFactor(mode->efbHeight, mode->xfbHeight));
    GX_SetDispCopySrc(0, 0, mode->fbWidth, mode->efbHeight);
    GX_SetDispCopyDst(mode->fbWidth, height);
    GX_SetCopyFilter(mode->aa, mode->sample_pattern, GX_FALSE, mode->vfilter);
    GX_SetFieldMode(mode->field_rendering, mode->viHeight == 2 * mode->xfbHeight
                                               ? GX_ENABLE
                                               : GX_DISABLE);
    GX_SetDispCopyGamma(GX_GM_1_0);
    GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    GX_SetDither(GX_DISABLE);
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_COPY);
    GX_SetColorUpdate(GX_TRUE);
    GX_SetAlphaUpdate(GX_FALSE);
    GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GX_SetZMode(GX_DISABLE, GX_ALWAYS, GX_FALSE);
    GX_SetCoPlanar(GX_DISABLE);
    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX,
                   GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
    GX_SetNumTexGens(0);
    GX_SetNumTevStages(1);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetArray(GX_VA_POS, positions, sizeof(Position));
    GX_SetCurrentMtx(GX_PNMTX0);
    Mtx44 projection;
    guOrtho(projection, 0, 480, 0, 640, 0, 1);
    GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
    GX_CopyDisp(xfb[1], GX_TRUE);
    GX_DrawDone();

    unsigned next = 1;
    for (;;) {
        PAD_ScanPads();
        const u32 down = PAD_ButtonsDown(0);
        if (down & PAD_BUTTON_START) break;

        panel(50, GX_DIRECT);
        panel(250, GX_INDEX8);
        panel(450, GX_INDEX16);
        GX_CopyDisp(xfb[next], GX_TRUE);
        GX_DrawDone();
        VIDEO_SetNextFramebuffer(xfb[next]);
        VIDEO_Flush();
        VIDEO_WaitVSync();
        next ^= 1;
    }

    return 0;
}
