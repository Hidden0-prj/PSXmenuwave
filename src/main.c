#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "gpu.h"
#include "ps1/cop0.h"
#include "ps1/gpucmd.h"
#include "ps1/gte.h"
#include "ps1/registers.h"
#include "trig.h"

// Include your dynamically generated 3D model and textures!
#include "model_data.h"
#include "texture_data.h"
#include "font_data.h"

#define GTE_UNIT (1 << 12)

static void setupGTE(int width, int height) {
    cop0_setReg(COP0_STATUS, cop0_getReg(COP0_STATUS) | COP0_STATUS_CU2);
    gte_setControlReg(GTE_OFX, (width  << 16) / 2);
    gte_setControlReg(GTE_OFY, (height << 16) / 2);
    int focalLength = (width < height) ? width : height;
    gte_setControlReg(GTE_H, focalLength / 2);
    gte_setControlReg(GTE_ZSF3, GPU_ORDERING_TABLE_SIZE / 3);
    gte_setControlReg(GTE_ZSF4, GPU_ORDERING_TABLE_SIZE / 4);
}

static void multiplyCurrentMatrixByVectors(GTEMatrix *output) {
    gte_command(GTE_CMD_MVMVA | GTE_SF | GTE_MX_RT | GTE_V_V0 | GTE_CV_NONE);
    output->values[0][0] = (int16_t) gte_getDataReg(GTE_IR1);
    output->values[1][0] = (int16_t) gte_getDataReg(GTE_IR2);
    output->values[2][0] = (int16_t) gte_getDataReg(GTE_IR3);

    gte_command(GTE_CMD_MVMVA | GTE_SF | GTE_MX_RT | GTE_V_V1 | GTE_CV_NONE);
    output->values[0][1] = (int16_t) gte_getDataReg(GTE_IR1);
    output->values[1][1] = (int16_t) gte_getDataReg(GTE_IR2);
    output->values[2][1] = (int16_t) gte_getDataReg(GTE_IR3);

    gte_command(GTE_CMD_MVMVA | GTE_SF | GTE_MX_RT | GTE_V_V2 | GTE_CV_NONE);
    output->values[0][2] = (int16_t) gte_getDataReg(GTE_IR1);
    output->values[1][2] = (int16_t) gte_getDataReg(GTE_IR2);
    output->values[2][2] = (int16_t) gte_getDataReg(GTE_IR3);
}

static void rotateCurrentMatrix(int yaw, int pitch, int roll) {
    static GTEMatrix multiplied;
    int s, c;

    if (yaw) {
        s = isin(yaw); c = icos(yaw);
        gte_setColumnVectors(c, -s, 0, s, c, 0, 0, 0, GTE_UNIT);
        multiplyCurrentMatrixByVectors(&multiplied);
        gte_loadRotationMatrix(&multiplied);
    }
    if (pitch) {
        s = isin(pitch); c = icos(pitch);
        gte_setColumnVectors(c, 0, s, 0, GTE_UNIT, 0, -s, 0, c);
        multiplyCurrentMatrixByVectors(&multiplied);
        gte_loadRotationMatrix(&multiplied);
    }
    if (roll) {
        s = isin(roll); c = icos(roll);
        gte_setColumnVectors(GTE_UNIT, 0, 0, 0, c, -s, 0, s, c);
        multiplyCurrentMatrixByVectors(&multiplied);
        gte_loadRotationMatrix(&multiplied);
    }
}

// --- Texture upload -------------------------------------------------------
typedef struct {
    uint8_t  u, v;
    uint16_t width, height;
    uint16_t page, clut;
} ModelTextureInfo;

#define MODEL_DMA_MAX_CHUNK_SIZE 16

static void modelWaitForGPUDMADone(void) {
    while (DMA_CHCR(DMA_GPU) & DMA_CHCR_ENABLE)
        __asm__ volatile("");
}

