#include <gccore.h>
#include <string.h>

static u8 fifo[GX_FIFO_MINSIZE] ATTRIBUTE_ALIGN(32);
static const GXColor blue = {40, 100, 220, 255};
static const GXColor red = {230, 60, 50, 255};

static void vertex(f32 x, f32 y, f32 depth, GXColor color) {
    GX_Position3f32(x, y, -depth);
    GX_Color4u8(color.r, color.g, color.b, color.a);
}

static void quad(f32 x, f32 y, f32 w, f32 h, f32 depth, GXColor color) {
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    vertex(x, y, depth, color);
    vertex(x + w, y, depth, color);
    vertex(x + w, y + h, depth, color);
    vertex(x, y + h, depth, color);
    GX_End();
}

static void panel(f32 x, u8 freeze) {
    GX_SetCoPlanar(GX_DISABLE);
    GX_SetZMode(GX_ENABLE, GX_LEQUAL, GX_TRUE);
    GX_SetColorUpdate(GX_TRUE);

    quad(x + 60, 180, 140, 120, 0.5f, red);

    GX_SetColorUpdate(GX_FALSE);
    GX_SetZMode(GX_ENABLE, GX_ALWAYS, GX_FALSE);
    GX_Begin(GX_TRIANGLES, GX_VTXFMT0, 3);
    vertex(x + 8, 128, 0.75f, red);
    vertex(x + 16, 128, 0.75f, red);
    vertex(x + 8, 136, 0.75f, red);
    GX_End();
    GX_SetCoPlanar(freeze);

    GX_SetColorUpdate(GX_TRUE);
    GX_SetZMode(GX_ENABLE, GX_LEQUAL, GX_TRUE);

    quad(x, 120, 260, 240, 0.25f, blue);
    GX_SetCoPlanar(GX_DISABLE);
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
    }

    VIDEO_Configure(mode);
    VIDEO_ClearFrameBuffer(mode, xfb[0], COLOR_BLACK);
    VIDEO_ClearFrameBuffer(mode, xfb[1], COLOR_BLACK);
    VIDEO_SetNextFramebuffer(xfb[0]);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (mode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();

    memset(fifo, 0, sizeof(fifo));
    GX_Init(fifo, sizeof(fifo));
    GX_SetCopyClear((GXColor){16, 16, 16, 255}, 0x00ffffff);
    GX_SetViewport(0, 0, mode->fbWidth, mode->efbHeight, 0, 1);
    GX_SetScissor(0, 0, mode->fbWidth, mode->efbHeight);
    u32 height = GX_SetDispCopyYScale(
        GX_GetYScaleFactor(mode->efbHeight, mode->xfbHeight));
    GX_SetDispCopySrc(0, 0, mode->fbWidth, mode->efbHeight);
    GX_SetDispCopyDst(mode->fbWidth, height);
    GX_SetCopyFilter(mode->aa, mode->sample_pattern, GX_TRUE, mode->vfilter);
    GX_SetFieldMode(mode->field_rendering, mode->viHeight == 2 * mode->xfbHeight
                                               ? GX_ENABLE
                                               : GX_DISABLE);
    GX_SetDispCopyGamma(GX_GM_1_0);
    GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_COPY);
    GX_SetAlphaUpdate(GX_FALSE);
    GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GX_SetZCompLoc(GX_TRUE);

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX,
                   GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
    GX_SetNumTexGens(0);
    GX_SetNumTevStages(1);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);

    Mtx model;
    Mtx44 projection;
    guMtxIdentity(model);
    GX_LoadPosMtxImm(model, GX_PNMTX0);
    GX_SetCurrentMtx(GX_PNMTX0);
    guOrtho(projection, 0, 480, 0, 640, 0, 1);
    GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);

    GX_SetZMode(GX_ENABLE, GX_LEQUAL, GX_TRUE);
    GX_SetColorUpdate(GX_TRUE);
    GX_CopyDisp(xfb[1], GX_TRUE);
    GX_DrawDone();

    unsigned next = 1;
    for (;;) {
        PAD_ScanPads();
        if (PAD_ButtonsDown(0) & PAD_BUTTON_START) break;

        panel(40, GX_DISABLE);
        panel(340, GX_ENABLE);
        GX_CopyDisp(xfb[next], GX_TRUE);
        GX_DrawDone();
        VIDEO_SetNextFramebuffer(xfb[next]);
        VIDEO_Flush();
        VIDEO_WaitVSync();
        next ^= 1;
    }

    return 0;
}
