#include <gccore.h>
#include <stdio.h>
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

static const u8 digits[16][7] = {
    {14, 17, 19, 21, 25, 17, 14}, {4, 12, 4, 4, 4, 4, 14},
    {14, 17, 1, 2, 4, 8, 31},     {30, 1, 1, 14, 1, 1, 30},
    {2, 6, 10, 18, 31, 2, 2},     {31, 16, 16, 30, 1, 1, 30},
    {14, 16, 16, 30, 17, 17, 14}, {31, 1, 2, 4, 8, 8, 8},
    {14, 17, 17, 14, 17, 17, 14}, {14, 17, 17, 15, 1, 1, 14},
    {14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30},
    {14, 17, 16, 16, 16, 17, 14}, {30, 17, 17, 17, 17, 17, 30},
    {31, 16, 16, 30, 16, 16, 31}, {31, 16, 16, 30, 16, 16, 16}};
static void text(float x, float y, const char* s) {
    const GXColor white = {240, 240, 240, 255};
    for (; *s; ++s, x += 12) {
        int d = *s >= '0' && *s <= '9'   ? *s - '0'
                : *s >= 'A' && *s <= 'F' ? *s - 'A' + 10
                                         : -1;
        for (int row = 0; row < 7; ++row) {
            unsigned bits = d >= 0                  ? digits[d][row]
                            : *s == '-' && row == 3 ? 14
                            : *s == '.' && row == 6 ? 4
                            : *s == 'H'             ? (row == 3 ? 31 : 17)
                            : *s == 'V'             ? (row < 5    ? 17
                                                       : row == 5 ? 10
                                                                  : 4)
                                                    : 0;
            for (int col = 0; col < 5; ++col)
                if (bits & (16 >> col))
                    quad(x + col * 2, y + row * 2, 2, 2, 0, white);
        }
    }
}

static u32 probe_depth(float depth, bool vertical) {
    const float origin = 256.5f - 8.0f * (depth - 0.25f);
    const float x = vertical ? 128.0f : origin;
    const float y = vertical ? origin : 128.0f;
    GX_SetCoPlanar(GX_DISABLE);
    GX_SetColorUpdate(GX_FALSE);
    GX_SetZMode(GX_ENABLE, GX_ALWAYS, GX_FALSE);
    GX_Begin(GX_TRIANGLES, GX_VTXFMT0, 3);
    vertex(x, y, 0.25f, red);
    vertex(x + 4, y, vertical ? 0.25f : 0.75f, red);
    vertex(x, y + 4, vertical ? 0.75f : 0.25f, red);
    GX_End();
    GX_SetCoPlanar(GX_ENABLE);
    GX_SetZMode(GX_ENABLE, GX_ALWAYS, GX_TRUE);
    quad(254, 254, 4, 4, 0.25f, blue);
    GX_DrawDone();
    u32 result;
    GX_PeekZ(256, 256, &result);
    GX_SetCoPlanar(GX_DISABLE);
    return result;
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
    GX_SetViewport(0, 0, 640, 480, 0, 1);
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

    static const float depths[] = {
        -8.25f, -7.75f, -4.25f, -3.75f, -0.25f, 0.25f,  0.5f,   0.75f,  1.25f,
        3.75f,  4.25f,  7.75f,  8.25f,  11.75f, 12.25f, 15.75f, 16.25f, 16.75f};
    unsigned next = 1;
    for (;;) {
        PAD_ScanPads();
        if (PAD_ButtonsDown(0) & PAD_BUTTON_START) break;

        u32 h[18], v[18];
        for (unsigned i = 0; i < 18; ++i) {
            h[i] = probe_depth(depths[i], false);
            v[i] = probe_depth(depths[i], true);
        }
        GX_SetCoPlanar(GX_DISABLE);
        GX_SetColorUpdate(GX_TRUE);
        GX_SetZMode(GX_DISABLE, GX_ALWAYS, GX_FALSE);
        quad(0, 0, 640, 480, 0, (GXColor){16, 16, 16, 255});
        text(32, 16, "D");
        text(188, 16, "H");
        text(356, 16, "V");
        for (unsigned i = 0; i < 18; ++i) {
            char line[64];
            snprintf(line, sizeof(line), "%6.2f", depths[i]);
            text(32, 44 + 22 * i, line);
            snprintf(line, sizeof(line), "%08lX", (unsigned long)h[i]);
            text(188, 44 + 22 * i, line);
            snprintf(line, sizeof(line), "%08lX", (unsigned long)v[i]);
            text(356, 44 + 22 * i, line);
        }
        GX_CopyDisp(xfb[next], GX_FALSE);
        GX_DrawDone();
        VIDEO_SetNextFramebuffer(xfb[next]);
        VIDEO_Flush();
        VIDEO_WaitVSync();
        next ^= 1;
    }
    return 0;
}