static void modelSendVRAMData(
    const void *data, int x, int y, int width, int height
) {
    modelWaitForGPUDMADone();
    assert(!((uint32_t) data % 4));

    size_t length = (width * height + 1) / 2;
    size_t chunkSize, numChunks;
    if (length < MODEL_DMA_MAX_CHUNK_SIZE) {
        chunkSize = length;
        numChunks = 1;
    } else {
        chunkSize = MODEL_DMA_MAX_CHUNK_SIZE;
        numChunks = length / MODEL_DMA_MAX_CHUNK_SIZE;
        assert(!(length % MODEL_DMA_MAX_CHUNK_SIZE));
    }

    waitForGP0Ready();
    GPU_GP0 = gp0_vramWrite();
    GPU_GP0 = gp0_xy(x, y);
    GPU_GP0 = gp0_xy(width, height);

    DMA_MADR(DMA_GPU) = (uint32_t) data;
    DMA_BCR (DMA_GPU) = chunkSize | (numChunks << 16);
    DMA_CHCR(DMA_GPU) = 0
        | DMA_CHCR_WRITE
        | DMA_CHCR_MODE_SLICE
        | DMA_CHCR_ENABLE;
}

static void modelUploadTexture(
    ModelTextureInfo *info, const void *data, int x, int y, int width, int height
) {
    assert((width <= 256) && (height <= 256));

    modelSendVRAMData(data, x, y, width, height);
    modelWaitForGPUDMADone();
    GPU_GP0 = gp0_flushCache();

    info->page = gp0_page(x / 64, y / 256, GP0_BLEND_SEMITRANS, GP0_COLOR_16BPP);
    info->clut = 0;
    info->u = (uint8_t) (x % 64);
    info->v = (uint8_t) (y % 256);
    info->width  = (uint16_t) width;
    info->height = (uint16_t) height;
}

// --- Controller input ------------------------------------------------------
#define MODEL_JOY_TX_DATA (*(volatile uint8_t  *) 0x1f801040)
#define MODEL_JOY_RX_DATA (*(volatile uint8_t  *) 0x1f801040)
#define MODEL_JOY_STAT    (*(volatile uint32_t *) 0x1f801044)
#define MODEL_JOY_MODE    (*(volatile uint16_t *) 0x1f801048)
#define MODEL_JOY_CTRL    (*(volatile uint16_t *) 0x1f80104a)
#define MODEL_JOY_BAUD    (*(volatile uint16_t *) 0x1f80104e)

#define BUTTON_SELECT   0x0001
#define BUTTON_START    0x0008
#define BUTTON_UP       0x0010
#define BUTTON_RIGHT    0x0020
#define BUTTON_DOWN     0x0040
#define BUTTON_LEFT     0x0080
#define BUTTON_L2       0x0100
#define BUTTON_R2       0x0200
#define BUTTON_L1       0x0400
#define BUTTON_R1       0x0800
#define BUTTON_TRIANGLE 0x1000
#define BUTTON_CIRCLE   0x2000
#define BUTTON_CROSS    0x4000
#define BUTTON_SQUARE   0x8000

static void padInit(void) {
    MODEL_JOY_CTRL = 0x0000;
    MODEL_JOY_MODE = 0x000d; 
    MODEL_JOY_BAUD = 0x0088; 
}

static uint8_t sioExchange(uint8_t value) {
    int timeout = 100000;
    while (!(MODEL_JOY_STAT & 0x0001) && --timeout) __asm__ volatile("");

    MODEL_JOY_TX_DATA = value;

    timeout = 100000;
    while (!(MODEL_JOY_STAT & 0x0002) && --timeout) __asm__ volatile("");
    if (!timeout) return 0xff;

    return MODEL_JOY_RX_DATA;
}

static uint16_t readPad(void) {
    MODEL_JOY_CTRL = 0x1003; 
    for (volatile int i = 0; i < 100; i++) {} 

    sioExchange(0x01);                 
    uint8_t idLo = sioExchange(0x42);  
    if (idLo == 0xff) {
        MODEL_JOY_CTRL = 0x0000;
        return 0xffff; 
    }
    sioExchange(0x00);                 
    uint8_t swLo = sioExchange(0x00);
    uint8_t swHi = sioExchange(0x00);

    MODEL_JOY_CTRL = 0x0000; 
    return (uint16_t) swLo | ((uint16_t) swHi << 8);
}

// --- Debug HUD text --------------------------------------------------------
// Draws small numbers on screen so you can read out the live camera values
// and report them back. Reuses the exact same textured-triangle-pair
// approach as the model faces above (2x FT3 per glyph quad) rather than a
// new primitive type, since that path is already proven to work.
#define FONT_GLYPH_W 8
#define FONT_GLYPH_H 10
#define FONT_COLS    4

// Index into the 4x3 glyph grid baked into assets/font.png: 0-9 = digits,
// 10 = '-', 11 = ':' (spare, unused).
static int glyphIndexForChar(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c == '-') return 10;
    return 11; // unknown chars fall back to a blank-ish glyph
}

static void drawGlyph(
    GPUDMAChain *chain, int screenX, int screenY, int scale,
    char c, const ModelTextureInfo *font, int zIndex
) {
    int index = glyphIndexForChar(c);
    int col = index % FONT_COLS;
    int row = index / FONT_COLS;
    int glyphU = col * FONT_GLYPH_W;
    int glyphV = row * FONT_GLYPH_H;

    int w = FONT_GLYPH_W * scale;
    int h = FONT_GLYPH_H * scale;

    uint32_t xyTL = gp0_xy(screenX,     screenY);
    uint32_t xyTR = gp0_xy(screenX + w, screenY);
    uint32_t xyBL = gp0_xy(screenX,     screenY + h);
    uint32_t xyBR = gp0_xy(screenX + w, screenY + h);

    uint8_t uTL = font->u + glyphU,                vTL = font->v + glyphV;
    uint8_t uTR = font->u + glyphU + FONT_GLYPH_W,  vTR = font->v + glyphV;
    uint8_t uBL = font->u + glyphU,                 vBL = font->v + glyphV + FONT_GLYPH_H;
    uint8_t uBR = font->u + glyphU + FONT_GLYPH_W,  vBR = font->v + glyphV + FONT_GLYPH_H;

    uint32_t *ptr;

    // Triangle 1: top-left, top-right, bottom-left
    ptr = allocateGP0Packet(chain, zIndex, 7);
    ptr[0] = 0x24000000 | gp0_rgb(128, 128, 128);
    ptr[1] = xyTL;
    ptr[2] = ((uint32_t) font->clut << 16) | ((uint32_t) vTL << 8) | uTL;
    ptr[3] = xyTR;
    ptr[4] = ((uint32_t) font->page << 16) | ((uint32_t) vTR << 8) | uTR;
    ptr[5] = xyBL;
    ptr[6] = ((uint32_t) vBL << 8) | uBL;

    // Triangle 2: top-right, bottom-right, bottom-left
    ptr = allocateGP0Packet(chain, zIndex, 7);
    ptr[0] = 0x24000000 | gp0_rgb(128, 128, 128);
    ptr[1] = xyTR;
    ptr[2] = ((uint32_t) font->clut << 16) | ((uint32_t) vTR << 8) | uTR;
    ptr[3] = xyBR;
    ptr[4] = ((uint32_t) font->page << 16) | ((uint32_t) vBR << 8) | uBR;
    ptr[5] = xyBL;
    ptr[6] = ((uint32_t) vBL << 8) | uBL;
}

// Fills buf with a signed decimal string (no null terminator, since the
// caller already knows the length it returns). Returns the number of
// characters written. buf must be at least 12 bytes (worst case
// "-2147483648").
static int formatInt(int value, char *buf) {
    bool neg = value < 0;
    unsigned int v = neg ? (unsigned int) (-value) : (unsigned int) value;

    char tmp[11];
    int n = 0;
    do {
        tmp[n++] = (char) ('0' + (v % 10));
        v /= 10;
    } while (v > 0 && n < (int) sizeof(tmp));

    int len = 0;
    if (neg) buf[len++] = '-';
    while (n > 0) buf[len++] = tmp[--n];
    return len;
}

static void drawNumber(
    GPUDMAChain *chain, int screenX, int screenY, int scale,
    int value, const ModelTextureInfo *font, int zIndex
) {
    char text[12];
    int len = formatInt(value, text);
    int advance = (FONT_GLYPH_W + 1) * scale;

    for (int i = 0; i < len; i++) {
        drawGlyph(chain, screenX + i * advance, screenY, scale, text[i], font, zIndex);
    }
}

#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240
#define STEP_SPEED  16

// Starting pose - the auto-spin loop always resets back to exactly this.
#define START_YAW   1824
#define START_PITCH 670
#define SPIN_SPEED  8 // how much Yaw advances per frame during auto-spin

int main() {
    initSerialIO(115200);
    setupGPU(GP1_MODE_NTSC, SCREEN_WIDTH, SCREEN_HEIGHT);
    setupGTE(SCREEN_WIDTH, SCREEN_HEIGHT);
    padInit();

    ModelTextureInfo texture;
    modelUploadTexture(
        &texture,
        modeltexTextureData,
        SCREEN_WIDTH * 2, 0,
        MODELTEX_WIDTH, MODELTEX_HEIGHT
    );

    // The debug HUD font goes right below the model texture in the same
    // VRAM page (128 tall, so it starts at y=128 and stays within the
    // page's 256-tall band).
    ModelTextureInfo font;
    modelUploadTexture(
        &font,
        fonttexTextureData,
        SCREEN_WIDTH * 2, 128,
        FONTTEX_WIDTH, FONTTEX_HEIGHT
    );

    GPUDMAChain dmaChains[2];
    bool usingSecondFrame = false;

    // Fixed camera settings you said you don't need to control - never
    // touched by any button, always these values.
    const int camDist = 450;
    const int camPanX = 0;
    const int camPanY = 50;

    int camYaw = START_YAW;
    int camPitch = START_PITCH;

    bool manualMode = false;
    bool prevSelect = false, prevStart = false;
    int autoFrameCounter = 0;

    for (;;) {
        int bufferX = usingSecondFrame ? SCREEN_WIDTH : 0;
        int bufferY = 0;

        GPUDMAChain *chain = &dmaChains[usingSecondFrame];
        usingSecondFrame   = !usingSecondFrame;
        uint32_t *ptr;

        GPU_GP1 = gp1_fbOffset(bufferX, bufferY);
        clearOrderingTable(chain->orderingTable, GPU_ORDERING_TABLE_SIZE);
        chain->nextPacket = chain->data;

        uint16_t buttons = readPad();
        bool selectHeld = !(buttons & BUTTON_SELECT);
        bool startHeld  = !(buttons & BUTTON_START);
        bool selectPressedThisFrame = selectHeld && !prevSelect;
        bool startPressedThisFrame  = startHeld  && !prevStart;
        prevSelect = selectHeld;
        prevStart  = startHeld;

        // What Yaw would be right now if still auto-spinning - used both by
        // the auto-spin branch below AND to freeze a seamless starting point
        // for manual mode (no visual jump when you press Select).
        int autoYaw = (START_YAW + autoFrameCounter * SPIN_SPEED) & 4095;

        if (selectPressedThisFrame && !manualMode) {
            camYaw = autoYaw;
            camPitch = START_PITCH;
            manualMode = true;
        }
        if (startPressedThisFrame) {
            manualMode = false;
            autoFrameCounter = 0;
        }

        if (manualMode) {
            // Select was pressed: D-pad now controls ONLY Yaw/Pitch - Dist,
            // PanX and PanY above are never touched by any button.
            if (!(buttons & BUTTON_LEFT))  camYaw   -= STEP_SPEED;
            if (!(buttons & BUTTON_RIGHT)) camYaw   += STEP_SPEED;
            if (!(buttons & BUTTON_UP))    camPitch -= STEP_SPEED;
            if (!(buttons & BUTTON_DOWN))  camPitch += STEP_SPEED;
            camYaw   &= 4095;
            camPitch &= 4095;
        } else {
            // Auto-spin: Yaw continuously cycles 1824 -> 4095 -> 0 -> ... ->
            // 1823, then wraps back to 1824 and repeats. Pitch stays fixed.
            camYaw = autoYaw;
            camPitch = START_PITCH;
            autoFrameCounter++;
        }

        // 1. Set Camera Position (Zoom and Pan) - fixed, per above
        gte_setControlReg(GTE_TRX, camPanX);
        gte_setControlReg(GTE_TRY, camPanY);
        gte_setControlReg(GTE_TRZ, camDist);
        
        // 2. Set Rotation Matrices
        gte_setRotationMatrix(GTE_UNIT, 0, 0, 0, GTE_UNIT, 0, 0, 0, GTE_UNIT);
        
        // Arcball Orbit Yaw
        rotateCurrentMatrix(0, camYaw, 0);   
        // Arcball Orbit Pitch
        rotateCurrentMatrix(0, 0, camPitch); 
        // Force model to lay flat and sideways internally
        rotateCurrentMatrix(1024, 0, 1024);  

        for (int i = 0; i < face_count; i++) {
            const MESH_FACE *face = &model_faces[i];

            gte_loadV0((const GTEVector16 *) &model_vertices[face->v0]);
            gte_loadV1((const GTEVector16 *) &model_vertices[face->v1]);
            gte_loadV2((const GTEVector16 *) &model_vertices[face->v2]);
            gte_command(GTE_CMD_RTPT | GTE_SF);

            uint32_t xy0 = gte_getDataReg(GTE_SXY0);
            uint32_t xy1 = gte_getDataReg(GTE_SXY1);
            uint32_t xy2 = gte_getDataReg(GTE_SXY2);

            gte_command(GTE_CMD_AVSZ3 | GTE_SF);
            int zIndex = (int) gte_getDataReg(GTE_OTZ);
            if (zIndex >= GPU_ORDERING_TABLE_SIZE) continue;
            if (zIndex < 1) zIndex = 1; // slot 0 is reserved for the HUD text below

            ptr = allocateGP0Packet(chain, zIndex, 7);

            ptr[0] = 0x24000000 | gp0_rgb(128, 128, 128);
            ptr[1] = xy0;
            ptr[2] = ((uint32_t) texture.clut << 16)
                   | ((uint32_t) (texture.v + face->tv0) << 8)
                   | (uint32_t) (texture.u + face->tu0);
            ptr[3] = xy1;
            ptr[4] = ((uint32_t) texture.page << 16)
                   | ((uint32_t) (texture.v + face->tv1) << 8)
                   | (uint32_t) (texture.u + face->tu1);
            ptr[5] = xy2;
            ptr[6] = ((uint32_t) (texture.v + face->tv2) << 8)
                   | (uint32_t) (texture.u + face->tu2);
        }

        // DEBUG HUD - shows the live camera values so you can read them off
        // and report back a pose you like. Order: Yaw, Pitch, Dist, PanX,
        // PanY, one per line, top-left corner. Drawn at zIndex 0, which is
        // reserved (see the "slot 0" clamp above) so this always stays on
        // top of the model.
        drawNumber(chain, 8,  8, 1, camYaw,   &font, 0);
        drawNumber(chain, 8, 20, 1, camPitch, &font, 0);
        drawNumber(chain, 8, 32, 1, camDist,  &font, 0);
        drawNumber(chain, 8, 44, 1, camPanX,  &font, 0);
        drawNumber(chain, 8, 56, 1, camPanY,  &font, 0);

        ptr = allocateGP0Packet(chain, GPU_ORDERING_TABLE_SIZE - 1, 3);
        ptr[0] = gp0_rgb(32, 32, 64) | gp0_vramFill();
        ptr[1] = gp0_xy(bufferX, bufferY);
        ptr[2] = gp0_xy(SCREEN_WIDTH, SCREEN_HEIGHT);

        ptr = allocateGP0Packet(chain, GPU_ORDERING_TABLE_SIZE - 1, 4);
        ptr[0] = gp0_setPage(0, true, false);
        ptr[1] = gp0_fbOffset1(bufferX, bufferY);
        ptr[2] = gp0_fbOffset2(bufferX + SCREEN_WIDTH  - 1, bufferY + SCREEN_HEIGHT - 1);
        ptr[3] = gp0_fbOrigin(bufferX, bufferY);

        waitForGP0Ready();
        waitForVSync();
        sendGPULinkedList(&(chain->orderingTable)[GPU_ORDERING_TABLE_SIZE - 1]);
    }
    return 0;
}
